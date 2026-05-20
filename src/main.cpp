// ES3C28P audio demo — ES8311 codec + FM8002E amp + SD card
// See Docs/ES3C28P_Audio_Reference.md for full hardware notes.

#include <Arduino.h>
#include <Wire.h>
#include <SD_MMC.h>
#include <Audio.h>
#include <WiFi.h>
#include <esp_bt.h>

// ── Pin definitions ───────────────────────────────────────────
#define PIN_I2C_SDA   16
#define PIN_I2C_SCL   15
#define PIN_I2S_MCLK   4
#define PIN_I2S_BCLK   5
#define PIN_I2S_LRCK   7
// IO8 = ES8311 SDIN  → ESP32 sends audio data here (playback)
// IO6 = ES8311 SDOUT → codec sends mic data here  (recording, not used)
#define PIN_I2S_DOUT   8
#define PIN_SD_CLK    38
#define PIN_SD_CMD    40
#define PIN_SD_D0     39
#define PIN_SD_D1     41
#define PIN_SD_D2     48
#define PIN_SD_D3     47
#define PIN_PA_EN      1   // FM8002E SHUTDOWN, active LOW
#define PIN_BAT_ADC    9   // 2:1 resistor divider
#define ES8311_ADDR   0x18

Audio audio;

// ── Playlist ──────────────────────────────────────────────────
static const char* SOUNDS[] = {
    "/sounds/angry.mp3",
    "/sounds/anxiety.mp3",
    "/sounds/happy.mp3",
    "/sounds/listening.mp3",
    "/sounds/sad.mp3",
    "/sounds/sleep.mp3",
    "/sounds/surprise.mp3",
    "/sounds/whacky.mp3",
};
static const int NUM_SOUNDS = sizeof(SOUNDS) / sizeof(SOUNDS[0]);
static int  soundIndex   = 0;
static unsigned long soundStartMs = 0;

// ── Battery monitoring ────────────────────────────────────────
static float readBatteryVoltage() {
    int sum = 0;
    for (int i = 0; i < 16; i++) sum += analogRead(PIN_BAT_ADC);
    return (sum / 16.0f) * (3.3f / 4095.0f) * 2.0f;  // ×2 for divider
}

static void printBattery() {
    float v = readBatteryVoltage();
    int pct = constrain((int)((v - 3.0f) / 1.2f * 100.0f), 0, 100);
    Serial.printf("Battery: %.2fV  %d%%%s\n", v, pct, pct < 20 ? "  *** LOW ***" : "");
}

// ── ES8311 codec init ─────────────────────────────────────────
// Writes registers directly over I2C. MCLK must already be running.
// Full register table in Docs/ES3C28P_Audio_Reference.md.
static bool initES8311() {
    auto wr = [](uint8_t reg, uint8_t val) {
        Wire.beginTransmission(ES8311_ADDR);
        Wire.write(reg); Wire.write(val);
        if (Wire.endTransmission()) Serial.printf("  I2C err reg 0x%02X\n", reg);
    };
    auto rd = [](uint8_t reg) -> uint8_t {
        Wire.beginTransmission(ES8311_ADDR);
        Wire.write(reg); Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
        return Wire.available() ? Wire.read() : 0xFF;
    };

    Wire.beginTransmission(ES8311_ADDR);
    if (Wire.endTransmission()) { Serial.println("ES8311: not found"); return false; }
    Serial.printf("ES8311: chip ID 0x%02X (expect 0x83)\n", rd(0xFD));

    // Reset then enter slave mode (ESP32 drives all clocks)
    wr(0x00, 0x1F); delay(20);
    wr(0x00, 0x80);

    // Clocks: MCLK from pin, BCLK = MCLK/8, LRCK period = 256 → 44100 Hz
    wr(0x01, 0x3F);
    wr(0x02, 0x00); wr(0x03, 0x10); wr(0x04, 0x10);
    wr(0x05, 0x00); wr(0x06, 0x03); wr(0x07, 0x00); wr(0x08, 0xFF);

    // Serial format: standard I2S, 16-bit (bits[5:4]=00 = standard, NOT left-justified)
    wr(0x09, 0x0C);  // SDPIN  (DAC input)
    wr(0x0A, 0x0C);  // SDPOUT (ADC output)

    // Analog power-up
    wr(0x0B, 0x00); wr(0x0C, 0x00);
    wr(0x0D, 0x01); wr(0x0E, 0x02); wr(0x0F, 0x44);
    wr(0x10, 0x1F); wr(0x11, 0x7F); wr(0x12, 0x00);
    wr(0x13, 0x10); wr(0x14, 0x1A);

    // ADC (mic path — powered up but muted at the serial port)
    wr(0x15, 0x40); wr(0x16, 0x24); wr(0x17, 0xBF);
    wr(0x1B, 0x0A); wr(0x1C, 0x6A);

    // DAC: unmute, max digital volume, enable output
    wr(0x31, 0x00);  // L+R unmuted
    wr(0x32, 0xFF);  // L volume 0 dB
    wr(0x33, 0xFF);  // R volume 0 dB
    wr(0x37, 0x08);  // output enable, bypass equalizer
    wr(0x45, 0x00);

    // Unmute DAC serial input; mute ADC serial output (mic not used)
    wr(0x09, 0x0C);  // SDPIN:  bit6=0 → not muted
    wr(0x0A, 0x4C);  // SDPOUT: bit6=1 → muted

    delay(50);
    Serial.println("ES8311: init OK");
    return true;
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== ES3C28P audio demo ===");

    // Radio off — reduces noise and saves power
    WiFi.mode(WIFI_OFF);
    esp_bt_controller_disable();

    analogReadResolution(12);
    printBattery();

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);

    // 1. Start I2S (this also starts MCLK — codec needs it before init)
    audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT,
                    I2S_PIN_NO_CHANGE, PIN_I2S_MCLK);
    i2s_set_sample_rates(I2S_NUM_0, 44100);
    delay(20);

    // 2. Init ES8311 codec via I2C
    if (!initES8311()) { Serial.println("ABORT"); return; }

    // 3. Enable FM8002E amplifier (active LOW)
    pinMode(PIN_PA_EN, OUTPUT);
    digitalWrite(PIN_PA_EN, LOW);

    // 4. Software volume (0–21). 8 is a comfortable room level.
    audio.setVolume(2);

    // 5. Mount SD card (4-bit mode, fall back to 1-bit)
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0,
                   PIN_SD_D1,  PIN_SD_D2,  PIN_SD_D3);
    bool sdOk = SD_MMC.begin("/sdcard", false) || SD_MMC.begin("/sdcard", true);
    if (!sdOk) { Serial.println("SD: failed"); return; }
    Serial.println("SD: OK");

    // 6. Play — cycles through all sounds, 5 seconds each
    soundIndex = 0;
    audio.connecttoFS(SD_MMC, SOUNDS[soundIndex]);
    soundStartMs = millis();
}

void loop() {
    audio.loop();

    if (millis() - soundStartMs >= 5000) {
        soundIndex = (soundIndex + 1) % NUM_SOUNDS;
        Serial.printf("[playlist] -> %s\n", SOUNDS[soundIndex]);
        audio.connecttoFS(SD_MMC, SOUNDS[soundIndex]);
        soundStartMs = millis();
    }
}

// Required by ESP32-audioI2S to receive decoder status messages
void audio_info(const char* info) {
    Serial.printf("[audio] %s\n", info);
}

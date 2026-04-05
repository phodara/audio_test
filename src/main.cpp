// ============================================================
//  audio_test — direct I2S WAV player (no Audio library)
//  ES3C28P board: ES8311 + FM8002E + SD card
//
//  Uses the same 32-bit I2S config proven to work cleanly.
//  Reads 16-bit stereo WAV from SD and shifts samples into
//  the upper 16 bits of 32-bit I2S frames for ES8311.
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <SD_MMC.h>
#include <driver/i2s.h>

#define PIN_I2C_SDA   16
#define PIN_I2C_SCL   15
#define PIN_I2S_MCLK   4
#define PIN_I2S_BCLK   5
#define PIN_I2S_LRCK   7
#define PIN_I2S_DOUT   6
#define PIN_SD_CLK    38
#define PIN_SD_CMD    40
#define PIN_SD_D0     39
#define PIN_SD_D1     41
#define PIN_SD_D2     48
#define PIN_SD_D3     47
#define PIN_PA_EN      1
#define ES8311_ADDR   0x18

// ── ES8311 init ───────────────────────────────────────────────
static bool initES8311() {
    auto wr = [](uint8_t reg, uint8_t val) -> bool {
        Wire.beginTransmission(ES8311_ADDR);
        Wire.write(reg); Wire.write(val);
        int rc = Wire.endTransmission();
        if (rc) Serial.printf("  I2C FAIL reg 0x%02X rc=%d\n", reg, rc);
        return rc == 0;
    };
    auto rd = [](uint8_t reg) -> uint8_t {
        Wire.beginTransmission(ES8311_ADDR);
        Wire.write(reg); Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
        return Wire.available() ? Wire.read() : 0xFF;
    };

    Wire.beginTransmission(ES8311_ADDR);
    if (Wire.endTransmission()) { Serial.println("ES8311: NOT FOUND"); return false; }
    Serial.printf("ES8311: chip ID = 0x%02X\n", rd(0xFD));

    wr(0x00, 0x1F); delay(20);  // reset
    wr(0x00, 0x80);              // CSM on, slave mode
    wr(0x01, 0x3F);              // all clocks on, MCLK from pin
    wr(0x02, 0x00);
    wr(0x03, 0x10);              // ADC OSR=16
    wr(0x04, 0x10);              // DAC OSR=16
    wr(0x05, 0x00);
    wr(0x06, 0x03);
    wr(0x07, 0x00);
    wr(0x08, 0xFF);

    // 32-bit I2S word length (matches our I2S config)
    wr(0x09, 0x10);              // SDPIN:  32-bit I2S, unmuted
    wr(0x0A, 0x10);              // SDPOUT: 32-bit I2S

    wr(0x0B, 0x00);
    wr(0x0C, 0x00);
    wr(0x0D, 0x05);              // analog on, VMID startup
    wr(0x0E, 0x02);
    wr(0x10, 0x1F);
    wr(0x11, 0x7C);
    wr(0x12, 0x01);              // DAC on, ENREFR=1
    wr(0x13, 0x40);              // line-out
    wr(0x14, 0x1A);
    wr(0x15, 0x40);
    wr(0x16, 0x24);
    wr(0x17, 0xBF);
    wr(0x1B, 0x0A);
    wr(0x1C, 0x6A);
    wr(0x31, 0x00);              // DAC unmuted
    wr(0x32, 0xBF);              // DAC volume = 0 dB
    wr(0x37, 0x08);
    wr(0x44, 0x08);
    wr(0x45, 0x00);
    wr(0x09, 0x10);              // SDPIN  unmuted, 32-bit
    wr(0x0A, 0x50);              // SDPOUT muted,   32-bit

    Serial.println("ES8311: init OK");
    return true;
}

// ── WAV loader: reads file into PSRAM, parses header ─────────
static uint8_t* s_wavBuf    = nullptr;
static size_t   s_wavDataOff = 0;
static size_t   s_wavDataLen = 0;
static uint16_t s_wavChannels   = 2;
static uint32_t s_wavSampleRate = 44100;
static uint16_t s_wavBitsPerSample = 16;

static bool loadWav(const char* path) {
    File f = SD_MMC.open(path);
    if (!f) { Serial.printf("Cannot open %s\n", path); return false; }

    size_t total = f.size();
    if (total < 44) { f.close(); return false; }

    if (s_wavBuf) { heap_caps_free(s_wavBuf); s_wavBuf = nullptr; }
    s_wavBuf = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!s_wavBuf) { Serial.println("PSRAM alloc failed"); f.close(); return false; }

    f.read(s_wavBuf, total);
    f.close();

    // Verify RIFF/WAVE signature
    if (memcmp(s_wavBuf, "RIFF", 4) || memcmp(s_wavBuf + 8, "WAVE", 4)) {
        Serial.println("WAV: bad header"); return false;
    }

    // Parse fmt chunk
    uint8_t* p = s_wavBuf + 12;
    uint8_t* end = s_wavBuf + total;
    s_wavDataOff = 0;
    s_wavDataLen = 0;

    while (p + 8 <= end) {
        uint32_t chunkId  = p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24);
        uint32_t chunkLen = p[4] | (p[5]<<8) | (p[6]<<16) | (p[7]<<24);
        p += 8;
        if (memcmp(p - 8, "fmt ", 4) == 0) {
            uint16_t audioFmt  = p[0] | (p[1] << 8);
            s_wavChannels      = p[2] | (p[3] << 8);
            s_wavSampleRate    = p[4] | (p[5]<<8) | (p[6]<<16) | (p[7]<<24);
            s_wavBitsPerSample = p[14] | (p[15] << 8);
            Serial.printf("WAV fmt: audioFormat=%u (1=PCM)\n", audioFmt);
            if (audioFmt != 1) {
                Serial.println("WAV: not PCM — cannot play"); return false;
            }
        } else if (memcmp(p - 8, "data", 4) == 0) {
            s_wavDataOff = p - s_wavBuf;
            s_wavDataLen = chunkLen;
            break;
        }
        p += chunkLen + (chunkLen & 1);  // chunks are word-aligned
    }

    if (!s_wavDataOff) { Serial.println("WAV: no data chunk"); return false; }

    Serial.printf("WAV: %u Hz, %u ch, %u-bit, data @ offset %u (%u bytes)\n",
        s_wavSampleRate, s_wavChannels, s_wavBitsPerSample,
        (unsigned)s_wavDataOff, (unsigned)s_wavDataLen);
    return true;
}

// ── WAV player: plays from PSRAM ──────────────────────────────
static void playWav() {
    if (!s_wavBuf || !s_wavDataLen) return;

    int16_t* samples = (int16_t*)(s_wavBuf + s_wavDataOff);
    size_t   nSamples = s_wavDataLen / 2;

    const int CHUNK = 128;  // max mono samples per iteration (outputs 2× words)
    int32_t out32[CHUNK * 2];

    size_t i = 0;
    while (i < nSamples) {
        int count = (CHUNK < (int)(nSamples - i)) ? CHUNK : (int)(nSamples - i);
        int words = 0;
        if (s_wavChannels == 2) {
            for (int j = 0; j < count; j++)
                out32[words++] = (int32_t)samples[i + j] << 16;
        } else {
            // mono: duplicate each sample to both L and R slots
            for (int j = 0; j < count; j++) {
                int32_t s = (int32_t)samples[i + j] << 16;
                out32[words++] = s;
                out32[words++] = s;
            }
        }
        size_t written;
        i2s_write(I2S_NUM_0, out32, words * 4, &written, portMAX_DELAY);
        i += count;
    }
    Serial.println("WAV done.");
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== audio_test ===");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);

    // I2S: 32-bit slots, MCLK=256×fs
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = 44100,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 128,
        .use_apll             = true,
        .tx_desc_auto_clear   = true,
        .mclk_multiple        = I2S_MCLK_MULTIPLE_256,
    };
    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL));

    i2s_pin_config_t pins = {
        .mck_io_num   = PIN_I2S_MCLK,
        .bck_io_num   = PIN_I2S_BCLK,
        .ws_io_num    = PIN_I2S_LRCK,
        .data_out_num = PIN_I2S_DOUT,
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };
    ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM_0, &pins));

    delay(50);  // let MCLK stabilise

    if (!initES8311()) { Serial.println("ABORT"); return; }

    // SD
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0,
                   PIN_SD_D1,  PIN_SD_D2,  PIN_SD_D3);
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("SD: FAILED"); return;
    }
    Serial.println("SD: OK");

    // Amp on
    pinMode(PIN_PA_EN, OUTPUT);
    digitalWrite(PIN_PA_EN, LOW);
    Serial.println("Amp ON");

    loadWav("/sounds/anxiety.wav");
    SD_MMC.end();  // stop MMC clock — no longer needed, reduces noise
    Serial.println("SD: unmounted");
}

static void playTone(float freqHz, float durationSec) {
    const int SAMPLE_RATE = 44100;
    const int CHUNK = 256;
    int32_t out32[CHUNK];
    int totalSamples = (int)(SAMPLE_RATE * durationSec);
    float phaseInc = 2.0f * M_PI * freqHz / SAMPLE_RATE;
    float phase = 0.0f;
    int i = 0;
    while (i < totalSamples) {
        int count = (CHUNK < totalSamples - i) ? CHUNK : (totalSamples - i);
        for (int j = 0; j < count; j++) {
            int16_t s = (int16_t)(16000.0f * sinf(phase));  // ~-6 dB headroom
            out32[j] = (int32_t)s << 16;
            phase += phaseInc;
            if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;
        }
        size_t written;
        i2s_write(I2S_NUM_0, out32, count * 4, &written, portMAX_DELAY);
        i += count;
    }
}

void loop() {
    playWav();
    delay(2000);
}

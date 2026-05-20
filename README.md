# Hosyond ESP32-S3 Audio

WAV audio player for the **ES3C28P** board (2.8" ESP32-S3 Display).  
Plays 16-bit PCM WAV files from SD card through the onboard ES8311 codec and FM8002E class-D amplifier.

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32-S3 |
| Audio codec | ES8311 (I2C + I2S) |
| Amplifier | FM8002E (BTL, max 2W into 4Ω) |
| Storage | MicroSD via SD-MMC (4-bit) |
| Speaker connector | JST 1.25mm 2-pin |
| Battery charger | TP4054 (single-cell LiPo) |
| Regulator | ME6217 LDO |

## How it works

1. On startup the ES8311 codec is initialized over I2C, then the FM8002E amplifier is enabled.
2. The player streams MP3 files directly from the SD card using the `ESP32-audioI2S` library — no PSRAM buffering needed.
3. A playlist of 8 sounds cycles automatically, playing each for 5 seconds before advancing.
4. Battery voltage is read on GPIO9 via a 2:1 voltage divider and printed to serial at startup.

## Audio file requirements

The player expects MP3 files on the SD card in `/sounds/` with these names:

```
/sounds/angry.mp3
/sounds/anxiety.mp3
/sounds/happy.mp3
/sounds/listening.mp3
/sounds/sad.mp3
/sounds/sleep.mp3
/sounds/surprise.mp3
/sounds/whacky.mp3
```

Any standard MP3 file works. To convert from AIFF on macOS:

```bash
ffmpeg -i input.aiff -codec:a libmp3lame -q:a 2 /Volumes/YOUR_SD/sounds/anxiety.mp3
```

## Battery monitoring

Battery voltage is read from **GPIO9** (IO9 on the board), which has a 2:1 voltage divider.

Output on serial monitor:
```
Battery: 3.85V  70%
```

Voltage thresholds (single-cell LiPo):

| Voltage | State |
|---------|-------|
| 4.2V | 100% (fully charged) |
| 3.7V | ~58% (nominal) |
| 3.3V | ~25% (low) |
| 3.0V | 0% (critical — stop use) |

Battery monitoring code:

```cpp
#define PIN_BAT_ADC 9

static float readBatteryVoltage() {
    int sum = 0;
    for (int i = 0; i < 16; i++) sum += analogRead(PIN_BAT_ADC);
    float adcV = (sum / 16.0f) * 3.3f / 4095.0f;
    return adcV * 2.0f;  // x2 for voltage divider
}

static int batteryPercent(float v) {
    if (v >= 4.2f) return 100;
    if (v <= 3.0f) return 0;
    return (int)((v - 3.0f) / (4.2f - 3.0f) * 100.0f);
}

static void printBattery() {
    float v = readBatteryVoltage();
    int pct = batteryPercent(v);
    Serial.printf("Battery: %.2fV  %d%%", v, pct);
    if (pct < 20) Serial.print("  *** LOW ***");
    Serial.println();
}
```

Call `analogReadResolution(12)` before first use.  
Prints at startup and every 30 seconds during `loop()`.

## Pin reference

| Signal | GPIO |
|--------|------|
| I2C SDA (ES8311) | 16 |
| I2C SCL (ES8311) | 15 |
| I2S MCLK | 4 |
| I2S BCLK | 5 |
| I2S LRCK | 7 |
| I2S DOUT (→ ES8311 SDIN) | 8 |
| SD CLK | 38 |
| SD CMD | 40 |
| SD D0 | 39 |
| SD D1 | 41 |
| SD D2 | 48 |
| SD D3 | 47 |
| PA enable (active LOW) | 1 |
| Battery ADC | 9 |

## I2S data pin naming — codec vs. ESP32 perspective

The board documentation labels I2S data pins from the **ES8311 codec's** perspective, not the ESP32's. This is the opposite of how most dev board silkscreens work and was the root cause of audio not working.

| GPIO | ES8311 label | ES8311 POV | ESP32 POV | Used for |
|------|-------------|------------|-----------|----------|
| 8 | I2S_DI | **Input** (codec receives) | **Output** (ESP32 sends) | Playback (DAC) |
| 6 | I2S_DO | **Output** (codec sends) | **Input** (ESP32 receives) | Recording (ADC, unused) |

`I2S_DI` means *data in to the codec* — so the ESP32 must drive GPIO8 as its output to play audio. Wiring GPIO6 instead sends data into `I2S_DO`, the codec's own output line, which can't receive anything.

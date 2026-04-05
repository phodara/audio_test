# audio_test

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

1. On startup the WAV file is loaded entirely into PSRAM — SD card activity stops before playback begins, eliminating MMC bus interference.
2. Audio is played as 32-bit I2S frames (16-bit samples shifted into the upper 16 bits) which is required for the ES8311 to read samples correctly.
3. Battery voltage is read on GPIO9 via a 2:1 voltage divider and printed to serial at startup and every 30 seconds.

## Audio file requirements

The player expects a standard PCM WAV file on the SD card at `/sounds/anxiety.wav`.

Required format:
- Format: PCM (uncompressed), audioFormat = 1
- Sample rate: 44100 Hz
- Channels: mono or stereo
- Bit depth: 16-bit

To generate a speech WAV on macOS using the built-in `say` command:

```bash
say -o /tmp/speech.aiff "Your text here"
afconvert -f WAVE -d LEI16@44100 -c 2 /tmp/speech.aiff /Volumes/YOUR_SD/sounds/anxiety.wav
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

## Grounding issue — standalone battery operation

**Symptom:** Audio only plays when a finger touches the GND pin. Releasing contact stops audio.

**Cause:** The ES8311 analog section requires a stable ground reference. Without a connection to earth ground, the board's ground floats and the audio circuit loses its reference. On USB power a partial earth path exists through the cable; on battery alone there is none.

**Solutions:**

### Option 1 — Copper foil in enclosure (recommended for final product)
Line the inside of the plastic enclosure with self-adhesive copper foil tape and connect it to a GND pin on the board with a short wire. The copper plane provides distributed capacitance to earth ground without needing a direct earth connection. This is standard practice in portable audio devices.

### Option 2 — Wire to a grounded object (for bench testing)
Connect a wire from any GND pin to a large metal object that is itself grounded — a laptop chassis (while plugged into AC), a metal desk lamp, or the screw on a grounded power outlet cover plate.

### Option 3 — USB cable to grounded host (development only)
When developing over USB, connecting to a Mac or PC that is plugged into AC power provides a ground path through the USB cable shield. This is not a solution for the standalone product.

**Note:** The FM8002E is a BTL (bridge-tied load) amplifier — its speaker outputs are fully differential and do not connect to ground. The grounding requirement comes from the ES8311 analog reference, not the speaker wiring.

## Pin reference

| Signal | GPIO |
|--------|------|
| I2C SDA (ES8311) | 16 |
| I2C SCL (ES8311) | 15 |
| I2S MCLK | 4 |
| I2S BCLK | 5 |
| I2S LRCK | 7 |
| I2S DOUT | 6 |
| SD CLK | 38 |
| SD CMD | 40 |
| SD D0 | 39 |
| SD D1 | 41 |
| SD D2 | 48 |
| SD D3 | 47 |
| PA enable (active LOW) | 1 |
| Battery ADC | 9 |

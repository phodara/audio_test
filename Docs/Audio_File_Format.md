# Audio File Format — ES3C28P

## Recommended format

| Property | Value |
|----------|-------|
| Container | MP3 |
| Sample rate | 44100 Hz |
| Channels | Stereo or Mono |
| Bitrate | 128–192 kbps CBR |
| Filename | Lowercase, no spaces (e.g. `happy.mp3`) |

44100 Hz is preferred because the ES8311 codec is clocked at 11.2896 MHz (44100 × 256). Files at other sample rates work but require the library to reconfigure I2S mid-playback, which can cause a brief click between tracks.

---

## What the library supports

The schreibfaul1 ESP32-audioI2S library (v2.0.0) handles a wide range of MP3 files:

- **Sample rates:** 8, 11.025, 16, 22.05, 24, 32, 44.1, 48 kHz — all work
- **Bitrates:** 32–320 kbps — all work; stay under 256 kbps on slow SD cards
- **Channels:** mono or stereo — both work
- **CBR and VBR:** both supported

---

## Files tested on this board

| File | Rate | Bitrate | Channels | Notes |
|------|------|---------|----------|-------|
| angry.mp3 | 44.1 kHz | 256 kbps | Mono | ✓ ideal rate |
| anxiety.mp3 | 24 kHz | 160 kbps | Stereo | ✓ works, lower quality |
| happy.mp3 | 48 kHz | 256 kbps | Stereo | ✓ works |
| listening.mp3 | 44.1 kHz | 256 kbps | Mono | ✓ ideal rate |
| sad.mp3 | 24 kHz | 160 kbps | Stereo | ✓ works |
| sleep.mp3 | 24 kHz | 160 kbps | Stereo | ✓ works |
| surprise.mp3 | 24 kHz | 160 kbps | Stereo | ✓ works |
| whacky.mp3 | 48 kHz | 256 kbps | Stereo | ✓ works |

---

## SD card placement

Files must be placed in the `/sounds/` folder at the root of the SD card:

```
SD card root/
└── sounds/
    ├── angry.mp3
    ├── anxiety.mp3
    └── ...
```

The SD card must be formatted as **FAT32**. Use lowercase filenames — some FAT32 implementations are case-sensitive.

---

## Converting files with ffmpeg

To convert any audio file to the recommended format:

```bash
ffmpeg -i input.wav -ar 44100 -ac 2 -b:a 128k output.mp3
```

| Flag | Meaning |
|------|---------|
| `-ar 44100` | Sample rate 44100 Hz |
| `-ac 2` | Stereo (use `-ac 1` for mono) |
| `-b:a 128k` | Bitrate 128 kbps |

To batch convert a folder:

```bash
for f in *.wav; do ffmpeg -i "$f" -ar 44100 -ac 2 -b:a 128k "${f%.wav}.mp3"; done
```

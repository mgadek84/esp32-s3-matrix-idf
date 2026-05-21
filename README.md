# esp32-s3-matrix-idf

ESP-IDF (C) firmware for **Waveshare ESP32-S3-Matrix** — 8×8 WS2812 RGB LED matrix.

Part of the [ESP32_microcontroller_project_IoT](https://github.com/mgadek84/ESP32_microcontroller_project_IoT) learning track (ESP32 + IoT); this repo is the **ESP-IDF / C** LED demo; the parent repo focuses on MicroPython.

## Hardware

| Item | Value |
|------|--------|
| Board | [Waveshare ESP32-S3-Matrix](https://www.waveshare.com/esp32-s3-matrix) |
| LEDs | 64 × WS2812 (8×8) |
| Data pin | **GPIO 14** |
| USB serial | e.g. `COM3` (Windows) |

## What it does

**Beat-sync light show** for *NO REPLY - NTPV…* (~128 s, ~103 BPM): kicks, snares, bass, hi-hats and section changes drive flashes, ripples and color moods on the 8×8 matrix.

1. Flash firmware, open monitor.
2. Start the **same MP3** when the matrix shows the **3-2-1** countdown (3 s after boot).
3. Timeline is in `main/bit_timeline.h` (regenerate: `python tools/analyze_bit.py`).

To use another track:

```powershell
pip install -r tools/requirements.txt
python tools/analyze_bit.py "path\to\track.mp3"
```

Optional debug dump: add `--json` (writes `tools/bit_analysis.json`, gitignored).

## Build & flash

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html) v5.x / v6.x and ESP-IDF PowerShell (or `export.ps1`).

```powershell
idf.py set-target esp32s3
idf.py -p COM3 build flash monitor
```

Replace `COM3` with your port. Exit monitor: **Ctrl+]**.

## Project layout

```text
├── CMakeLists.txt
├── sdkconfig.defaults
├── tools/
│   ├── analyze_bit.py      # MP3 → main/bit_timeline.h
│   └── requirements.txt
└── main/
    ├── hello_s3.c
    ├── bit_timeline.h      # generated timeline
    ├── CMakeLists.txt
    └── idf_component.yml   # espressif/led_strip
```

## Notes

- Use a folder name **without special Unicode characters** (e.g. avoid `—` in path); ESP-IDF can fail on broken paths on Windows.
- If the matrix stays dark, confirm **GPIO 14** (not 38) for this board.

## License

MIT — use freely for learning and hacks.

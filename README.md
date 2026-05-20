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

Simple **rainbow** effect: full matrix cycles through red → orange → yellow → green → cyan → blue → violet (no white flash; channels kept moderate).

## Build & flash

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html) v5.x / v6.x and ESP-IDF PowerShell (or `export.ps1`).

```powershell
cd hello_s3_kopia
idf.py set-target esp32s3
idf.py -p COM3 build flash monitor
```

Replace `COM3` with your port. Exit monitor: **Ctrl+]**.

## Project layout

```text
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── hello_s3.c
    ├── CMakeLists.txt
    └── idf_component.yml   # espressif/led_strip
```

## Notes

- Use a folder name **without special Unicode characters** (e.g. avoid `—` in path); ESP-IDF can fail on broken paths on Windows.
- If the matrix stays dark, confirm **GPIO 14** (not 38) for this board.

## License

MIT — use freely for learning and hacks.

# Hollow Star OS Basic — Project Context (RP2040 / Raspberry Pi Pico W)

## Overview
Compact embedded OS written in C++ (Arduino-Pico / Earle Philhower core v6.4.0) running on **FreeRTOS SMP** on a Raspberry Pi Pico W (RP2040).
Current resource constraints: **~71% Flash used (1 MB sketch partition)**, tight 264 KB SRAM. Avoid heavy dynamic allocations and large stack frames.

## Build Configuration (Arduino IDE / arduino-cli)
- **Board:** Raspberry Pi Pico W (`rp2040:rp2040:rpipicow`)
- **Flash Size:** `2MB (Sketch: 1MB, FS: 1MB)` (LittleFS required for `/config.cfg` and `.md` notes)
- **Operating System:** `FreeRTOS SMP`
- **IP/Bluetooth Stack:** `IPv4 + Bluetooth`
- **CPU Speed:** `240 MHz (Overclock)` (must stay a multiple of 120 MHz for Pico-PIO-USB + fast enough for MP3/A2DP)
- **USB Stack:** `Adafruit TinyUSB`

## Hardware Pinout
- **ST7565R 128x64 LCD (GMG12864-06D Ver.2.2, 3.3V logic/power ONLY, U8g2 SW SPI):**
  - `SCL=GP18`, `SI=GP19`, `CS=GP17`, `RS/DC=GP16`, `RSE/RST=GP20`, `VDD=3V3`, `A=3V3 via 47-100Ω`, `K/VSS=GND`
  - Default contrast: `20/63` (`u8g2.setContrast(contrast * 4)`), 180° orientation (`U8G2_R0`/`U8G2_R2`) avoids the ST7565 4px RAM offset.
- **Catalex MicroSD v1.0 (FAT32, hardware SPI1):**
  - `VCC=VBUS (5V!)`, `SCK=GP10`, `MOSI=GP11`, `MISO=GP12`, `CS=GP13`
  - Uses `SPI_HALF_SPEED` with fallback to `SPI_QUARTER_SPEED` for breadboard stability.
- **USB-A 2.4 GHz Keyboard Receiver (Pico-PIO-USB on Core 1):**
  - `VCC=VBUS (5V)`, `D+=GP2`, `D-=GP3`, `GND=GND`

## Architecture & Critical Constraints (DO NOT BREAK)
1. **Header Isolation:** `Adafruit_TinyUSB.h` and `BluetoothAudio.h` (BTstack) have conflicting HID macro/enum names (`HID_REPORT_TYPE_INPUT`, etc.). They MUST stay in separate translation units (`usb_kbd.cpp` vs `bt_audio.cpp`).
2. **Core Affinity & FreeRTOS:**
   - Core 1 (`setup1`/`loop1` in `usb_kbd.cpp`): dedicated exclusively to `USBHost.task()`, pushes keys into `keyQueue`.
   - Core 0: `loop()` (UI + `U8g2`), `TaskAudioBT` (pinned to Core 0, owns `SD` audio + `A2DPSource` + `MP3DecoderHelix`), `TaskNet` (pinned to Core 0, owns Wi-Fi STA, NTP `CET-1CEST`, PASV FTP port 21).
   - Shared access to `LittleFS` and `SD` is guarded by `fsMutex` (`fsLock()` / `fsUnlock()` in `storage_cfg.cpp`).
3. **Audio Decoder Rule:** Do NOT use `BackgroundAudioMP3` under FreeRTOS SMP — its User IRQ calls `a2dp.write()`, which deadlocks on FreeRTOS/CYW43 mutexes. Always keep synchronous `arduino-libhelix` (`MP3DecoderHelix`) inside `TaskAudioBT`, feeding digital silence when idle.

## UI Conventions
- Strictly sharp, square icons and frames (`drawFrame`/`drawBox`, no rounded corners).
- Persistent top status bar: Left = USB keyboard icon + connection dot; Center = `HH:MM`; Right = `W` (Wi-Fi) + Bluetooth icon + connection dot.
- 2-page main menu with bottom-right dot indicators (**hollow square = active page, filled square = inactive page**):
  - Page 1: Temp RP2040, Calculator, Markdown Notes (`.md`), Games (Brick Breaker, Tetris, Brick Game Racing), Music (SD -> BT A2DP), Settings (BT device scanner, Wi-Fi/NTP, Contrast, Time, Format Flash/SD, About).
  - Page 2: File Explorer (Flash/SD), Tools (FTP Server, NTP Sync), Suspend (`setPowerSave(1)`).

## Build, Flash & Hardware Access (macOS)
- The Raspberry Pi Pico W is connected via USB (`/dev/cu.usbmodem*`).
- Compile only: `./pico.sh build`
- Compile & flash to Pico W: `./pico.sh flash`
- Capture 5s of Serial Monitor logs (115200 baud, non-blocking): `./pico.sh logs`
- Always run `./pico.sh build` after code changes to verify zero compilation errors and check Flash/RAM usage before flashing.

## Imprtant! Always read folder_structure.txt for the planned directory layout and keep it updated when adding new top-level modules.

# Hollow Star OS Basic

A compact, sharp-pixel embedded operating system for the **Raspberry Pi Pico W (RP2040)** running on **FreeRTOS SMP** (Arduino-Pico core v6.4.0), featuring a 128×64 monochrome LCD UI, USB 2.4 GHz keyboard input via PIO, Bluetooth A2DP MP3 streaming, Wi-Fi + NTP clock sync, a passive FTP server, a Markdown notebook, a dual-drive file explorer, an expression calculator, and classic retro games.

---

## Features

- **Custom Sharp-Pixel UI (`U8g2`)**: Zero rounded corners, boot splash screen with the Hollow Star logo, persistent top status bar (USB keyboard status, `HH:MM` clock, 4-bar Wi-Fi signal strength indicator, Bluetooth audio status), and 2-page main launcher.
- **Universal `F1` Shortcuts Help**: Press `F1` inside any menu, app, game, or settings screen to open a context-sensitive keyboard shortcuts overlay.
- **Expression Calculator**: Multi-operator expressions with parentheses and operator precedence, powers (`^` / `P`), square & cube roots (`sqrt(...)` via `R`/`S`, `cbrt(...)` via `Q`), decimal and exact rational fraction display (`F2` toggle).
- **Markdown Notes (`.md`)**: Text editor and formatted Markdown preview (`#`, `##`, `-`, `>`, `---`, `**bold**`) with save support (`Ctrl+S` / `F3`) on LittleFS or SD.
- **Dual-Drive File Explorer**: Browse LittleFS (Flash) and FAT32 (SD card), create new `.md` notes (`N` / `F2`), edit text files, play `.mp3` files from SD, delete entries (`Del`), and remount SD (`R`).
- **Bluetooth A2DP MP3 Player**: Streams MP3 files from the SD card to Bluetooth headphones/speakers using synchronous `arduino-libhelix` (`MP3DecoderHelix`).
- **Wi-Fi, NTP & PASV FTP Server**: Connect to Wi-Fi networks, automatically sync Polish local time (`CET-1CEST`) via NTP, and manage files over FTP (port `21`, passive port `50009`) with live Flash/SD drive switching (`<` / `>` / `Tab`).
- **Classic Games**: Brick Breaker, Tetris, and Brick Game Racing (with persistent high score).
- **Settings & Diagnostics**: Bluetooth scanner & pairing manager, Wi-Fi & NTP setup, LCD contrast (`0–63`), manual clock setup, Flash/SD formatting, and an **About** screen with live free heap RAM monitoring.
- **Hardware Suspend**: LCD hardware power-save mode (`setPowerSave(1)`) with instant wake-up on any key press.

---

## Hardware Pinout

### 1. ST7565R 128×64 LCD (`GMG12864-06D Ver.2.2`, 3.3V logic/power only)
| LCD Pin | Pico W Pin | Notes |
| :--- | :--- | :--- |
| `SCL` | `GP18` | Software SPI clock (`U8g2`) |
| `SI` | `GP19` | Software SPI MOSI |
| `CS` | `GP17` | Chip select |
| `RS / DC` | `GP16` | Data / Command |
| `RSE / RST` | `GP20` | Reset |
| `VDD` | `3V3` | 3.3V power |
| `A` (Backlight +) | `3V3` via `47–100Ω` | Current-limiting resistor |
| `K / VSS` | `GND` | Ground |

### 2. Catalex MicroSD v1.0 (FAT32, Hardware `SPI1`)
| SD Module Pin | Pico W Pin | Notes |
| :--- | :--- | :--- |
| `VCC` | `VBUS` (`5V`) | On-board 3.3V regulator on Catalex module |
| `SCK` | `GP10` | `SPI1` SCK |
| `MOSI` | `GP11` | `SPI1` TX |
| `MISO` | `GP12` | `SPI1` RX (internal pull-up enabled) |
| `CS` | `GP13` | Software CS |
| `GND` | `GND` | Ground |

### 3. USB-A 2.4 GHz Keyboard Receiver (`Pico-PIO-USB`)
| USB Pin | Pico W Pin | Notes |
| :--- | :--- | :--- |
| `VCC` | `VBUS` (`5V`) | 5V power for USB receiver |
| `D+` | `GP2` | PIO USB D+ |
| `D-` | `GP3` | PIO USB D- (`GP2 + 1`) |
| `GND` | `GND` | Ground |

---

## Architecture (FreeRTOS SMP)

- **Core 1 (`setup1` / `loop1` in `usb_kbd.cpp`)**: Dedicated to `USBHost.task()` (`Pico-PIO-USB` + `Adafruit_TinyUSB`), pushing decoded keys into a static FreeRTOS `keyQueue`.
- **Core 0**:
  - `CORE0` (`setup` / `loop`): UI rendering (`U8g2`) and state dispatch across modular `src/` screens.
  - `TaskAudioBT` (`bt_audio.cpp`): Owns `SD` audio playback, synchronous `MP3DecoderHelix`, and `A2DPSource` (with a patched FreeRTOS BTstack run-loop avoiding timer alarm storms).
  - `TaskNet` (`net_ftp.cpp`): Owns Wi-Fi STA + RSSI polling, NTP sync, and the passive FTP server.
- **Header Isolation**: `Adafruit_TinyUSB.h` (`usb_kbd.cpp`) and `BluetoothAudio.h` (`bt_audio.cpp`) remain strictly isolated in separate translation units due to conflicting HID macro/enum definitions.
- **Filesystem Locking**: Shared access to `LittleFS` and `SD` (`SDFS`) is synchronized via `fsMutex` (`fsLock()` / `fsUnlock()` in `storage_cfg.cpp`).

---

## Build & Flash

### Required Arduino Configuration
- **Board**: Raspberry Pi Pico W (`rp2040:rp2040:rpipicow`)
- **Flash Size**: `2MB (Sketch: 1MB, FS: 1MB)`
- **Operating System**: `FreeRTOS SMP`
- **IP/Bluetooth Stack**: `IPv4 + Bluetooth`
- **CPU Speed**: `240 MHz (Overclock)`
- **USB Stack**: `Adafruit TinyUSB`

### Required Libraries
- `U8g2`
- `Adafruit TinyUSB Library`
- `Pico PIO USB`
- `arduino-libhelix` (`libhelix`)
- Built-in RP2040 core libraries: `FreeRTOS`, `BluetoothAudio`, `WiFi`, `LittleFS`, `SD` / `SDFS`, `SPI`

### Using `pico.sh` (macOS / Linux with `arduino-cli`)
```bash
# Compile only
./pico.sh build

# Compile and upload to Pico W
./pico.sh flash

# Capture 5 seconds of Serial Monitor logs (115200 baud)
./pico.sh logs
```

---

## Quick Keyboard Reference

| Context | Key | Action |
| :--- | :--- | :--- |
| **Any Screen** | `F1` | Open keyboard shortcuts help overlay |
| **Main Menu** | `Arrows` / `Tab` / `Enter` | Navigate tiles / switch page 1–2 / launch |
| **Calculator** | `0-9 . , + - * / ( ) ^ P R S Q` | Build expression (`^`/`P` power, `R`/`S` `sqrt`, `Q` `cbrt`) |
| **Calculator** | `Enter` / `=` / `F2` / `C` | Evaluate / toggle Fraction vs Decimal / Clear |
| **Notes (`.md`)** | `Ctrl+S` or `F3` / `Tab` or `F2` | Save file / Toggle Markdown preview |
| **File Explorer** | `Enter` / `Tab` / `N` or `F2` / `R` / `Del` | Open or play / Flash↔SD / New `.md` / Remount SD / Delete |
| **FTP Server** | `←` / `→` / `Tab` / `D` / `Enter` / `R` | Switch Flash↔SD / Start-Stop / Remount SD |
| **Music Player** | `↑` / `↓` / `Enter` / `Space` / `S` / `R` | Select / Play / Pause / Stop / Rescan SD |

---

## Project Structure

```text
hollow-star-os-basic/
├── AGENTS.md
├── LICENSE
├── README.md
├── folder_structure.txt
├── pico.sh
├── docs/
│   └── wiring.md
└── hollow_star_os_basic/
    ├── hollow_star_os_basic.ino
    ├── usb_kbd.h / .cpp
    ├── storage_cfg.h / .cpp
    ├── bt_audio.h / .cpp
    ├── net_ftp.h / .cpp
    └── src/
        ├── os/
        │   ├── app_types.h
        │   ├── ui_common.h / .cpp
        │   └── ui_menus.h / .cpp
        ├── apps/
        │   ├── app_temp.h / .cpp
        │   ├── app_calc.h / .cpp
        │   ├── app_notes.h / .cpp
        │   ├── app_files.h / .cpp
        │   ├── app_music.h / .cpp
        │   ├── app_ftp.h / .cpp
        │   └── app_settings.h / .cpp
        └── games/
            ├── game_brick.h / .cpp
            ├── game_tetris.h / .cpp
            └── game_racing.h / .cpp
```

---

## License

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)** — see the [LICENSE](LICENSE) file for details.

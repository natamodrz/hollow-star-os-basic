# Hollow Star OS Basic

A compact, sharp-pixel embedded operating system for the **Raspberry Pi Pico & Raspberry Pi Pico W (RP2040)** running on **FreeRTOS SMP** (Arduino-Pico core v6.4.0), featuring a 128×64 monochrome LCD UI, USB 2.4 GHz keyboard input via PIO, dual **Bluetooth A2DP + 3.5 mm PWM Stereo Headphone Jack** MP3 playback, Wi-Fi + NTP clock sync, authenticated PASV FTP server, Markdown notes creator/editor, dual-drive file explorer, expression calculator, system resource monitor, and classic retro games.

---

## Features

- **Pico & Pico W Support**:
  - **Raspberry Pi Pico W (`hollow-star-os-picow-v1.1.0.uf2`)**: Full feature set including Wi-Fi, NTP clock sync, FTP server, Bluetooth A2DP audio, and 3.5 mm Headphone Jack output.
  - **Raspberry Pi Pico (`hollow-star-os-pico-v1.1.0.uf2`)**: Clean build without CYW43 dependencies — Bluetooth, Wi-Fi, NTP, and FTP UI options are hidden automatically while MP3 playback works over the 3.5 mm Headphone Jack.
- **Audio Player & Headphone Jack Auto-Switch (`BT A2DP` / `PWM Jack`)**:
  - Plugging headphones/speakers into the 3.5 mm jack (`GP8` active-LOW detect) automatically disconnects and disables Bluetooth and routes audio to `PWMAudio` stereo (`GP6` = L, `GP7` = R).
  - Manual output override (`O` key: `AUTO` / `JACK` / `BT`), smooth synchronized `MP3DecoderHelix` playback with `ID3v2` skip, 512-byte aligned BT streaming (`24 KB` A2DP ring buffer, priority-2 audio task), dynamic `0–100%` software volume (`←`/`→` or `-`/`+`), animated equalizer, progress bar, and elapsed timer.
- **Sys Info Monitor**: Live RP2040 CPU temperature, used/total heap RAM (`KB` & `%`), and free/total capacity on both internal Flash (`LittleFS`) and FAT32 SD card.
- **Markdown Notes (`.md`) with Location & File Creator**: Launch Notes to pick drive (`Flash` / `SD`), folder, and filename first, then edit an empty `.md` file (`Ctrl+S` / `F3` save) or toggle live formatted Markdown preview (`Tab` / `F2`).
- **Authenticated PASV FTP Server (Pico W)**: Live Flash/SD drive switching (`←` / `→` / `Tab` / `D`), SD remount (`R`), and configurable FTP username/password (`U` / `P`, default `hollow` / `hollow`).
- **Dual-Drive File Explorer**: Browse Flash (`LittleFS`) and FAT32 (`SD`), create `.md` files (`N` / `F2`), edit text/config files, play `.mp3` files from SD, and delete files (`Del`).
- **Expression Calculator**: Multi-operator math expressions with parentheses, precedence, powers (`^` / `P`), square & cube roots (`sqrt` via `R`/`S`, `cbrt` via `Q`), and decimal/fraction output toggle (`F2`).
- **Custom Sharp-Pixel UI (`U8g2`) & Universal `F1` Help**: Zero rounded corners, boot splash, persistent top status bar (USB keyboard, `HH:MM`, Wi-Fi bars on Pico W, active BT/Headphone icon), and context-sensitive `F1` keyboard shortcuts overlay on every screen.
- **Classic Games & Hardware Suspend**: Brick Breaker, Tetris, Brick Game Racing, and LCD hardware power-save (`setPowerSave(1)`).

---

## Hardware Pinout

| Subsystem | Pins | Notes |
| :--- | :--- | :--- |
| **ST7565R 128×64 LCD** | `SCL=GP18`, `SI=GP19`, `CS=GP17`, `RS=GP16`, `RSE=GP20` | 3.3V logic/power only (`A` via `47–100Ω` to `3V3`) |
| **Catalex MicroSD (`SPI1`)** | `VCC=VBUS (5V)`, `SCK=GP10`, `MOSI=GP11`, `MISO=GP12`, `CS=GP13` | FAT32, internal pull-up on `MISO` |
| **USB 2.4 GHz Keyboard (`PIO`)** | `VCC=VBUS (5V)`, `D+=GP2`, `D-=GP3` | Core 1 `Pico-PIO-USB` Host |
| **3.5 mm Headphone Jack (`PWM`)** | `L=GP6`, `R=GP7`, `Detect=GP8`, `GND=GND` | `62.5 kHz` PWM + RC filter, `GP8` `INPUT_PULLUP` active LOW |

Full wiring diagrams and the passive RC audio filter schematic are in [docs/wiring.md](docs/wiring.md).

---

## Flashable Prebuilt Firmware (`.uf2`)

Prebuilt `.uf2` images are included in [releases/](releases/):
- **Raspberry Pi Pico W**: `releases/hollow-star-os-picow-v1.1.0.uf2`
- **Raspberry Pi Pico**: `releases/hollow-star-os-pico-v1.1.0.uf2`

Hold **BOOTSEL**, plug in the Pico over USB, and copy the matching `.uf2` file onto the `RPI-RP2` drive.

---

## Build & Flash

### Required Arduino Configuration
- **Board**: Raspberry Pi Pico W (`rp2040:rp2040:rpipicow`) or Raspberry Pi Pico (`rp2040:rp2040:rpipico`)
- **Flash Size**: `2MB (Sketch: 1MB, FS: 1MB)`
- **Operating System**: `FreeRTOS SMP`
- **CPU Speed**: `240 MHz (Overclock)`
- **USB Stack**: `Adafruit TinyUSB`
- **IP/Bluetooth Stack**: `IPv4 + Bluetooth` (Pico W) / `IPv4 Only` (Pico)

### Using `pico.sh` (`arduino-cli`)
```bash
# Compile for Pico W (default) or standard Pico
./pico.sh build picow
./pico.sh build pico

# Build both release .uf2 files into releases/
./pico.sh release

# Compile and flash directly
./pico.sh flash picow
./pico.sh flash pico

# Capture 5s of Serial logs (115200 baud)
./pico.sh logs
```

---

## Quick Keyboard Reference

| Context | Key | Action |
| :--- | :--- | :--- |
| **Any Screen** | `F1` | Open keyboard shortcuts help overlay |
| **Main Menu** | `Arrows` / `Tab` / `Enter` | Navigate / Page 1–2 / Launch |
| **Music Player** | `↑` / `↓` / `Enter` / `Space` / `S` | Track / Play / Pause / Stop |
| **Music Player** | `←` / `→` / `-` / `+` / `Tab` / `O` | Volume -/+ / Player↔Playlist / Output (`AUTO`/`JACK`/`BT`) |
| **Notes (`.md`)** | `↑` / `↓` / `←` / `→` / `Enter` / `F3` | Setup Drive, Folder, Name / Edit empty or open existing |
| **Notes Editor** | `Ctrl+S` or `F3` / `Tab` or `F2` | Save `.md` / Toggle Markdown preview |
| **FTP Server (Pico W)** | `U` / `P` / `←` `→` `D` / `Enter` / `R` | Set User / Set Password / Flash↔SD / Start-Stop / Remount SD |
| **Sys Info** | `R` / `Esc` | Refresh Flash & SD space / Menu |

---

## Architecture Notes

- **Header Isolation**: `Adafruit_TinyUSB.h` (`usb_kbd.cpp`) and `BluetoothAudio.h` (`bt_audio.cpp`) stay in strictly isolated translation units.
- **Core Affinity**: Core 1 drives `USBHost.task()`, while Core 0 handles UI (`loop()`), `TaskAudioBT` (priority 2, synchronous `MP3DecoderHelix` -> A2DP SBC or `PWMAudio` DMA), and `TaskNet` (Pico W only). Shared Flash/SD access uses `fsLock()` / `fsUnlock()`.

---

## License

Licensed under the **GNU General Public License v3.0 (GPL-3.0)** — see [LICENSE](LICENSE).

# Hardware Wiring Guide (Raspberry Pi Pico & Pico W)

## 1. ST7565R 128×64 LCD (`GMG12864-06D Ver.2.2`, 3.3V Logic Only)
| LCD Pin | Pico / Pico W Pin | Notes |
| :--- | :--- | :--- |
| `SCL` | `GP18` | Software SPI clock (`U8g2`) |
| `SI` | `GP19` | Software SPI MOSI |
| `CS` | `GP17` | Chip Select |
| `RS / DC` | `GP16` | Data / Command |
| `RSE / RST` | `GP20` | Reset |
| `VDD` | `3V3` | 3.3V power (do **not** connect to 5V) |
| `A` (Backlight +) | `3V3` via `47–100Ω` | Current-limiting resistor |
| `K / VSS` | `GND` | Ground |

## 2. Catalex MicroSD v1.0 (FAT32, Hardware `SPI1`)
| SD Module Pin | Pico / Pico W Pin | Notes |
| :--- | :--- | :--- |
| `VCC` | `VBUS` (`5V`) | Uses Catalex on-board 3.3V LDO |
| `SCK` | `GP10` | `SPI1` SCK |
| `MOSI` | `GP11` | `SPI1` TX |
| `MISO` | `GP12` | `SPI1` RX (internal pull-up enabled in firmware) |
| `CS` | `GP13` | Software CS |
| `GND` | `GND` | Ground |

## 3. USB-A 2.4 GHz Keyboard Receiver (`Pico-PIO-USB`)
| USB Pin | Pico / Pico W Pin | Notes |
| :--- | :--- | :--- |
| `VCC` | `VBUS` (`5V`) | 5V receiver power |
| `D+` | `GP2` | PIO USB D+ |
| `D-` | `GP3` | PIO USB D- (`GP2 + 1`) |
| `GND` | `GND` | Ground |

## 4. 3.5 mm Headphone / Speaker Jack (`PWMAudio` Stereo + Jack Detect)
The RP2040 has no internal analog DAC, so stereo audio is output at `62.5 kHz` PWM carrier on `GP6` (Left) and `GP7` (Right) with a simple RC low-pass + DC-blocking capacitor filter, plus an active-LOW jack insertion switch on `GP8`.

| Signal | Pico / Pico W Pin | 3.5 mm Jack Contact | Component / Notes |
| :--- | :--- | :--- | :--- |
| Audio Left | `GP6` (Pin 9) | `Tip (L)` | Via RC filter below |
| Audio Right | `GP7` (Pin 10) | `Ring (R)` | Via RC filter below |
| Jack Detect | `GP8` (Pin 11) | `Detect Switch` | Internal `INPUT_PULLUP`; closes to `GND` when plug is inserted (`LOW` disables Bluetooth on Pico W) |
| Audio Ground | `GND` (Pin 8 / 13) | `Sleeve (GND)` + Switch Common | Common ground |

### Simple Passive RC Filter per Channel (`L` and `R`)
```text
GP6 (L) ──[ 150Ω–220Ω ]──┬──||──[ + 10µF–47µF - ]──> Jack Tip (L)
                         │
                     [ 100nF ]
                         │
                        GND

GP7 (R) ──[ 150Ω–220Ω ]──┬──||──[ + 10µF–47µF - ]──> Jack Ring (R)
                         │
                     [ 100nF ]
                         │
                        GND
```
> Note: If using a basic 3-pin 3.5 mm socket without a mechanical detect switch, switch the audio output mode inside the **Music** player using the `O` key (`JACK` forced mode).

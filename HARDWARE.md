# Hardware Reference

> **Reference document** — compiled from product listings, Waveshare documentation, community research, and confirmed working configurations.

---

## Board Roles

| Role | Board | Project folder |
|------|-------|---------------|
| **Master** | diymore ESP32-WROOM-32 with 1.9" LCD | `master/` |
| **Channel** | AITRIP 30-pin CP2102 ESP32-WROOM-32 (no display) | `channel/` |

The master board receives audio from N channel boards via I2S/I2C, mixes it, and forwards to a Bluetooth speaker via A2DP source. Each channel board receives audio via Bluetooth A2DP sink, applies its own volume (rotary encoder), and sends PCM frames to the master.

---

## Channel Board — AITRIP 30-pin CP2102 ESP32-WROOM-32

Standard ESP32-WROOM-32 module on a minimal 30-pin devkit. No display. USB-C. CP2102 USB-serial chip.

| Parameter | Value |
|-----------|-------|
| Module | ESP32-WROOM-32 |
| USB Interface | Type-C |
| USB-Serial Chip | CP2102 |
| Working Voltage | 3.3V / USB 5V |
| Flash | 4MB |
| WiFi | 2.4GHz 802.11 b/g/n |
| Bluetooth | 4.2 BR/EDR + BLE |

### Pinout — 30-pin devkit (standard layout)

#### Left side (top to bottom)

| Pin | GPIO | Notes |
|-----|------|-------|
| 3V3 | — | 3.3V out |
| EN | — | Reset |
| VP | GPIO36 | Input only |
| VN | GPIO39 | Input only |
| D34 | GPIO34 | Input only |
| D35 | GPIO35 | Input only |
| D32 | GPIO32 | ADC1 |
| D33 | GPIO33 | ADC1 |
| D25 | GPIO25 | DAC1 |
| D26 | GPIO26 | DAC2 |
| D27 | GPIO27 | |
| D14 | GPIO14 | Strapping — boot |
| D12 | GPIO12 | Strapping — flash voltage, avoid |
| GND | — | |
| D13 | GPIO13 | |

#### Right side (top to bottom)

| Pin | GPIO | Notes |
|-----|------|-------|
| VIN | — | 5V in |
| GND | — | |
| D15 | GPIO15 | Strapping |
| D2 | GPIO2 | Strapping, ADC2 |
| D4 | GPIO4 | ADC2 |
| RX2 | GPIO16 | UART2 RX |
| TX2 | GPIO17 | UART2 TX |
| D5 | GPIO5 | Strapping, SPI CS |
| D18 | GPIO18 | SPI SCK |
| D19 | GPIO19 | SPI MISO |
| D21 | GPIO21 | I2C SDA |
| RX0 | GPIO3 | UART0 RX |
| TX0 | GPIO1 | UART0 TX |
| D22 | GPIO22 | I2C SCL |
| D23 | GPIO23 | SPI MOSI |

### Planned channel board wiring

| KY-040 Pin | GPIO | Notes |
|-----------|------|-------|
| CLK | GPIO33 | Encoder A |
| DT | GPIO32 | Encoder B |
| SW | GPIO27 | Push button |
| VCC | 3.3V | |
| GND | GND | |

I2C to master (shared bus, each channel board is a slave at a unique address):

| Signal | GPIO |
|--------|------|
| SDA | GPIO21 |
| SCL | GPIO22 |

---

## Attached Peripherals — Master board only

### RUNCCI-YUN KY-040 Rotary Encoder

Incremental rotary encoder with push button. Has onboard pull-up resistors on all signal lines — no internal pull-ups required in firmware.

| KY-040 Pin | Signal | GPIO | Board Label (Left Header) |
|-----------|--------|------|--------------------------|
| VCC | Power | 3.3V | Right header, bottom pin |
| GND | Ground | GND | Left header, bottom pin |
| CLK | Encoder A | GPIO33 | CH8 (ADC5) |
| DT | Encoder B | GPIO27 | CH7 (ADC17) |
| SW | Push button (active-low) | GPIO14 | CH6 (ADC16) |

**How it works:** CLK and DT produce quadrature signals 90° out of phase. Interrupt on CLK; read DT to determine direction — states differ = clockwise, states match = counter-clockwise. SW goes LOW when the knob is pressed.

---

## ESP32 Board — diymore ESP32-WROOM-32 with 1.9" LCD Display

---

## Overview

The diymore ESP32 board integrates the ESP32-WROOM-32 module with a 1.9-inch ST7789V2 LCD screen, combining full ESP32 functionality with an onboard colour display. The LCD driver code is based on Waveshare's standard LCD library (confirmed from product documentation showing `LCD_Driver.cpp`, `WIFI_Driver.c` etc., authored by the Waveshare team, dated 2023-11-22). Suitable for beginners and professionals building IoT projects.

---

## Hardware Specifications

| Parameter | Value |
|-----------|-------|
| Module | ESP32-WROOM-32 |
| USB Interface | Type-C |
| Working Voltage | USB 5V |
| USB-Serial Chip | CH340 |
| LCD Size | 1.9 inches |
| LCD Resolution | 170 × 320 pixels |
| LCD Driver IC | ST7789V2 |
| LCD Colour Depth | 262K colours (RGB565) |
| LCD Interface | 4-wire SPI (write-only, no MISO) |
| SPI Flash | 32Mbit default |
| Serial Speed | 115200 bps |
| WiFi | 2.4GHz, 802.11 b/g/n, 2412–2484 MHz |
| Bluetooth | 4.2 BR/EDR and BLE |
| Antenna | Onboard PCB, 2dBi gain |

---

## Supported Interfaces

UART, SPI, SDIO, I2C, PWM, I2S, IR, ADC, DAC

---

## Pinout — Header Pins

### Left Side (top to bottom)

| Outer Label | Inner Label | GPIO |
|-------------|-------------|------|
| — | EN | RESET |
| — | ADC0 | GPIO36 |
| — | ADC3 | GPIO39 |
| — | ADC6 | GPIO34 |
| — | ADC7 | GPIO35 |
| CH9 | ADC4 | GPIO32 |
| CH8 | ADC5 | GPIO33 |
| DAC1 | ADC18 | GPIO25 |
| DAC2 | ADC19 | GPIO26 |
| CH7 | ADC17 | GPIO27 |
| CH6 | ADC16 | GPIO14 |
| CH5 | ADC15 | GPIO12 |
| CH4 | ADC14 | GPIO13 |
| — | — | GND |
| — | — | VIN (5V) |

### Right Side (top to bottom)

| GPIO | Function | Outer Label |
|------|----------|-------------|
| GPIO23 | MOSI (SPI) | — |
| GPIO22 | I2C SCL | — |
| GPIO1 | UART0 TX | — |
| GPIO3 | UART0 RX | — |
| GPIO21 | I2C SDA | — |
| GPIO19 | MISO (SPI) | — |
| GPIO18 | SCK (SPI) | — |
| GPIO5 | SS/CS (SPI) | — |
| GPIO17 | UART2 TX | — |
| GPIO16 | UART2 RX | — |
| GPIO4 | ADC10 | CH0 |
| GPIO2 | ADC12 | CH2 |
| GPIO15 | ADC13 | CH3 |
| — | GND | — |
| — | 3.3V | — |

### Onboard Buttons

| Button | Function |
|--------|----------|
| BOOT | Hold during power-on/reset to enter firmware download mode |
| EN | Hardware reset |

---

## Internal LCD Wiring — Confirmed ✅

The onboard ST7789V2 display is wired to the ESP32 internally via dedicated GPIOs. These are **separate from and independent of the broken-out SPI header pins**. Confirmed from the ideaspark board (identical hardware design) official documentation, and cross-referenced against multiple working community configurations.

| LCD Signal | GPIO | Notes |
|-----------|------|-------|
| MOSI | GPIO23 | Shared with header — do not use header GPIO23 for other SPI devices simultaneously |
| SCLK | GPIO18 | Shared with header |
| CS | GPIO15 | Strapping pin — dedicated to LCD |
| DC (Data/Command) | GPIO2 | Strapping pin — see WiFi caveat below |
| RST | GPIO4 | Dedicated to LCD reset |
| BL (Backlight) | GPIO32 | PWM-capable — can be used to dim the display |

> **Strapping pin note:** GPIO2 and GPIO15 are ESP32 strapping pins. The board manages their boot states correctly in normal operation. Do not add strong external pull-resistors to these pins.

> **WiFi + GPIO2 caveat:** GPIO2 is on ADC2, which shares resources with the WiFi radio. The DC signal is output-only so display operation is unaffected, but do not attempt to use GPIO2 for analogue reads when WiFi is active.

---

## Backlight Dimming (PWM — ESP-IDF)

GPIO32 is PWM-capable via the LEDC peripheral:

```c
#include "driver/ledc.h"

ledc_timer_config_t timer = {
    .speed_mode      = LEDC_LOW_SPEED_MODE,
    .timer_num       = LEDC_TIMER_0,
    .duty_resolution = LEDC_TIMER_8_BIT,
    .freq_hz         = 5000,
    .clk_cfg         = LEDC_AUTO_CLK,
};
ledc_timer_config(&timer);

ledc_channel_config_t ch = {
    .gpio_num   = 32,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel    = LEDC_CHANNEL_0,
    .timer_sel  = LEDC_TIMER_0,
    .duty       = 200,   // 0 = off, 255 = full brightness
    .hpoint     = 0,
};
ledc_channel_config(&ch);
```

---

## GPIO Usage Guide

### Pins used internally by the LCD — do not reuse

| GPIO | LCD Function |
|------|-------------|
| GPIO2 | DC (Data/Command) |
| GPIO4 | RST |
| GPIO15 | CS |
| GPIO18 | SCLK — shared with header |
| GPIO23 | MOSI — shared with header |
| GPIO32 | Backlight |

### Free to use for your project

| GPIO | Notes |
|------|-------|
| GPIO13 | General purpose |
| GPIO21 | Default I2C SDA — free, broken out on header |
| GPIO22 | Default I2C SCL — free, broken out on header |
| GPIO25 | Also DAC1 |
| GPIO26 | Also DAC2 |
| GPIO34, 35, 36, 39 | **Input only** — no pull-up/down, no output |

### In use by peripherals

| GPIO | Peripheral | Signal |
|------|-----------|--------|
| GPIO14 | KY-040 rotary encoder | SW (push button) |
| GPIO27 | KY-040 rotary encoder | DT (encoder B) |
| GPIO33 | KY-040 rotary encoder | CLK (encoder A) |

### Strapping pins — handle with care

| GPIO | State at boot | In normal use |
|------|---------------|---------------|
| GPIO0 | LOW = flash download mode | Fine after boot |
| GPIO2 | Must be LOW or floating | Used for LCD DC |
| GPIO5 | Must be HIGH | Fine after boot |
| GPIO12 | Controls flash voltage — leave unconnected | Avoid |
| GPIO15 | Must be HIGH | Used for LCD CS |

### ADC2 pins — avoid for analogue when WiFi is active

GPIO2, 4, 12, 13, 14, 15, 25, 26, 27 are all ADC2 channels. ADC2 is unreliable for analogue reads while WiFi is running. Use **ADC1 pins** (GPIO32–39) for any analogue sensing in WiFi projects.

---

## ESP-IDF Components

All built into ESP-IDF — no external dependencies needed for display or core peripherals:

| Component | Header | Purpose |
|-----------|--------|---------|
| SPI Master | `driver/spi_master.h` | SPI bus and device management |
| GPIO | `driver/gpio.h` | Pin control |
| LEDC | `driver/ledc.h` | PWM for backlight dimming |
| WiFi | `esp_wifi.h` | 2.4GHz WiFi |
| HTTP Server | `esp_http_server.h` | Embedded web server |
| MQTT | `mqtt_client.h` | MQTT (via `idf_component.yml`) |

---

## Development Environment Setup

### ESP-IDF (VS Code)

Use the same toolchain layout as other ESP-IDF projects. `flash.ps1` at project root sets up the environment and wraps `idf.py`:

```powershell
.\flash.ps1 build
.\flash.ps1 -p COM3 build flash monitor
```

Top-level `CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(your-project)
```

`main/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")
```

> **Note:** ESP-IDF defines `SPI_HOST` as `SPI1_HOST`. Use `SPI2_HOST` directly for the LCD. See Known Issues.

### Firmware Download Mode

If upload fails: hold **BOOT**, press **EN** to reset, release **BOOT** once the upload starts. Alternatively, hold BOOT while plugging in the USB cable.

---

## Official Sample Code

The official diymore demo sketch is called **`WIFI_AP_LCD`** and consists of these files (all based on Waveshare's standard library structure):

| File | Purpose |
|------|---------|
| `LCD_Driver.cpp` / `.h` | ST7789 SPI driver |
| `GUI_Paint.cpp` / `.h` | Drawing primitives (lines, rectangles, text, images) |
| `WIFI_Driver.cpp` / `.h` | WiFi access point setup |
| `font12.cpp` etc. | Bitmap font data |

After flashing, the board creates a WiFi access point (`HW-656 Server` at `192.168.4.1`). Connect to it and browse to that address to control display output (monochrome or image mode).

---

## Known Issues & Caveats

| Issue | Detail |
|-------|--------|
| **GPIO2 / WiFi** | GPIO2 (DC pin) is ADC2; ADC2 shares resources with WiFi. No display issue, but don't use GPIO2 for ADC reads when WiFi is active. |
| **Strapping pins** | GPIO2 and GPIO15 are boot strapping pins used by the LCD. Do not add external pull-ups/downs. |
| **GPIO12** | Controls ESP32 flash voltage. Never pull HIGH at boot — can prevent startup. Avoid using it. |
| **Upload failures** | Drop upload speed from 921600 to 460800 if experiencing timeouts. |
| **USB-C to USB-C** | Some users report USB-C to USB-C cables not powering the board — use USB-A to USB-C. |
| **ADC2 with WiFi** | All ADC2 pins (GPIO2, 4, 12–15, 25–27) are unreliable for analogue reads when WiFi is active. |
| **SPI bus sharing** | GPIO18 (SCLK) and GPIO23 (MOSI) are shared between the LCD and the external SPI header. Adding another SPI device on these pins will work electrically, but requires careful CS management. |
| **Board type** | Must use "ESP32 Wrover Module" not generic esp32dev — wrong partition/memory layout otherwise. |
| **ST7789V2 column offset** | The frame buffer is 240 columns wide but the panel is only 170. The visible area starts at column 35. Any SPI driver must add a column offset of 35 to all CASET commands, otherwise drawing is misaligned and old GRAM content shows at the edges. Row offset is 0. |
| **SPI clock limit (ESP-IDF)** | ESP-IDF rejects clock speeds above ~26.6 MHz for SPI devices on non-IOMUX pins in full-duplex mode. Use 26 MHz. Also set `SPI_DEVICE_HALFDUPLEX` since the display is write-only — this removes the frequency restriction entirely. |
| **SPI_HOST name conflict** | ESP-IDF defines `SPI_HOST` as `SPI1_HOST` in `hal/spi_types.h`. Do not redefine it. Use `SPI2_HOST` directly or name your macro something else (e.g. `LCD_SPI_HOST`). |

---

## Typical Applications

- WiFi-connected IoT sensors with live display readout
- Smart home status dashboards
- Mesh networking nodes with status screen
- BLE peripheral devices
- WiFi access point with web configuration UI
- Data logging with on-device display
- General Arduino-compatible prototyping

---

## Resources

| Resource | Link |
|----------|------|
| diymore official store | https://www.diymore.cc |
| Amazon UK listing | https://www.amazon.co.uk/dp/B0DK399Y9Q |
| ESP32 Arduino Core docs | https://docs.espressif.com/projects/arduino-esp32/en/latest/ |
| ESP-IDF GPIO reference | https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html |
| TFT_eSPI library | https://github.com/Bodmer/TFT_eSPI |
| LovyanGFX library | https://github.com/lovyan03/LovyanGFX |
| LVGL | https://lvgl.io |
| Waveshare 1.9" LCD wiki | https://www.waveshare.com/wiki/1.9inch_LCD_Module |
| ST7789 170×320 TFT_eSPI config | https://github.com/mboehmerm/IPS-Display-ST7789-170x320 |
| ideaspark example code (same HW) | https://github.com/jwkunz/ideaspark_esp32_114LCD_example |
| Board manager JSON URL | https://dl.espressif.com/dl/package_esp32_index.json |
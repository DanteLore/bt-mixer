# bt-mixer — Project Context

## What this project is

A multi-channel Bluetooth audio mixer built on ESP32 hardware. The system consists of:

- **One master board** — receives audio from N channel boards via SPI, mixes it, and streams to a Bluetooth speaker via A2DP source
- **N channel boards** — each receives audio via Bluetooth A2DP sink, applies per-channel volume controlled by a rotary encoder, and sends PCM to the master over SPI

## Repository layout

```
bt-mixer/
├── master/              ← firmware for the master board (diymore ESP32 + LCD)
│   ├── CMakeLists.txt
│   ├── flash.ps1
│   ├── sdkconfig.defaults
│   └── main/
│       ├── main.c       ← display mode / scan mode state machine
│       ├── bt_audio.c/h ← A2DP source, test tone, gain ramping
│       ├── encoder.c/h  ← quadrature ISR, acceleration
│       ├── scan_mode.c/h← BT device discovery UI
│       ├── st7789.c/h   ← LCD driver
│       └── font8x8_basic.h
├── channel/             ← firmware for channel boards (AITRIP 30-pin ESP32)
│   ├── CMakeLists.txt
│   ├── flash.ps1
│   ├── sdkconfig.defaults
│   └── main/
│       ├── main.c       ← encoder loop, NVS volume, SPI slave (TODO)
│       ├── bt_audio.c/h ← A2DP sink, volume, level meter
│       └── encoder.c/h  ← same quadrature logic as master
├── HARDWARE.md          ← pinouts and wiring for all boards
├── README.md            ← this file
└── .gitignore
```

## Hardware

See HARDWARE.md for full pinouts.

**Master board:** diymore ESP32-WROOM-32 with integrated 1.9" ST7789V2 LCD (170×320, landscape).
- LCD internal wiring: GPIO2 (DC), GPIO4 (RST), GPIO15 (CS), GPIO18 (SCLK), GPIO23 (MOSI), GPIO32 (BL)
- Rotary encoder: GPIO33 (CLK), GPIO27 (DT), GPIO14 (SW)

**Channel board:** AITRIP 30-pin CP2102 ESP32-WROOM-32. No display.
- SPI to master: GPIO5 (CS), GPIO18 (SCLK), GPIO19 (MISO), GPIO23 (MOSI)
- Rotary encoder: GPIO33 (CLK), GPIO32 (DT), GPIO27 (SW)

## Toolchain

- **ESP-IDF:** v5.5.3
- **IDF_PATH:** `C:\esp\v5.5.3\esp-idf`
- **Tools:** `C:\Espressif\tools`
- **Build from project subdirectory:** `cd master` then `.\flash.ps1 build`
- Language: **C only, not Arduino**

## Key architectural decisions

### Bluedroid A2DP sink + source are mutually exclusive
The ESP32 Bluedroid stack cannot run A2DP sink and source simultaneously. Master = source only (→ speaker). Channel = sink only (← phone/device). Audio travels between boards over SPI as raw PCM.

### Volume control — both boards
- Logarithmic dB curve: `dB = -40 + v * 0.468`, mapping 0–100 to -40dB–+6.8dB
- Gain ramping: `GAIN_STEP = 10` per sample at 44100Hz (~5ms ramp), prevents pops
- Volume persisted to NVS; saved 2 seconds after knob stops (flash wear protection)

### Encoder — both boards
- ANYEDGE interrupts on both CLK and DT pins
- Quadrature state machine lookup table — invalid transitions (bounce) silently ignored
- Accumulates within each detent, emits one step only on return to rest (0b11)
- Acceleration: step size 1/2/4/8 based on CPU cycle counter between detents

### Display — master only
- ST7789V2, landscape 320×170 px, RGB565, 8×8px bitmap font
- Differential bar drawing — only changed slice repainted, no flicker
- 20Hz draw rate; VOL label + conn state on row 1; SIG meter below

### Bluetooth — master
- Device name: `bt-mixer`, PIN: `0000` (legacy pairing)
- Scan mode: CoD filter (Audio/Video major class 0x04 only — filters PCs etc.)
- Rescan every 15s; `<< BACK` at bottom of list
- BDA + device name persisted to NVS; auto-connects on boot
- Auto-reconnect every 7s when disconnected
- Pending connect: defers `source_connect` until `DISCONNECTED` callback (avoids stack errors)

### Bluetooth — channel
- Device name: `bt-mixer-ch1`, `bt-mixer-ch2` etc. (set via `CHANNEL_ID` in main.c)
- PIN: `0000`
- Discoverable and connectable; phone/device connects to it like a BT speaker

### Audio — master (current)
- Test tone: 440Hz sine wave in `source_data_cb` — to be replaced with SPI mix
- Level meter: peak of post-gain samples → dBFS → 0–100 bar

### SPI — channel → master (TODO)
- Master polls each channel board in turn using a dedicated CS pin per board
- Channel SPI slave pre-fills a DMA buffer each A2DP callback; master reads it on demand
- Each transaction carries a small header (volume, conn state) followed by a PCM frame
- One shared bus (SPI3_HOST on master): SCLK, MOSI, MISO + one CS pin per channel
- Channel board uses SPI slave driver with DMA; master uses SPI master driver
- Not yet implemented — stub in channel/main/main.c

## What to work on next

1. Implement SPI slave on channel boards (respond to master poll with PCM frame + volume)
2. Implement SPI master on master board (poll N channels, mix PCM)
3. Replace master sine wave with mixed channel audio
4. Consider display extensions: per-channel level bars on master LCD

## Coding style

- C only, no Arduino
- No defensive guard code unless at system boundaries
- No doc comments on non-library functions
- Short functions, self-documenting names
- Comments only for non-obvious logic
- British English in comments, US English in identifiers
- No premature abstractions

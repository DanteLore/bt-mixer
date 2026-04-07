# bt-mixer — Project Context for Claude

## What this project is

A multi-channel Bluetooth audio mixer built on ESP32 hardware. The system consists of:

- **One master board** — receives audio from N channel boards, mixes it, and streams to a Bluetooth speaker via A2DP source
- **N channel boards** — each receives audio via Bluetooth A2DP sink, applies per-channel volume, and sends PCM to the master

## Repository layout

```
bt-mixer/
├── master/          ← firmware for the master board (diymore ESP32 + LCD)
│   └── main/
├── channel/         ← firmware for channel boards (AITRIP 30-pin ESP32)
│   └── main/
├── HARDWARE.md      ← pinouts and wiring for all boards
├── CLAUDE.md        ← this file
└── flash.ps1        ← build/flash wrapper (ESP-IDF v5.5.3)
```

> **Note:** The repo is currently flat (all master code in `main/`). Restructuring into `master/` and `channel/` subdirectories is the next planned step.

## Hardware

See HARDWARE.md for full details.

**Master board:** diymore ESP32-WROOM-32 with integrated 1.9" ST7789V2 LCD (170×320, landscape). LCD is wired internally — GPIO2 (DC), GPIO4 (RST), GPIO15 (CS), GPIO18 (SCLK), GPIO23 (MOSI), GPIO32 (BL). Rotary encoder on GPIO33 (CLK), GPIO27 (DT), GPIO14 (SW).

**Channel board:** AITRIP 30-pin CP2102 ESP32-WROOM-32. No display. I2C to master on GPIO21 (SDA) / GPIO22 (SCL). Rotary encoder on GPIO33/32/27.

## Toolchain

- **ESP-IDF:** v5.5.3
- **IDF_PATH:** `C:\esp\v5.5.3\esp-idf`
- **Tools:** `C:\Espressif\tools`
- **Build:** `.\flash.ps1 build` / `.\flash.ps1 -p COM3 build flash monitor`
- Language: **C only, not Arduino**

## Key architectural decisions

### Bluedroid A2DP sink + source are mutually exclusive
The ESP32 Bluedroid stack cannot run A2DP sink and source at the same time. This is why the master board is source-only (sends to speaker) and the channel boards are sink-only (receive from phone/device). Audio is transferred between boards over I2C, not Bluetooth.

### Volume control
- Per-channel volume: controlled by the rotary encoder on each channel board, applied to PCM samples before sending to master
- Master output volume: controlled by the rotary encoder on the master board, applied before A2DP transmission
- Both use a logarithmic dB curve: `dB = -40 + v * 0.468`, mapping 0–100 to -40dB–+6.8dB
- Gain ramping: `GAIN_STEP = 10` per sample at 44100Hz (~5ms), prevents pops on fast changes
- Volume persisted to NVS; saved 2 seconds after knob stops (flash wear protection)

### Encoder
- ANYEDGE interrupts on both CLK and DT
- Quadrature state machine with lookup table — invalid transitions (bounce) return 0
- Accumulates transitions within each detent, emits one step only when encoder returns to rest state (0b11) — eliminates ×4 counting
- Acceleration: step size 1/2/4/8 based on CPU cycle timer between detents
- ~5ms / 15ms / ... thresholds — tweak in encoder.c if too sensitive

### Display (master only)
- ST7789V2, landscape, 320×170 px, RGB565
- Differential bar drawing — only the changed slice is repainted, prevents flicker
- 20Hz draw rate, gated on value changes
- VOL label + conn state on row 1 (y=8); SIG bar at y=48/64; font is 8×8px

### Bluetooth (master)
- Device name: `bt-mixer`
- PIN: `0000` (legacy pairing, no SSP)
- Scan mode: entered by pressing encoder button
  - CoD filter: Audio/Video major class only (0x04) — filters out PCs etc.
  - Rescan every 15s automatically
  - `<< BACK` as last row; scroll with encoder, select with button press
- BDA and device name persisted to NVS; auto-connects on boot
- Auto-reconnect: retries every 7s when disconnected
- Pending connect: if already connected when a new connect is requested, suspends + disconnects cleanly first, then connects in the DISCONNECTED callback

### Audio (master, current)
- Test tone: 440Hz sine wave generated in `source_data_cb`
- When channel boards are integrated: mix N × PCM streams from I2C, replace sine generator
- Level meter: peak of post-gain samples, mapped to dBFS (-40dBFS → 0%, 0dBFS → 100%), fast attack / slow decay (7/8 decay per callback)

### I2C protocol (planned — channel → master)
- Master polls each channel board at a fixed interval
- Each channel board responds with a fixed-size PCM frame + current volume
- I2C addresses: one per channel, configured at compile time or via hardware strapping
- Not yet implemented

## What to work on next

1. Restructure repo into `master/` and `channel/` directories with separate CMakeLists
2. Implement channel board firmware:
   - A2DP sink (receive from phone)
   - Rotary encoder volume control
   - I2C slave: respond to master poll with PCM frame
3. Update master firmware:
   - I2C master: poll N channel boards
   - Replace sine wave with mixed PCM from channel boards
   - Display channel count / per-channel levels (future)

## Coding style

- C only, no Arduino
- No defensive guard code unless at system boundaries
- No doc comments on non-library functions
- Short functions, self-documenting names
- Comments only for non-obvious logic
- British English in comments, US English in identifiers
- No premature abstractions

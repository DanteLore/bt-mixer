#pragma once

#include <stdint.h>

// Initialise GAP discovery callback. Call once after bt_audio_init().
void scan_mode_init(void);

// Enter scan mode — clears device list, starts discovery, takes over display.
void scan_mode_enter(void);

// Tick function — call from main loop while in scan mode.
// Returns:  0 = still scanning/selecting
//           1 = device selected (call bt_audio_connect with scan_mode_get_bda())
//          -1 = user pressed Back
int scan_mode_tick(int encoder_delta, int button_pressed);

// Returns the BDA of the last selected device.
uint8_t *scan_mode_get_bda(void);

// Returns the name of the last selected device (empty string if none).
const char *scan_mode_get_name(void);

// Restores the device name from NVS on startup.
void scan_mode_set_name(const char *name);

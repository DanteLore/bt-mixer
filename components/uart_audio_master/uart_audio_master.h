#pragma once

#include <stdint.h>

// Initialise UART audio master for n_channels channel boards.
// Starts a background task receiving frames on UART2 RX (GPIO16).
void uart_audio_master_init(int n_channels);

// Source callback: fill dst with n_stereo_samples stereo pairs from the ring.
// Pass directly to bt_audio_set_source().
void uart_audio_master_get_samples(int16_t *dst, int n_stereo_samples);

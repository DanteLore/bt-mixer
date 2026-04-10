#pragma once

#include <stdint.h>

// Initialise SPI3 master and start the polling task.
// n_channels: number of channel boards wired up (max 3).
void spi_audio_master_init(int n_channels);

// Fill dst with n_stereo_samples of mixed audio from all channels.
// Outputs silence on ring-buffer underrun.
// Signature matches bt_audio_source_cb_t — pass directly to bt_audio_set_source().
void spi_audio_master_get_samples(int16_t *dst, int n_stereo_samples);

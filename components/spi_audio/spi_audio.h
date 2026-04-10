#pragma once

#include <stdint.h>

#define SPI_AUDIO_FRAME_SAMPLES  128

#define SPI_AUDIO_MAGIC  0xAB12CD34u

typedef struct {
    uint32_t magic;       // always SPI_AUDIO_MAGIC
    uint8_t  volume;      // channel volume 0-100
    uint8_t  conn_state;  // bt_conn_state_t cast to uint8_t
    uint16_t seq;         // wrapping sequence number
    int16_t  pcm[SPI_AUDIO_FRAME_SAMPLES * 2];  // gain-adjusted stereo, interleaved L/R
    uint32_t crc;         // CRC32 over all preceding bytes (magic..pcm)
} spi_audio_frame_t;
// Total: 8 + 512 + 4 = 524 bytes

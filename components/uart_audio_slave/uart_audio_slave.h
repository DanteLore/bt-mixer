#pragma once

// Initialise the UART audio slave.
// Registers a bt_audio frame callback and streams one spi_audio_frame_t per
// 128-sample audio frame over UART2 TX (GPIO17) at 2 Mbaud.
void uart_audio_slave_init(void);

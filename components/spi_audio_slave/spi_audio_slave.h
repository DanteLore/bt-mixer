#pragma once

// Initialise SPI2 slave and start the transmit task.
// Call once after bt_audio_init().
void spi_audio_slave_init(void);

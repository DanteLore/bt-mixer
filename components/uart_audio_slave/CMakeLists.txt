idf_component_register(
    SRCS "uart_audio_slave.c"
    INCLUDE_DIRS "."
    REQUIRES spi_audio bt_audio_sink esp_driver_uart esp_timer esp_rom
)

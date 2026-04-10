idf_component_register(
    SRCS "uart_audio_master.c"
    INCLUDE_DIRS "."
    REQUIRES spi_audio esp_driver_uart freertos esp_timer esp_rom
)

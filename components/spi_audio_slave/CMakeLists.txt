idf_component_register(
    SRCS "spi_audio_slave.c"
    INCLUDE_DIRS "."
    REQUIRES spi_audio bt_audio_sink esp_driver_spi esp_driver_gpio freertos
)

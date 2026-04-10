idf_component_register(
    SRCS "spi_audio_master.c"
    INCLUDE_DIRS "."
    REQUIRES spi_audio esp_driver_spi esp_driver_gpio freertos esp_timer
)

idf_component_register(
    SRCS "main.c" "st7789.c" "encoder.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_driver_spi esp_driver_gpio esp_timer
)

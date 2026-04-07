idf_component_register(
    SRCS "main.c" "st7789.c" "encoder.c" "bt_audio.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_driver_spi esp_driver_gpio esp_timer bt
)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)

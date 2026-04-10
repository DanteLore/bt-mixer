idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_timer bt_audio_sink encoder uart_audio_slave
)

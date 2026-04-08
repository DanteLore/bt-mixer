idf_component_register(
    SRCS "scan_mode.c"
    INCLUDE_DIRS "."
    REQUIRES st7789 bt_audio_source bt esp_timer
)

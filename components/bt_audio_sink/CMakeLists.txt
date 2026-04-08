idf_component_register(
    SRCS "bt_audio.c"
    INCLUDE_DIRS "."
    REQUIRES bt
)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)

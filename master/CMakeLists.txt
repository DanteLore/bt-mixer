cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "../components")
set(COMPONENTS main bt_audio_source encoder scan_mode st7789 spi_audio uart_audio_master)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bt-mixer)

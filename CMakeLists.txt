cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/components/vbot_config"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/vbot_wifi"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/vbot_webserver"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/vbot_ws"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/vbot_audio"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/vbot_display"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/vbot_led"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/vbot_buttons"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_vbot)

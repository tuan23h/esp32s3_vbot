idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        vbot_config vbot_wifi vbot_webserver
        vbot_audio vbot_display vbot_led
        vbot_buttons vbot_ws
        nvs_flash log freertos esp_event json
)

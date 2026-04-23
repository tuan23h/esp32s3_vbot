idf_component_register(
    SRCS "vbot_ws.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_websocket_client log freertos json
        vbot_config vbot_audio esp_heap_caps
)

idf_component_register(
    SRCS "vbot_audio.c"
    INCLUDE_DIRS "."
    REQUIRES driver log freertos vbot_config esp_heap_caps
)

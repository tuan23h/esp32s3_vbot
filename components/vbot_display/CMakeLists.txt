idf_component_register(
    SRCS "vbot_display.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver log freertos
        esp_lcd esp_timer
        vbot_config esp_heap_caps
)

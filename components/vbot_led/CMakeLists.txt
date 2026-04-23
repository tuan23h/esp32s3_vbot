idf_component_register(
    SRCS "vbot_led.c"
    INCLUDE_DIRS "."
    REQUIRES led_strip log freertos vbot_config
)

idf_component_register(
    SRCS "vbot_buttons.c"
    INCLUDE_DIRS "."
    REQUIRES driver log freertos vbot_config
)

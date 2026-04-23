idf_component_register(
    SRCS "vbot_wifi.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif log vbot_config
)

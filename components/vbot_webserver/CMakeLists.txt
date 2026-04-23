idf_component_register(
    SRCS "vbot_webserver.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_http_server esp_wifi esp_netif esp_timer
        esp_chip_info esp_flash nvs_flash
        json log freertos
        vbot_config vbot_wifi
    EMBED_FILES
        "www/index.html"
)

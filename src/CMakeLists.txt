idf_component_register(
    SRCS "main.c" "bme68x.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server esp_http_client esp_netif esp_eth esp_event esp_wifi lwip mqtt
)

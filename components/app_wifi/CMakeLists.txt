idf_component_register(SRCS "app_wifi.c"
                    INCLUDE_DIRS "include"
REQUIRES esp_wifi esp_netif esp_event nvs_flash nvs_storage
                    )

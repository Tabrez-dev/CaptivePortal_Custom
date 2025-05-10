idf_component_register(SRCS "main.c" "app_station.c" "time_sync.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_local_server nvs_flash esp_wifi
                    )
                

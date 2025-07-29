idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_local_server app_wifi app_time_sync nvs_storage esp_wifi spi_ffs_storage rfid_manager aws_iot
                    )

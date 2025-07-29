idf_component_register(SRCS "app_local_server.c" "dns_server.c" 
EMBED_FILES webpage/index.html webpage/app.css webpage/app.js webpage/jquery-3.3.1.min.js webpage/favicon.ico webpage/rfid_management.html webpage/rfid_management.js
INCLUDE_DIRS "include"
REQUIRES json esp_http_server app_update esp_timer esp_wifi nvs_storage rfid_manager aws_iot
                    )

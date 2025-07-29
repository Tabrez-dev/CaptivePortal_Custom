idf_component_register(SRCS "aws_iot.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi esp_event mqtt json app_wifi)

target_add_binary_data(${COMPONENT_TARGET} "certs/AmazonRootCA1.pem" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "certs/device_certificate.pem" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "certs/private_key.pem" TEXT)

# AWS IoT Component CMake Configuration

idf_component_register(
    SRCS "aws_iot.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_aws_iot
)

# Register the test component
idf_component_register(
    SRCS 
        "unity_test.c"
    INCLUDE_DIRS 
        "include"
        "."
        "${CMAKE_CURRENT_SOURCE_DIR}/../spi_ffs_storage/include"
    REQUIRES 
        unity
        spi_ffs_storage
        nvs_flash
        spiffs
        log
        esp_system
        esp_common
)

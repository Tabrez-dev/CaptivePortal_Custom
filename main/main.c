#include "app_local_server.h"
#include "../components/app_time_sync/include/app_time_sync.h"
#include "app_wifi.h"
#include "nvs_storage.h"
#include "spi_ffs_storage.h"
#include "rfid_manager.h" // Added for RFID Management
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    nvs_storage_init();
    // Initialize WiFi first (this sets up the network stack)
    app_wifi_init();
    
    // Now that network is up, start the local server
    app_local_server_init();
    app_local_server_start();
    
    // Initialize time sync in non-blocking mode (requires network)
    ESP_LOGI(TAG, "Starting time synchronization in background");
    app_time_sync_init();
    
    // Initialize SPIFFS storage
    spiffs_storage_init();
    
    // Initialize RFID manager with error checking
    esp_err_t rfid_init_ret = rfid_manager_init();
    if (rfid_init_ret != ESP_OK) {
        ESP_LOGE("MAIN", "Failed to initialize RFID manager: %s", esp_err_to_name(rfid_init_ret));
    } else {
        ESP_LOGI("MAIN", "RFID manager initialized successfully");
    }
    // Main loop
    while (1) {
        app_local_server_process();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

#include "app_local_server.h"
#include "app_time_sync.h"
#include "app_wifi.h"
#include "nvs_storage.h"
#include "spi_ffs_storage.h"
#include "rfid_manager.h" // Added for RFID Management

void app_main(void)
{
    nvs_storage_init();
    // Initialize WiFi first (this sets up the network stack)
    app_wifi_init();
    
    // Now that network is up, start the local server
    app_local_server_init();
    app_local_server_start();
    
    // Initialize time sync (requires network)
    app_time_sync_init();
    spiffs_storage_init();
    //spiffs_storage_test();
    //ESP_LOGI(TAG, "Initializing RFID Manager");
    ESP_ERROR_CHECK(rfid_manager_init());
    // Main loop
    while (1) {
        app_local_server_process();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

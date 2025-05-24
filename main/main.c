/* Captive Portal Example

    This example code is in the Public Domain (or CC0 licensed, at your option.)

    Unless required by applicable law or agreed to in writing, this
    software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, either express or implied.
*/
#include <sys/param.h>
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "app_local_server.h"
#include "app_wifi.h"
#include "app_time_sync.h"

static const char *TAG = "main";

void app_main(void)
{
    // Initialize NVS needed by Wi-Fi
    ESP_ERROR_CHECK(nvs_flash_init());

    // Initialize networking stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop needed by the main app
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize components
    app_local_server_init();
    app_wifi_init();
    
    // Start components
    ESP_ERROR_CHECK(esp_wifi_start());
    app_local_server_start();
    app_time_sync_init();

    // Main loop
    while (1) {
        app_local_server_process();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

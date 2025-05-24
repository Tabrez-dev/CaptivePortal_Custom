#include "app_local_server.h"
#include "app_time_sync.h"
#include "app_wifi.h"

void app_main(void)
{
    // Initialize WiFi first (this sets up the network stack)
    app_wifi_init();
    
    // Now that network is up, start the local server
    app_local_server_init();
    app_local_server_start();
    
    // Initialize time sync (requires network)
    app_time_sync_init();
    
    // Main loop
    while (1) {
        app_local_server_process();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

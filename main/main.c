#include "app_local_server.h"
#include "../components/app_time_sync/include/app_time_sync.h"
#include "app_wifi.h"
#include "nvs_storage.h"
#include "spi_ffs_storage.h"
#include "rfid_manager.h" // Added for RFID Management
#include "aws_iot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "MAIN";

/**
 * @brief Handler for messages received from AWS IoT
 */
static void aws_iot_message_handler(const char *topic, int topic_len, const char *data, int data_len)
{
    ESP_LOGI(TAG, "Received message from AWS IoT");
    ESP_LOGI(TAG, "Topic: %.*s", topic_len, topic);
    ESP_LOGI(TAG, "Data: %.*s", data_len, data);
    
    // Create null-terminated strings for easier processing
    char topic_str[256] = {0};
    char data_str[1024] = {0};
    
    int topic_copy_len = (topic_len < sizeof(topic_str) - 1) ? topic_len : sizeof(topic_str) - 1;
    int data_copy_len = (data_len < sizeof(data_str) - 1) ? data_len : sizeof(data_str) - 1;
    
    strncpy(topic_str, topic, topic_copy_len);
    strncpy(data_str, data, data_copy_len);
    
    // Try to parse as JSON
    cJSON *json = cJSON_Parse(data_str);
    if (json != NULL) {
        // Check for message field
        cJSON *message = cJSON_GetObjectItem(json, "message");
        if (cJSON_IsString(message)) {
            ESP_LOGI(TAG, "Message field: %s", message->valuestring);
        }
        
        // Handle different commands if present
        cJSON *command = cJSON_GetObjectItem(json, "command");
        if (cJSON_IsString(command)) {
            const char *cmd = command->valuestring;
            ESP_LOGI(TAG, "Command: %s", cmd);
            
            // Example command handlers
            if (strcmp(cmd, "reboot") == 0) {
                ESP_LOGW(TAG, "Reboot command received. Rebooting in 5 seconds...");
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                esp_restart();
            } else if (strcmp(cmd, "status") == 0) {
                ESP_LOGI(TAG, "Status request received");
                // You could publish status back to AWS IoT here
            } else if (strcmp(cmd, "led_on") == 0) {
                ESP_LOGI(TAG, "LED ON command received");
                // Control LED if you have one connected
            } else if (strcmp(cmd, "led_off") == 0) {
                ESP_LOGI(TAG, "LED OFF command received");
                // Control LED if you have one connected
            } else {
                ESP_LOGW(TAG, "Unknown command: %s", cmd);
            }
        }
        
        // You can check for other fields like temperature/humidity
        // but ignore them since they're from your own published messages
        cJSON *temperature = cJSON_GetObjectItem(json, "temperature");
        cJSON *humidity = cJSON_GetObjectItem(json, "humidity");
        if (temperature || humidity) {
            ESP_LOGD(TAG, "Ignoring sensor data echo (from our own publish)");
        }
        
        cJSON_Delete(json);
    } else {
        ESP_LOGW(TAG, "Failed to parse message as JSON. Raw message: %s", data_str);
    }
}

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
    
    // Register AWS IoT message handler
    // This will be called when messages are received on subscribed topics
    esp_err_t aws_cb_ret = aws_iot_set_message_callback(aws_iot_message_handler);
    if (aws_cb_ret == ESP_OK) {
        ESP_LOGI(TAG, "AWS IoT message handler registered successfully");
    } else {
        ESP_LOGE(TAG, "Failed to register AWS IoT message handler: %s", esp_err_to_name(aws_cb_ret));
    }
    
    // Main loop
    while (1) {
        app_local_server_process();
        rfid_manager_process();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

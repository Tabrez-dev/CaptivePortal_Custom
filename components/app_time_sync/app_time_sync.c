#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "app_time_sync.h"
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "app_time_sync";

// Event group to signal when time is synchronized
static EventGroupHandle_t time_sync_event_group;
#define TIME_SYNC_COMPLETED_BIT BIT0

// Task handle for the time sync task
static TaskHandle_t time_sync_task_handle = NULL;

static void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Notification of a time synchronization event");
    
    // Set the event bit to indicate time sync is complete
    if (time_sync_event_group != NULL) {
        xEventGroupSetBits(time_sync_event_group, TIME_SYNC_COMPLETED_BIT);
    }
}

static void configure_timezone(void) {
    // Set timezone to IST (UTC+5:30)
    setenv("TZ", "IST-5:30", 1);
    tzset();
}

/**
 * @brief Task to handle time synchronization in the background
 * 
 * This task initializes SNTP, waits for time sync, and then deletes itself
 */
static void time_sync_task(void *pvParameters) {
    ESP_LOGI(TAG, "Time sync task started");
    
    // Use a simpler approach for SNTP initialization
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    // Set NTP servers
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "time.windows.com");
    
    // Set callback for time synchronization notification
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    
    // Initialize SNTP
    esp_sntp_init();
    
    // For compatibility with the rest of the code
    esp_err_t err = ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SNTP: %s", esp_err_to_name(err));
        // Set the event bit even on failure so the application continues
        xEventGroupSetBits(time_sync_event_group, TIME_SYNC_COMPLETED_BIT);
        vTaskDelete(NULL);
        return;
    }

    // Wait for time sync (30 second timeout)
    int retry_count = 30;
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && retry_count-- > 0) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d)", retry_count);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    if (retry_count <= 0) {
        ESP_LOGE(TAG, "Failed to get time from NTP server");
    } else {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "Time synced: %s", strftime_buf);
    }

    // Set timezone and log current time
    configure_timezone();
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current local time is: %s", strftime_buf);
    
    // Set the event bit to indicate time sync attempt is complete
    xEventGroupSetBits(time_sync_event_group, TIME_SYNC_COMPLETED_BIT);
    
    // Task completed, delete itself
    time_sync_task_handle = NULL;
    vTaskDelete(NULL);
}

void app_time_sync_init(void) {
    // Check if time is already set
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Create event group for synchronization
    time_sync_event_group = xEventGroupCreate();
    if (time_sync_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return;
    }

    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGI(TAG, "Time is not set. Starting time sync task...");
        
        // Create a task for time synchronization
        BaseType_t task_created = xTaskCreate(
            time_sync_task,          // Task function
            "time_sync_task",        // Task name
            4096,                    // Stack size (bytes)
            NULL,                    // Task parameters
            5,                       // Priority (higher number = higher priority)
            &time_sync_task_handle   // Task handle
        );
        
        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create time sync task");
            vEventGroupDelete(time_sync_event_group);
            time_sync_event_group = NULL;
            return;
        }
        
        ESP_LOGI(TAG, "Time sync task created successfully");
    } else {
        ESP_LOGI(TAG, "Time is already set");
        
        // Set timezone and log current time
        configure_timezone();
        time(&now);
        localtime_r(&now, &timeinfo);
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "The current local time is: %s", strftime_buf);
        
        // Set the event bit since time is already set
        xEventGroupSetBits(time_sync_event_group, TIME_SYNC_COMPLETED_BIT);
    }
}

/**
 * @brief Wait for time synchronization to complete with timeout
 * 
 * @param timeout_ms Timeout in milliseconds
 * @return true if time sync completed, false if timeout
 */
bool app_time_sync_wait(uint32_t timeout_ms) {
    if (time_sync_event_group == NULL) {
        return false;
    }
    
    // Wait for the time sync completed bit with timeout
    EventBits_t bits = xEventGroupWaitBits(
        time_sync_event_group,
        TIME_SYNC_COMPLETED_BIT,
        pdFALSE,  // Don't clear the bits
        pdFALSE,  // Any bit will do
        timeout_ms / portTICK_PERIOD_MS
    );
    
    return (bits & TIME_SYNC_COMPLETED_BIT) != 0;
}

/**
 * @brief Check if time synchronization is completed
 * 
 * @return true if time sync completed, false otherwise
 */
bool app_time_sync_is_completed(void) {
    if (time_sync_event_group == NULL) {
        return false;
    }
    
    EventBits_t bits = xEventGroupGetBits(time_sync_event_group);
    return (bits & TIME_SYNC_COMPLETED_BIT) != 0;
}

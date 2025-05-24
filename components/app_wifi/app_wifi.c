#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet.h"
#include "app_wifi.h"

static const char *TAG = "app_wifi";

// Forward declarations
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data);
static void wifi_init_softap(void);

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

void app_wifi_init(void)
{
    // Initialize the event group
    s_wifi_event_group = xEventGroupCreate();
    
    // Initialize Wi-Fi including netif with default config
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    
    // Initialize AP and STA modes
    wifi_init_softap();
    wifi_init_sta();
    
    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_init_sta(void)
{
    // Configure station mode
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    ESP_LOGI(TAG, "Setting WiFi configuration SSID: %s", CONFIG_ESP_WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                    MAC2STR(event->mac), event->aid);
        }
        else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                    MAC2STR(event->mac), event->aid);
        }
        else if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        }
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (s_retry_num < 5) {  // Retry 5 times
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "Retry to connect to the AP");
            }
            ESP_LOGI(TAG, "Failed to connect to AP");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
    }
}

static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = CONFIG_ESP_WIFI_AP_SSID,
            .ssid_len = strlen(CONFIG_ESP_WIFI_AP_SSID),
            .password = CONFIG_ESP_WIFI_AP_PASSWORD,
            .max_connection = CONFIG_ESP_MAX_AP_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    
    if (strlen(CONFIG_ESP_WIFI_AP_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
}

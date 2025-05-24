#pragma once

#include <stdint.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"

// WiFi configuration that can be set via menuconfig
#ifndef CONFIG_ESP_WIFI_SSID
#define CONFIG_ESP_WIFI_SSID ""
#endif

#ifndef CONFIG_ESP_WIFI_PASSWORD
#define CONFIG_ESP_WIFI_PASSWORD ""
#endif

/**
 * @brief Initialize WiFi in both AP and STA modes
 */
void app_wifi_init(void);

/**
 * @brief Initialize WiFi station mode with configured SSID and password
 */
void wifi_init_sta(void);

// Event handler is internal to app_wifi.c

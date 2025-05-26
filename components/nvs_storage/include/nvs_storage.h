#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize NVS (Non-volatile storage)
 * 
 * This function initializes the default NVS partition and creates a namespace
 * for the application's persistent storage.
 * 
 * @return esp_err_t 
 *     - ESP_OK if initialization was successful
 *     - Other error codes from the underlying NVS APIs
 */
esp_err_t nvs_storage_init(void);

/**
 * @brief Deinitialize NVS storage
 * 
 * This function should be called when NVS storage is no longer needed.
 */
void nvs_storage_deinit(void);
bool nvs_storage_test(void);

bool nvs_storage_set_wifi_credentials(const char *ssid, const char *password);
bool wifi_credentials_test(void);
bool nvs_storage_get_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size);

#ifdef __cplusplus
}
#endif

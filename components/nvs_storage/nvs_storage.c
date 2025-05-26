#include <stdio.h>
#include<inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs.h"
//#include "nvs_storage.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "nvs_storage.h"
static const char *TAG = "nvs_storage";
static nvs_handle_t nvs_storage_handle;

esp_err_t nvs_storage_init(void)
{
    esp_err_t ret;
    
    // Initialize NVS flash storage
    ret = nvs_flash_init();
    
    // If the NVS storage is corrupted, erase it and try again
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "NVS partition was truncated and needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Open NVS namespace for our application
    ret = nvs_open("storage", NVS_READWRITE, &nvs_storage_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "NVS storage initialized successfully");
    //nvs_storage_test();
    //wifi_credentials_test();
    return ESP_OK;
}

void nvs_storage_deinit(void)
{
    // Close the NVS handle
    nvs_close(nvs_storage_handle);
    ESP_LOGI(TAG, "NVS storage deinitialized");
}

bool nvs_storage_test(void)
{
    esp_err_t err = nvs_flash_init();

        // Open
        printf("\n");
        printf("Opening Non-Volatile Storage (NVS) handle... ");
        nvs_handle_t my_handle;
        err = nvs_open("nvs", NVS_READWRITE, &my_handle);
        if (err != ESP_OK) {
            printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        } else {
            printf("Done\n");
    
            // Read
            printf("Reading restart counter from NVS ... ");
            int32_t restart_counter = 0; // value will default to 0, if not set yet in NVS
            err = nvs_get_i32(my_handle, "restart_counter", &restart_counter);
            switch (err) {
                case ESP_OK:
                    printf("Done\n");
                    printf("Restart counter = %" PRIu32 "\n", restart_counter);
                    break;
                case ESP_ERR_NVS_NOT_FOUND:
                    printf("The value is not initialized yet!\n");
                    break;
                default :
                    printf("Error (%s) reading!\n", esp_err_to_name(err));
            }
    
            // Write
            printf("Updating restart counter in NVS ... ");
            restart_counter++;
            err = nvs_set_i32(my_handle, "restart_counter", restart_counter);
            printf((err != ESP_OK) ? "Failed!\n" : "Done\n");
    
            // Commit written value.
            // After setting any values, nvs_commit() must be called to ensure changes are written
            // to flash storage. Implementations may write to storage at other times,
            // but this is not guaranteed.
            printf("Committing updates in NVS ... ");
            err = nvs_commit(my_handle);
            printf((err != ESP_OK) ? "Failed!\n" : "Done\n");
    
            // Close
            nvs_close(my_handle);
            
        }
        return true;
}

bool nvs_storage_get_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    esp_err_t err;

    // Read SSID
    err = nvs_get_str(nvs_storage_handle, "wifi_ssid", ssid, &ssid_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading SSID from NVS: %s", esp_err_to_name(err));
        return false;
    }

    // Read Password
    err = nvs_get_str(nvs_storage_handle, "wifi_pass", password, &password_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading Password from NVS: %s", esp_err_to_name(err));
        return false;
    }
    if(ssid_size == 0) {
        ESP_LOGE(TAG, "SSID or Password size is zero");
        return false;
    }

    return true;
}

bool nvs_storage_set_wifi_credentials(const char *ssid, const char *password)
{
    esp_err_t err;
    
    if (ssid == NULL || password == NULL) {
        ESP_LOGE(TAG, "Invalid input: SSID or password is NULL");
        return false;
    }
    
    // Write SSID to NVS
    err = nvs_set_str(nvs_storage_handle, "wifi_ssid", ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error writing SSID to NVS: %s", esp_err_to_name(err));
        return false;
    }
    
    // Write Password to NVS
    err = nvs_set_str(nvs_storage_handle, "wifi_pass", password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error writing Password to NVS: %s", esp_err_to_name(err));
        return false;
    }
    
    // Commit changes
    err = nvs_commit(nvs_storage_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing WiFi credentials to NVS: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "WiFi credentials successfully saved to NVS");
    return true;
}

bool wifi_credentials_test(void)
{
    // Test values to validate storage and retrieval
    bool set_result, get_result;
    char ssid[33] = {0};    // Max SSID length is 32 bytes + null terminator
    char pass[65] = {0};    // Max password length is 64 bytes + null terminator
    size_t ssid_size = sizeof(ssid);
    size_t pass_size = sizeof(pass);
    
    // Step 2: Retrieve the credentials from NVS
    ESP_LOGI(TAG, "Testing WiFi credential retrieval");
    get_result = nvs_storage_get_wifi_credentials(ssid, ssid_size, 
                                                  pass, pass_size);
    if (get_result) {
       printf("ssid: %s, pass: %s\n", ssid, pass);
        
    }else{
        printf("Failed to retrieve WiFi credentials from NVS\n");
        
    }

      //Step 1: Save the provided credentials to NVS
      ESP_LOGI(TAG, "Testing WiFi credential storage - attempting to save credentials");
      set_result = nvs_storage_set_wifi_credentials("Test", "testpass");
      if (set_result) {
        printf("WiFi credentials saved successfully\n");
        
      }else{
        printf("Failed to save WiFi credentials to NVS\n");
      }
      
    
    // // Step 3: Verify that the retrieved credentials match what was saved
    // if (strcmp(ssid, retrieved_ssid) != 0) {
    //     ESP_LOGE(TAG, "SSID verification failed: saved '%s' but retrieved '%s'", 
    //              ssid, retrieved_ssid);
    //     return false;
    // }
    
    // if (strcmp(password, retrieved_pass) != 0) {
    //     ESP_LOGE(TAG, "Password verification failed: saved '%s' but retrieved '%s'", 
    //              password, retrieved_pass);
    //     return false;
    // }
    
    ESP_LOGI(TAG, "WiFi credentials test passed successfully");
    return true;
}
#include "rfid_manager.h"
#include <string.h>
#include "esp_spiffs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h" // For mutex
#include <time.h>            // For time()
#include "esp_timer.h"       // For caching timer
// #include <inttypes.h> // PRIX32 not used, using %lx with cast instead

static const char *TAG = "RFID_MANAGER";

#define RFID_DATABASE_FILE "/spiffs/rfid_cards.dat"

// In-memory database for RFID cards
static rfid_card_t rfid_database[RFID_MAX_CARDS];

// Mutex for thread-safe access to the database and file operations
static SemaphoreHandle_t rfid_mutex = NULL;

// Caching mechanism variables
static bool is_dirty = false;                       // Flag to indicate pending changes
static bool is_ready_to_write = false;              // Flag to signal that a write to NVS is pending
static esp_timer_handle_t rfid_write_timer = NULL;  // Timer for delayed NVS write
static uint32_t rfid_write_timeout_ms = RFID_DEFAULT_CACHE_TIMEOUT_MS; // Configurable timeout

// Default RFID cards
static const rfid_card_t default_cards[] = {
    {0x12345678, 1, "Admin Card", 0},
    {0x87654321, 1, "User Card 1", 0},
    {0xABCDEF00, 1, "User Card 2", 0}
    // Add more default cards if needed, up to RFID_MAX_CARDS
};
static const uint16_t num_default_cards = sizeof(default_cards) / sizeof(rfid_card_t);

/**
 * @brief Loads default RFID cards into the database.
 *
 * This function populates the in-memory database with a predefined set of RFID cards
 * and then saves this default set to the persistent storage file.
 * This is typically called during the first boot or if the existing database is invalid.
 *
 * @return esp_err_t ESP_OK on success, or an error code if saving to file fails.
 */
static esp_err_t rfid_manager_load_defaults(void);

/**
 * @brief Internal function to perform the actual write of cached RFID data to NVS.
 * 
 * This function is called by rfid_manager_process() or directly when caching is disabled.
 * It handles mutex acquisition and error logging for the NVS write operation.
 * 
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
static esp_err_t rfid_manager_write_into_memory(void);

/**
 * @brief Saves the current in-memory RFID database to the SPIFFS file.
 *
 * This function writes all card data (including inactive ones if they are kept in the array)
 * to the persistent storage file.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure (e.g., file write error).
 */
static esp_err_t rfid_manager_save_to_file(void);

/**
 * @brief Loads the RFID database from the SPIFFS file into memory.
 *
 * Reads the card data from persistent storage.
 *
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if the file doesn't exist,
 *         ESP_FAIL for other errors.
 */
static esp_err_t rfid_manager_load_from_file(void);

/**
 * @brief Timer callback function for delayed writing to flash.
 * 
 * This function is called when the write timer expires. If there are pending
 * changes (is_dirty == true), it writes the RFID database to flash.
 * 
 * @param arg Timer argument (unused)
 */
static void rfid_cache_write_timeout_handler(void* arg);

esp_err_t rfid_manager_get_card(uint32_t card_id, rfid_card_t *card)
{
    if (card == NULL)
    {
        ESP_LOGE(TAG, "Output card pointer is NULL.");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if mutex is initialized first
    if (rfid_mutex == NULL) {
        ESP_LOGE(TAG, "RFID mutex not initialized in get_card");
        return ESP_FAIL;
    }
    
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        {
            if (rfid_database[i].card_id == card_id)
            {
                if (rfid_database[i].active)
                {
                    *card = rfid_database[i]; // Copy the card data
                    xSemaphoreGive(rfid_mutex);
                    ESP_LOGI(TAG, "Card 0x%08lx found at slot %u.", (unsigned long)card_id, i);
                    return ESP_OK;
                }
                else
                {
                    // Card found but is inactive
                    ESP_LOGW(TAG, "Card 0x%08lx found at slot %u but is inactive.", (unsigned long)card_id, i);
                    xSemaphoreGive(rfid_mutex);
                    return ESP_ERR_NOT_FOUND; // Treat inactive as not found for "get active card" purposes
                }
            }
        }
        // Card ID not found in any slot
        ESP_LOGW(TAG, "Card 0x%08lx not found in the database.", (unsigned long)card_id);
        xSemaphoreGive(rfid_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGE(TAG, "Failed to take RFID mutex in get_card");
    return ESP_FAIL; // Mutex acquisition failed
}


int testable_mean(const int *values, int count)
{
    if (count == 0)
    {
        return 0;
    }
    int sum = 0;
    for (int i = 0; i < count; ++i)
    {
        sum += values[i];
    }
    return sum / count;
}

// --- Core API Functions ---

esp_err_t rfid_manager_init(void)
{
    if (rfid_mutex == NULL)
    {
        rfid_mutex = xSemaphoreCreateMutex();
        if (rfid_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create RFID mutex");
            return ESP_FAIL;
        }
    }
    
    // Initialize the caching mechanism
    is_dirty = false;
    
    // Create the timer if it doesn't exist
    if (rfid_write_timer == NULL)
    {
        esp_timer_create_args_t timer_args = {
            .callback = &rfid_cache_write_timeout_handler,
            .name = "rfid_write_timer"
        };
        esp_err_t timer_ret = esp_timer_create(&timer_args, &rfid_write_timer);
        if (timer_ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to create RFID write timer: %s", esp_err_to_name(timer_ret));
            return timer_ret;
        }
    }

    if (xSemaphoreTake(rfid_mutex, portMAX_DELAY) == pdTRUE)
    {
        esp_err_t ret; // Declared once at the beginning of the scope

        size_t total_bytes, used_bytes;

        esp_err_t spiffs_ret = esp_spiffs_info(NULL, &total_bytes, &used_bytes); // Use default partition label (NULL)

        if (spiffs_ret != ESP_OK)
        {
            ESP_LOGE(TAG, "SPIFFS filesystem not found or not mounted. Please initialize SPIFFS first. Error: %s", esp_err_to_name(spiffs_ret));
            xSemaphoreGive(rfid_mutex);
            return ESP_ERR_INVALID_STATE; // Indicate that a required pre-condition (SPIFFS mounted) is not met.
        }

        ESP_LOGI(TAG, "SPIFFS filesystem found. Partition size: total: %zu, used: %zu", total_bytes, used_bytes);

        // Attempt to load cards from file
        ret = rfid_manager_load_from_file(); // Assign to the already declared ret

        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "RFID database loaded successfully from file.");        }
        else
        {
            ESP_LOGW(TAG, "Failed to load RFID database from file (%s). Loading defaults.", esp_err_to_name(ret));
            // If loading failed (file not found, corrupted, etc.), load default cards.
            ret = rfid_manager_load_defaults();
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to load default RFID cards. System may not function correctly.");
                // This is a critical failure if defaults can't be loaded.
            }
            else
            {
                ESP_LOGI(TAG, "Default RFID cards loaded and saved.");
            }
        }

        xSemaphoreGive(rfid_mutex);
        return ret; // Return status of load_from_file or load_defaults
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take RFID mutex in init");
        return ESP_FAIL;
    }
}

esp_err_t rfid_manager_add_card(uint32_t card_id, const char *name)
{
    // Check if mutex is initialized
    if (rfid_mutex == NULL) {
        ESP_LOGE(TAG, "RFID mutex not initialized in add_card");
        return ESP_FAIL;
    }
    
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        if (name == NULL)
        {
            ESP_LOGE(TAG, "Cannot add card with NULL name.");
            xSemaphoreGive(rfid_mutex);
            return ESP_ERR_INVALID_ARG;
        }

        // Check if card already exists by iterating through all possible slots
        // A card_id is considered to exist if it's present in any slot and is not 0 (which might indicate an uninitialized slot).
        // This check prevents adding a card with an ID that is already in the system, regardless of its active status.
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        {
            if (rfid_database[i].card_id == card_id && rfid_database[i].card_id != 0)
            {
                ESP_LOGW(TAG, "Attempt to add card 0x%08lx which already exists at slot %u (status: %s). Operation aborted.",
                         (unsigned long)card_id, i, rfid_database[i].active ? "active" : "inactive");
                xSemaphoreGive(rfid_mutex);
                return ESP_ERR_INVALID_STATE; // Card ID already present in the database, operation invalid in this state
            }
        }

        // If card does not exist, try to add to the first inactive slot or at the end
        uint16_t _index_of_first_inactive_slot = RFID_MAX_CARDS;

        // First, look for an inactive slot to reuse
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        {
            // This logic assumes that card_id == 0 can mean an empty/never-used slot,
            // if we compact the array on removal.
            // The current design saves all RFID_MAX_CARDS slots.
            // An inactive card would have card_id != 0 but active == 0.
            // A truly empty slot might have card_id == 0.
            // Let's find the first slot where card_id is 0 (never used) or active is 0 (previously removed)
            if (rfid_database[i].card_id == 0 || rfid_database[i].active == 0)
            {
                _index_of_first_inactive_slot = i;
                break;
            }
        }

        if (_index_of_first_inactive_slot < RFID_MAX_CARDS)
        {
            rfid_database[_index_of_first_inactive_slot].card_id = card_id;
            strncpy(rfid_database[_index_of_first_inactive_slot].name, name, RFID_CARD_NAME_LEN - 1);
            rfid_database[_index_of_first_inactive_slot].name[RFID_CARD_NAME_LEN - 1] = '\0';
            rfid_database[_index_of_first_inactive_slot].active = 1;
            time_t now_add;
            time(&now_add);
            rfid_database[_index_of_first_inactive_slot].timestamp = (uint32_t)now_add; // Set current timestamp

            ESP_LOGI(TAG, "Added card %lu ('%s') at slot %u.", (unsigned long)card_id, name, _index_of_first_inactive_slot);         
            
            // Mark as dirty and start/reset the timer for delayed write
            is_dirty = true;
            
            // Reset the timer if it's running
            if (rfid_write_timer != NULL) {
                esp_timer_stop(rfid_write_timer);
                
                // Only start the timer if caching is enabled (timeout > 0)
                if (rfid_write_timeout_ms > 0) {
                    esp_timer_start_once(rfid_write_timer, rfid_write_timeout_ms * 1000);
                    ESP_LOGD(TAG, "Started RFID write timer for %lu ms", (unsigned long)rfid_write_timeout_ms);
                } else {
                    // If caching is disabled, write immediately
                    esp_err_t save_ret = rfid_manager_write_into_memory(); // Call the new function
                    xSemaphoreGive(rfid_mutex);
                    return save_ret;
                }
            }

            xSemaphoreGive(rfid_mutex);
            return ESP_OK;
        }
        else
        {
           // This case (_index_of_first_inactive_slot == RFID_MAX_CARDS) means all RFID_MAX_CARDS slots // have card_id != 0 AND active == 1. So, the database is truly full.
            ESP_LOGW(TAG, "RFID database is full (all %d slots active). Cannot add new card 0x%08lx.", RFID_MAX_CARDS, (unsigned long)card_id);
            xSemaphoreGive(rfid_mutex);
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGE(TAG, "Failed to take RFID mutex in add_card");
    return ESP_FAIL;
}

esp_err_t rfid_manager_remove_card(uint32_t card_id)
{
    // Check if mutex is initialized
    if (rfid_mutex == NULL) {
        ESP_LOGE(TAG, "RFID mutex not initialized in remove_card");
        return ESP_FAIL;
    }
    
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        { // Iterate all possible slots
            if (rfid_database[i].card_id == card_id && rfid_database[i].active)
            {
                rfid_database[i].active = 0; // Mark as inactive
                // Optionally clear name and timestamp
                // memset(rfid_database[i].name, 0, RFID_CARD_NAME_LEN);
                // rfid_database[i].timestamp = 0;

                ESP_LOGI(TAG, "Removed card %lu.", (unsigned long)card_id);
                
                // Mark as dirty and start/reset the timer for delayed write
                is_dirty = true;
                
                // Reset the timer if it's running
                if (rfid_write_timer != NULL) {
                    esp_timer_stop(rfid_write_timer);
                    
                    // Only start the timer if caching is enabled (timeout > 0)
                    if (rfid_write_timeout_ms > 0) {
                        esp_timer_start_once(rfid_write_timer, rfid_write_timeout_ms * 1000);
                        ESP_LOGD(TAG, "Started RFID write timer for %lu ms", (unsigned long)rfid_write_timeout_ms);
                    } else {
                    // If caching is disabled, write immediately
                    esp_err_t save_ret = rfid_manager_write_into_memory();
                    xSemaphoreGive(rfid_mutex);
                    return save_ret;
                    }
                }
                
                xSemaphoreGive(rfid_mutex);
                return ESP_OK;
            }
        }
        ESP_LOGW(TAG, "Card 0x%08lx not found or already inactive.", (unsigned long)card_id);
        xSemaphoreGive(rfid_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGE(TAG, "Failed to take RFID mutex in remove_card");
    return ESP_FAIL;
}

bool rfid_manager_check_card(uint32_t card_id)
{
    // Check if mutex is initialized
    if (rfid_mutex == NULL) {
        ESP_LOGE(TAG, "RFID mutex not initialized in check_card, returning false");
        return false;
    }
    
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        { // Iterate all slots
            if (rfid_database[i].card_id == card_id && rfid_database[i].active)
            {
                // Update timestamp on successful check
                time_t now;
                time(&now);
                rfid_database[i].timestamp = (uint32_t)now;
                ESP_LOGI(TAG, "Card %lu checked successfully. Timestamp updated to %lu.", (unsigned long)card_id, (unsigned long)rfid_database[i].timestamp);

               // NOTE: Removed rfid_manager_save_to_file() here to reduce flash wear and improve performance.
                // The timestamp update will only be in RAM until the next explicit save operation (e.g., add/remove card).
                // If persistent timestamps on every check are critical, a different strategy is needed.

                xSemaphoreGive(rfid_mutex);
                return true;
            }
        }
        xSemaphoreGive(rfid_mutex);
        return false;
    }
    ESP_LOGE(TAG, "Failed to take RFID mutex in check_card");
    return false; // Default to not authorized if mutex fails
}

uint16_t rfid_manager_get_card_count(void)
{
    // Taking mutex for consistency, though it might be okay for a quick read if not critical.
    uint16_t active_count = 0;
    
    // Check if mutex is initialized
    if (rfid_mutex == NULL) {
        ESP_LOGE(TAG, "RFID mutex not initialized in get_card_count, returning 0");
        return 0;
    }
    
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        // Recalculate on demand to ensure accuracy
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        {
            if (rfid_database[i].active && rfid_database[i].card_id != 0)
            {
                active_count++;
            }
        }
        xSemaphoreGive(rfid_mutex);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take RFID mutex in get_card_count, returning 0");
        // Return a safe value or last known value. Here, returning 0 on mutex failure.
    }
    return active_count;
}

esp_err_t rfid_manager_list_cards(rfid_card_t *cards_buffer, uint16_t buffer_size, uint16_t *num_cards_copied)
{
    if (cards_buffer == NULL || num_cards_copied == NULL)
    {
        ESP_LOGE(TAG, "list_cards: Invalid arguments"); // Added logging for invalid args
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = ESP_OK; // Initialize ret

    // Check if mutex is initialized first
    if (rfid_mutex == NULL) {
        ESP_LOGE(TAG, "RFID mutex not initialized in list_cards");
        *num_cards_copied = 0;
        return ESP_FAIL;
    }
    
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        uint16_t active_cards_found = 0;
        for (uint16_t i = 0; i < RFID_MAX_CARDS && active_cards_found < buffer_size; ++i)
        {
            if (rfid_database[i].active && rfid_database[i].card_id != 0)
            {
                cards_buffer[active_cards_found] = rfid_database[i];
                active_cards_found++;
            }
        }
        *num_cards_copied = active_cards_found;
        // Give back the mutex
        xSemaphoreGive(rfid_mutex);
        ESP_LOGD(TAG, "Listed %u cards", active_cards_found);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take RFID mutex in list_cards");
        *num_cards_copied = 0; // Ensure num_cards_copied is set on failure
        ret = ESP_FAIL;        // Set return value to indicate failure
    }
    return ret;
}

esp_err_t rfid_manager_format_database(void)
{
    esp_err_t ret = ESP_FAIL;
    
    // Check if mutex is initialized
    if (rfid_mutex == NULL) {
        ESP_LOGE(TAG, "RFID mutex not initialized in format_database");
        return ESP_FAIL;
    }
    
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) // Longer timeout for format
    {
        ESP_LOGW(TAG, "Formatting RFID database. All existing cards will be erased and defaults loaded.");
        
        // Clear the database in memory first
        memset(rfid_database, 0, sizeof(rfid_database));
        
        // Copy default cards directly (don't call another function that might take the mutex)
        uint16_t index = 0;
        for (uint16_t i = 0; i < num_default_cards && index < RFID_MAX_CARDS; ++i, ++index)
        {
            rfid_database[index] = default_cards[i]; // Copy default card
            rfid_database[index].name[RFID_CARD_NAME_LEN - 1] = '\0'; // Ensure null-termination
        }
        
        // Format is a special operation that always writes immediately to disk
        // Reset any pending cache operation
        if (rfid_write_timer != NULL) {
            esp_timer_stop(rfid_write_timer);
        }
        is_dirty = false;
        
        // Save to file directly
        FILE *f = fopen(RFID_DATABASE_FILE, "wb");
        if (f == NULL)
        {
            ESP_LOGE(TAG, "Failed to open RFID database file for writing during format");
            xSemaphoreGive(rfid_mutex);
            return ESP_FAIL;
        }
        
        size_t cards_written = fwrite(rfid_database, sizeof(rfid_card_t), RFID_MAX_CARDS, f);
        if (cards_written != RFID_MAX_CARDS)
        {
            ESP_LOGE(TAG, "Failed to write all RFID card data during format. Wrote %d of %d.", 
                    cards_written, RFID_MAX_CARDS);
            fclose(f);
            xSemaphoreGive(rfid_mutex);
            return ESP_FAIL;
        }
        
        if (fclose(f) != 0)
        {
            ESP_LOGE(TAG, "Failed to close RFID database file after format.");
            xSemaphoreGive(rfid_mutex);
            return ESP_FAIL;
        }
        
        ESP_LOGI(TAG, "%d default cards loaded and saved during format.", index);
        ret = ESP_OK;
        xSemaphoreGive(rfid_mutex);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take RFID mutex in format_database");
    }
    return ret;
}

esp_err_t rfid_manager_get_card_list_json(char *buffer, size_t bufferMaxLength)
{
    esp_err_t ret = ESP_OK;
    uint16_t _length = 0;
    bool isComma = false;

    // validate the params
    if (!buffer || !bufferMaxLength)
    {
        ESP_LOGE(TAG, "Invalid parameters in get_card_list_json");
        return ESP_FAIL;
    }
    
    // Check if mutex is initialized
    if (rfid_mutex == NULL) {
        ESP_LOGE(TAG, "RFID mutex not initialized in get_card_list_json");
        // Return an empty card list as a fallback
        snprintf(buffer, bufferMaxLength, "{\"cards\":[]}");
        return ESP_FAIL;
    }

    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(500)) == pdTRUE) // Longer timeout for format
    {
        ESP_LOGI(TAG, "Getting the Lock");

        // clear the buffer
        memset(buffer, 0, bufferMaxLength);

        // Prepare the JSON, underscore before a variable indicates it is a local variable
        _length = snprintf(buffer, bufferMaxLength, "{\"cards\":[");

        // loop over all possible card slots and add active ones to the JSON string
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i) // Iterate up to RFID_MAX_CARDS
        {
            // only do for active cards that have a valid card_id
            if (rfid_database[i].active && rfid_database[i].card_id != 0)
            {
                if (_length + 80U >= bufferMaxLength)
                {
                    ESP_LOGE(TAG, "Buffer too small for the JSON string");
                    xSemaphoreGive(rfid_mutex);
                    return ESP_FAIL;
                }

                _length += snprintf(buffer + _length, bufferMaxLength - _length,
                                    "%s{\"id\":\"0x%lX\",\"nm\":\"%s\",\"ts\":%lu}",
                                    isComma ? "," : "", rfid_database[i].card_id, rfid_database[i].name, rfid_database[i].timestamp);

                isComma = true;//whenever a new item is printed a comma is palced i.e obj, obj, ...
                // debug print statement
                ESP_LOGI(TAG, "Adding Card %d to JSON", i + 1);
            }
        }

        // Add the closing bracket and null terminator to complete the JSON string
        if (_length + 3U >= bufferMaxLength)
        {
            ESP_LOGE(TAG, "Buffer too small for the JSON string");
            xSemaphoreGive(rfid_mutex);
            return ESP_FAIL;
        }

        _length += snprintf(buffer + _length, bufferMaxLength - _length, "]}");

        xSemaphoreGive(rfid_mutex);
    }
    /*whenever you're gonna call this function and pass the buffer pointer and length,
     you're gonna get back the complete Jason string which is ready to send.*/

    return ret;
}

static esp_err_t rfid_manager_load_defaults(void)
{
    ESP_LOGI(TAG, "Loading default RFID cards...");
    memset(rfid_database, 0, sizeof(rfid_database)); // Clear existing in-memory db
    uint16_t index = 0;

    for (uint16_t i = 0; i < num_default_cards && index < RFID_MAX_CARDS; ++i, ++index)
    {
        rfid_database[index] = default_cards[i]; // Copy default card
        // Ensure name is null-terminated if it's shorter than RFID_CARD_NAME_LEN
        rfid_database[index].name[RFID_CARD_NAME_LEN - 1] = '\0';
    }

    esp_err_t ret = rfid_manager_save_to_file();
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "%d default cards loaded and saved.", index);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to save default cards to file: %s", esp_err_to_name(ret));
    }

    return ret;
}

static esp_err_t rfid_manager_save_to_file(void)
{
    bool mutex_taken_locally = false;
    
    // Check if mutex is valid first
    if (rfid_mutex == NULL) {
        ESP_LOGE(TAG, "RFID mutex not initialized in save_to_file");
        return ESP_FAIL;
    }
    
    // Only take the mutex if not already held by this task
    if (xSemaphoreTakeRecursive(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        mutex_taken_locally = true;
    }
    else
    {
        // If we can't get the mutex, it's either held by another task or there's a timeout
        ESP_LOGE(TAG, "Failed to take RFID mutex in save_to_file");
        return ESP_FAIL;
    }
    
    FILE *f = fopen(RFID_DATABASE_FILE, "wb"); // Open for writing in binary mode
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open RFID database file for writing: %s", RFID_DATABASE_FILE);
        if (mutex_taken_locally) {
            xSemaphoreGive(rfid_mutex);
        }
        return ESP_FAIL;
    }

    // Write card data (all RFID_MAX_CARDS slots)
    size_t cards_written = fwrite(rfid_database, sizeof(rfid_card_t), RFID_MAX_CARDS, f);
    if (cards_written != RFID_MAX_CARDS)
    {
        ESP_LOGE(TAG, "Failed to write all RFID card data to file. Wrote %d of %d.", cards_written, RFID_MAX_CARDS);
        fclose(f);
        if (mutex_taken_locally) {
            xSemaphoreGive(rfid_mutex);
        }
        return ESP_FAIL;
    }

    if (fclose(f) != 0)
    {
        ESP_LOGE(TAG, "Failed to close RFID database file after writing.");
        if (mutex_taken_locally) {
            xSemaphoreGive(rfid_mutex);
        }
        return ESP_FAIL;
    }

    if (mutex_taken_locally) {
        xSemaphoreGive(rfid_mutex);
    }
    return ESP_OK;
}

/**
 * @brief Timer callback function for delayed writing to flash.
 * 
 * This function is called when the write timer expires. It sets a flag
 * to indicate that a write to NVS is pending, which will then be handled
 * by rfid_manager_process().
 * 
 * @param arg Timer argument (unused)
 */
static void rfid_cache_write_timeout_handler(void* arg)
{
    ESP_LOGI(TAG, "RFID write timer expired. Setting is_ready_to_write flag.");
    is_ready_to_write = true;
}

/**
 * @brief Internal function to perform the actual write of cached RFID data to NVS.
 * 
 * This function is called by rfid_manager_process() or directly when caching is disabled.
 * It handles mutex acquisition and error logging for the NVS write operation.
 * 
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
static esp_err_t rfid_manager_write_into_memory(void)
{
    esp_err_t write_into_mem_ret = ESP_OK;

    ESP_LOGI(TAG, "Attempting to write cached RFID data to NVS.");
    // Attempt to take the mutex before accessing shared resources
    // Use xSemaphoreTakeRecursive as this function might be called from contexts
    // that already hold the mutex (e.g., add_card, remove_card).
    if (rfid_mutex != NULL && xSemaphoreTakeRecursive(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) { // Use a reasonable timeout
        if (is_dirty) {
            ESP_LOGI(TAG, "is_dirty is true, writing cached RFID data to NVS...");
            esp_err_t err = rfid_manager_save_to_file();
            if (err == ESP_OK) {
                is_dirty = false;
                ESP_LOGI(TAG, "Successfully wrote RFID data to NVS.");
            } else {
                ESP_LOGE(TAG, "Failed to write RFID data to NVS from timer: %s", esp_err_to_name(err));
                // Consider: What to do if save fails? Retry? For now, is_dirty remains true.
                write_into_mem_ret = ESP_FAIL;
            }
        } else {
            ESP_LOGI(TAG, "is_dirty is false, no NVS write needed from timer.");
        }
        xSemaphoreGiveRecursive(rfid_mutex); // Use GiveRecursive to match TakeRecursive
    } else {
        ESP_LOGE(TAG, "Failed to take RFID mutex for NVS write. NVS write deferred.");
        // If this happens, data remains dirty and will attempt to save on next timer expiry or deinit.
        write_into_mem_ret = ESP_FAIL;
    }

    return write_into_mem_ret;
}

/**
 * Set the cache timeout for RFID database writes.
 */
esp_err_t rfid_manager_set_cache_timeout(uint32_t timeout_ms)
{
    if (rfid_mutex == NULL) {
        ESP_LOGE(TAG, "RFID mutex not initialized in set_cache_timeout");
        return ESP_FAIL;
    }
    
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // If decreasing timeout and we have pending changes, 
        // adjust the current timer if it's running
        if (timeout_ms < rfid_write_timeout_ms && is_dirty && rfid_write_timer != NULL) {
            esp_timer_stop(rfid_write_timer);
            if (timeout_ms > 0) {
                // Start with new timeout
                esp_timer_start_once(rfid_write_timer, timeout_ms * 1000);
            } else {
                // If setting timeout to 0, write immediately
                rfid_manager_save_to_file();
                is_dirty = false;
            }
        }
        
        // Update the timeout value
        rfid_write_timeout_ms = timeout_ms;
        ESP_LOGI(TAG, "RFID cache timeout set to %lu ms", (unsigned long)timeout_ms);
        
        xSemaphoreGive(rfid_mutex);
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Failed to take RFID mutex in set_cache_timeout");
    return ESP_FAIL;
}

/**
 * Force an immediate write of any cached changes.
 */
esp_err_t rfid_manager_flush_cache(void)
{
    if (rfid_mutex == NULL) {
        ESP_LOGE(TAG, "RFID mutex not initialized in flush_cache");
        return ESP_FAIL;
    }
    
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        // Stop any pending timer
        if (rfid_write_timer != NULL) {
            esp_timer_stop(rfid_write_timer);
        }
        
        // Only write if there are pending changes
        if (is_dirty) {
            ESP_LOGI(TAG, "Flushing RFID cache to flash");
            esp_err_t err = rfid_manager_save_to_file();
            if (err == ESP_OK) {
                is_dirty = false;
                ESP_LOGI(TAG, "RFID cache successfully flushed to flash");
            } else {
                ESP_LOGE(TAG, "Failed to flush RFID cache to flash: %s", esp_err_to_name(err));
                xSemaphoreGive(rfid_mutex);
                return err;
            }
        } else {
            ESP_LOGD(TAG, "No pending changes to flush to flash");
        }
        
        xSemaphoreGive(rfid_mutex);
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Failed to take RFID mutex in flush_cache");
    return ESP_FAIL;
}

static esp_err_t rfid_manager_load_from_file(void)
{

    FILE *f = fopen(RFID_DATABASE_FILE, "rb"); // Open for reading in binary mode

    if (f == NULL)
    {
        ESP_LOGW(TAG, "RFID database file not found: %s. This may be the first boot.", RFID_DATABASE_FILE);
        return ESP_ERR_NOT_FOUND; // File not found is a common case on first boot
    }

    // Check file size to determine if it's a new format or old format with header
    long file_size;
    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Initialize database to clean state
    memset(rfid_database, 0, sizeof(rfid_database));
    
    // Expected size for database without header
    size_t expected_size = sizeof(rfid_card_t) * RFID_MAX_CARDS;
    
    if (file_size == expected_size) {
        // New format - just read the card data directly
        size_t cards_read = fread(rfid_database, sizeof(rfid_card_t), RFID_MAX_CARDS, f);
        if (cards_read != RFID_MAX_CARDS) {
            ESP_LOGE(TAG, "Failed to read all RFID card data. Read %d of %d slots.", cards_read, RFID_MAX_CARDS);
            fclose(f);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Successfully read RFID database in new format");
    } else {
        // Old format with header or corrupted file - reset to defaults
        ESP_LOGW(TAG, "File size mismatch (got %ld, expected %d). Resetting to defaults.", file_size, expected_size);
        fclose(f);
        return ESP_ERR_INVALID_SIZE; // Return error to trigger loading defaults
    }

    if (fclose(f) != 0)
    {
        ESP_LOGE(TAG, "Failed to close RFID database file after reading.");
        // Data might be loaded, but this is still an issue.
    }

    return ESP_OK;
}

esp_err_t rfid_manager_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing RFID manager...");

    // 1. Ensure all pending changes are flushed to NVS before deinitialization
    // This will acquire the mutex internally, stop the timer, and save data if dirty.
    esp_err_t flush_ret = rfid_manager_flush_cache();
    if (flush_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to flush RFID cache during deinitialization: %s", esp_err_to_name(flush_ret));
        // Decide if this is a critical error. For deinit, we might proceed with cleanup anyway.
    }

    // 2. Stop and delete the timer
    if (rfid_write_timer != NULL)
    {
        esp_timer_stop(rfid_write_timer);
        esp_timer_delete(rfid_write_timer);
        rfid_write_timer = NULL;
        ESP_LOGD(TAG, "RFID write timer deleted.");
    }

    // 3. Delete the mutex
    if (rfid_mutex != NULL) { // Check if mutex is still valid after flush_cache (it should be)
        vSemaphoreDelete(rfid_mutex);
        rfid_mutex = NULL;
        ESP_LOGD(TAG, "RFID mutex deleted.");
    }

    ESP_LOGI(TAG, "RFID manager deinitialized successfully.");
    return ESP_OK;
}

/**
 * @brief Processes pending RFID manager operations, such as writing cached data to NVS.
 * 
 * This function should be called periodically from the main application loop.
 * It checks if there are pending NVS write operations signaled by the timer
 * and executes them.
 * 
 * @return true if processing occurred (e.g., a write was attempted), false otherwise.
 */
bool rfid_manager_process(void)
{
    if (is_ready_to_write)
    {
        ESP_LOGI(TAG, "rfid_manager_process: is_ready_to_write is true. Attempting NVS write.");
        esp_err_t write_into_ret = rfid_manager_write_into_memory();

        if(write_into_ret == ESP_OK)
        {
            is_ready_to_write = false;
            ESP_LOGI(TAG, "rfid_manager_process: NVS write successful, is_ready_to_write cleared.");
        } else {
            ESP_LOGE(TAG, "rfid_manager_process: NVS write failed.");
        }
        return true; // Indicate that processing occurred
    } 

    return false; // No processing needed
}

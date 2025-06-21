#ifndef RFID_MANAGER_H
#define RFID_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define RFID_MAX_CARDS 200
#define RFID_CARD_NAME_LEN 32
#define RFID_DEFAULT_CACHE_TIMEOUT_MS 5000  // Default 5 seconds cache timeout

// RFID card structure
// Size: 4 (card_id) + 1 (active) + 32 (name) + 4 (timestamp) = 41 bytes
typedef struct {
    uint32_t card_id;     // 32-bit RFID card number
    uint8_t active;       // Card status (1=active, 0=inactive/removed)
    char name[RFID_CARD_NAME_LEN]; // Card holder name (ensure null-termination if shorter)
    uint32_t timestamp;   // Last access timestamp (0 if not used)
} rfid_card_t;

esp_err_t rfid_manager_get_card(uint32_t card_id, rfid_card_t *card);

int testable_mean(const int *values, int count);
/**
 * @brief Initializes the RFID manager.
 *
 * This function should be called once at startup. It will:
 * 1. Ensure the SPIFFS filesystem is mounted (usually handled by spi_ffs_storage component).
 * 2. Attempt to load RFID cards from the persistent storage file.
 * 3. If the file doesn't exist, is empty, or corrupted (checksum mismatch),
 *    it will initialize the database with default cards.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t rfid_manager_init(void);

/**
 * @brief Adds a new RFID card to the database.
 *
 * Adds the given card_id and name to the list of authorized cards.
 * If the card_id already exists, its name and active status might be updated.
 * If the database is full, an error will be returned.
 * Changes are persisted to the storage file.
 *
 * @param card_id The 32-bit ID of the RFID card to add.
 * @param name Pointer to a string containing the name associated with the card.
 *             The name will be truncated if longer than RFID_CARD_NAME_LEN-1.
 * @return esp_err_t ESP_OK on success, ESP_ERR_NO_MEM if the database is full,
 *         ESP_FAIL for other errors.
 */
esp_err_t rfid_manager_add_card(uint32_t card_id, const char* name);

/**
 * @brief Removes an RFID card from the database.
 *
 * Marks the card with the given card_id as inactive.
 * The card data might remain in the file but will be treated as invalid.
 * Changes are persisted to the storage file.
 *
 * @param card_id The 32-bit ID of the RFID card to remove.
 * @return esp_err_t ESP_OK if the card was found and marked inactive,
 *         ESP_ERR_NOT_FOUND if the card_id does not exist,
 *         ESP_FAIL for other errors.
 */
esp_err_t rfid_manager_remove_card(uint32_t card_id);

/**
 * @brief Checks if an RFID card is authorized.
 *
 * Verifies if the given card_id exists in the database and is marked as active.
 *
 * @param card_id The 32-bit ID of the RFID card to check.
 * @return true if the card is authorized and active, false otherwise.
 */
bool rfid_manager_check_card(uint32_t card_id);

/**
 * @brief Gets the total number of active RFID cards in the database.
 *
 * @return uint16_t The count of currently active RFID cards.
 */
uint16_t rfid_manager_get_card_count(void);

/**
 * @brief Retrieves a list of all active RFID cards.
 *
 * Populates the provided 'cards_buffer' with data of all active RFID cards.
 *
 * @param cards_buffer Pointer to an array of rfid_card_t structures to store the card data.
 * @param buffer_size The maximum number of rfid_card_t elements the cards_buffer can hold.
 *                    It's recommended this be at least RFID_MAX_CARDS.
 * @param num_cards_copied Pointer to a uint16_t that will be filled with the number of cards actually copied to the buffer.
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG if buffer is NULL or num_cards_copied is NULL.
 */
esp_err_t rfid_manager_list_cards(rfid_card_t *cards_buffer, uint16_t buffer_size, uint16_t *num_cards_copied);

/**
 * @brief Formats the RFID database.
 *
 * Clears all existing RFID cards from the database and re-initializes it
 * by loading the default set of cards.
 * This operation is destructive to existing card data.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t rfid_manager_format_database(void);


esp_err_t rfid_manager_get_card_list_json(char* buffer, size_t bufferLength);

/**
 * @brief Set the cache timeout for RFID database writes.
 *
 * Sets the time in milliseconds after which pending changes to the RFID database
 * will be written to flash storage. Multiple card operations within this timeout
 * will be coalesced into a single write operation to reduce flash wear.
 *
 * @param timeout_ms The timeout in milliseconds. Set to 0 to disable caching.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t rfid_manager_set_cache_timeout(uint32_t timeout_ms);

/**
 * @brief Force an immediate write of any cached changes to flash storage.
 *
 * This function can be used to ensure all changes are persisted immediately,
 * for example before system shutdown or when immediate persistence is required.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t rfid_manager_flush_cache(void);

// Custom error codes for RFID Manager
// ESP-IDF typically uses 0x1000 range for component-specific errors
#define RFID_MANAGER_ERR_BASE           0x1000  // Base error code for RFID manager
#define RFID_MANAGER_ERR_DUPLICATE_ID   (RFID_MANAGER_ERR_BASE + 1)  // Error code for duplicate ID

#endif // RFID_MANAGER_H

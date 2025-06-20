/* RFID Manager Unit Tests
 *
 * Comprehensive test suite for the RFID Manager component.
 * Tests card management operations, error handling, and recovery scenarios.
 */

#include <limits.h>
#include <string.h>
#include "unity.h"
#include "rfid_manager.h"

#define countof(x) (sizeof(x) / sizeof(x[0]))

// Test initialization of RFID Manager
TEST_CASE("RFID Manager Initialization", "[RFID]")
{
    esp_err_t ret = rfid_manager_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

// Test adding a single card
TEST_CASE("Adding Single Card", "[RFID]")
{
    // Format database to start with a clean state
    esp_err_t ret = rfid_manager_format_database();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Add a test card
    ret = rfid_manager_add_card(0xABCD1234, "Test Card One");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify card count increased
    uint16_t count = rfid_manager_get_card_count();
    TEST_ASSERT_GREATER_THAN(0, count); // Should have at least one card (may include default cards)
}

// Test retrieving a card
TEST_CASE("Retrieving Card", "[RFID]")
{
    // Add a card with known values
    uint32_t test_card_id = 0x55667788;
    const char* test_card_name = "Retrieval Test Card";
    
    esp_err_t ret = rfid_manager_add_card(test_card_id, test_card_name);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Retrieve the card
    rfid_card_t card;
    ret = rfid_manager_get_card(test_card_id, &card);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify card data
    TEST_ASSERT_EQUAL_UINT32(test_card_id, card.card_id);
    TEST_ASSERT_EQUAL_STRING(test_card_name, card.name);
    TEST_ASSERT_EQUAL_UINT8(1, card.active);
}

// Test card removal
TEST_CASE("Removing Card", "[RFID]")
{
    // Add a card to be removed
    uint32_t card_to_remove = 0x99887766;
    esp_err_t ret = rfid_manager_add_card(card_to_remove, "Card To Remove");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Get initial card count
    uint16_t initial_count = rfid_manager_get_card_count();
    
    // Verify card exists
    TEST_ASSERT_TRUE(rfid_manager_check_card(card_to_remove));
    
    // Remove the card
    ret = rfid_manager_remove_card(card_to_remove);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify card no longer exists
    TEST_ASSERT_FALSE(rfid_manager_check_card(card_to_remove));
    
    // Verify card count decreased
    uint16_t new_count = rfid_manager_get_card_count();
    TEST_ASSERT_EQUAL(initial_count - 1, new_count);
    
    // Try to get the removed card - should fail
    rfid_card_t card;
    ret = rfid_manager_get_card(card_to_remove, &card);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret);
}

// Test checking if a card exists
TEST_CASE("Checking Card Existence", "[RFID]")
{
    // Add a card to check
    uint32_t existing_card = 0x11223344;
    uint32_t nonexistent_card = 0x55667788;
    
    // First make sure the nonexistent card is really removed (in case previous tests added it)
    rfid_manager_remove_card(nonexistent_card);
    
    // Add the card we want to exist
    esp_err_t ret = rfid_manager_add_card(existing_card, "Existing Card");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check the existing card
    TEST_ASSERT_TRUE(rfid_manager_check_card(existing_card));
    
    // Check the nonexistent card
    TEST_ASSERT_FALSE(rfid_manager_check_card(nonexistent_card));
}

// Test listing cards
TEST_CASE("Listing Cards", "[RFID]")
{
    // Format database to start with a clean state
    esp_err_t ret = rfid_manager_format_database();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Add multiple cards
    const uint32_t test_cards[] = {0xAABBCCDD, 0xEEFF0011, 0x22334455};
    const char* test_names[] = {"Card One", "Card Two", "Card Three"};
    const int num_test_cards = countof(test_cards);
    
    for (int i = 0; i < num_test_cards; i++) {
        ret = rfid_manager_add_card(test_cards[i], test_names[i]);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
    }
    
    // Get the current count
    uint16_t card_count = rfid_manager_get_card_count();
    
    // The count should be at least the number of test cards we added
    // (might be more if there are default cards)
    TEST_ASSERT_GREATER_OR_EQUAL(num_test_cards, card_count);
    
    // List the cards
    rfid_card_t cards_buffer[RFID_MAX_CARDS];
    uint16_t num_cards_copied = 0;
    
    ret = rfid_manager_list_cards(cards_buffer, RFID_MAX_CARDS, &num_cards_copied);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(card_count, num_cards_copied);
    
    // Verify our test cards are in the list
    bool found_cards[num_test_cards];
    memset(found_cards, 0, sizeof(found_cards));
    
    for (int i = 0; i < num_cards_copied; i++) {
        for (int j = 0; j < num_test_cards; j++) {
            if (cards_buffer[i].card_id == test_cards[j]) {
                TEST_ASSERT_EQUAL_STRING(test_names[j], cards_buffer[i].name);
                found_cards[j] = true;
            }
        }
    }
    
    // Make sure all our test cards were found
    for (int j = 0; j < num_test_cards; j++) {
        TEST_ASSERT_TRUE(found_cards[j]);
    }
}

// Test JSON format for card list
TEST_CASE("JSON Card List", "[RFID]")
{
    // Format database to start with a clean state
    esp_err_t ret = rfid_manager_format_database();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Add a known card
    uint32_t test_card_id = 0x12345678;
    const char* test_card_name = "JSON Test Card";
    
    ret = rfid_manager_add_card(test_card_id, test_card_name);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Get the JSON string
    char json_buffer[1024];
    ret = rfid_manager_get_card_list_json(json_buffer, sizeof(json_buffer));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify the JSON contains our card ID and name
    char card_id_str[16];
    snprintf(card_id_str, sizeof(card_id_str), "%lu", (unsigned long)test_card_id);
    
    TEST_ASSERT_NOT_NULL(strstr(json_buffer, card_id_str));
    TEST_ASSERT_NOT_NULL(strstr(json_buffer, test_card_name));
}

// Test handling of invalid parameters
TEST_CASE("Invalid Parameters", "[RFID]")
{
    // Test adding a card with NULL name
    esp_err_t ret = rfid_manager_add_card(0x12345678, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test getting a card with NULL output buffer
    ret = rfid_manager_get_card(0x12345678, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test listing cards with NULL buffer
    uint16_t num_cards;
    ret = rfid_manager_list_cards(NULL, 10, &num_cards);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test listing cards with NULL count output
    rfid_card_t cards[10];
    ret = rfid_manager_list_cards(cards, 10, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

// Test storage overflow handling
TEST_CASE("Storage Overflow Handling", "[RFID]")
{
    // Format database to start with a clean state
    esp_err_t ret = rfid_manager_format_database();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Fill the database to capacity with unique card IDs
    for (uint32_t i = 0; i < RFID_MAX_CARDS; i++) {
        char name[RFID_CARD_NAME_LEN];
        snprintf(name, sizeof(name), "Overflow Test Card %lu", (unsigned long)i);
        
        // Use unique IDs starting from a specific base
        uint32_t card_id = 0xF0000000 + i;
        
        ret = rfid_manager_add_card(card_id, name);
        
        // Early cards should be added successfully
        if (i < RFID_MAX_CARDS - 3) { // Accounting for default cards that might be there
            TEST_ASSERT_EQUAL(ESP_OK, ret);
        }
    }
    
    // Verify we have the maximum number of cards
    uint16_t count = rfid_manager_get_card_count();
    TEST_ASSERT_EQUAL(RFID_MAX_CARDS, count);
    
    // Try to add one more card - should fail with NO_MEM error
    ret = rfid_manager_add_card(0xFFFFFFFF, "Overflow Card");
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, ret);
    
    // Verify count didn't change
    count = rfid_manager_get_card_count();
    TEST_ASSERT_EQUAL(RFID_MAX_CARDS, count);
    
    // Remove one card to make space
    rfid_card_t cards[RFID_MAX_CARDS];
    uint16_t num_cards_copied;
    ret = rfid_manager_list_cards(cards, RFID_MAX_CARDS, &num_cards_copied);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    if (num_cards_copied > 0) {
        ret = rfid_manager_remove_card(cards[0].card_id);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        
        // Now we should be able to add a new card
        ret = rfid_manager_add_card(0xFFFFFFFF, "Replacement Card");
        TEST_ASSERT_EQUAL(ESP_OK, ret);
    }
}

// Clean up the database after massive tests
TEST_CASE("Cleanup After Tests", "[RFID]")
{
    // Format database to reset to defaults
    esp_err_t ret = rfid_manager_format_database();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify we're back to just the default cards
    uint16_t count = rfid_manager_get_card_count();
    TEST_ASSERT_LESS_THAN(10, count); // Should be just the default cards (typically 3)
}

// Legacy tests kept for compatibility
TEST_CASE("Mean of an empty array is zero", "[RFID]")
{
    const int values[] = {0};
    TEST_ASSERT_EQUAL(0, testable_mean(values, 0));
}

TEST_CASE("Mean of a test vector", "[RFID]")
{
    const int v[] = {1, 3, 5, 7, 9};
    TEST_ASSERT_EQUAL(5, testable_mean(v, countof(v)));
}

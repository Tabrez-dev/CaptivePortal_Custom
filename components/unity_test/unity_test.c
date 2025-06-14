#include "unity_test.h"
#include "esp_log.h"
#include "string.h"
#include <dirent.h>

static const char *TAG = "UNITY_TEST";

// Test file paths
static const char *test_file = "/spiffs/test_file.txt";
static const char *test_file2 = "/spiffs/test_file2.txt";
static const char *test_rename = "/spiffs/renamed_file.txt";
static const char *nonexistent_file = "/spiffs/nonexistent.txt";

// Test data
static const char *test_data = "This is a test string";
static const char *test_data2 = "This is another test string";

// Helper function to clean up test files
static void cleanup_test_files(void) {
    spiffs_storage_delete_file(test_file);
    spiffs_storage_delete_file(test_file2);
    spiffs_storage_delete_file(test_rename);
    spiffs_storage_delete_file(nonexistent_file);
}

// Helper function to verify file content
static void verify_file_content(const char *filename, const char *expected_content) {
    char read_buffer[512] = {0};
    TEST_ASSERT_TRUE_MESSAGE(spiffs_storage_read_file(filename, read_buffer, sizeof(read_buffer)), 
                           "Failed to read file for content verification");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected_content, read_buffer, 
                                   "File content doesn't match expected value");
}

// Test case: File creation and existence check
static void test_file_creation(void) {
    cleanup_test_files();
    
    // Test file creation
    TEST_ASSERT_TRUE_MESSAGE(spiffs_storage_create_file(test_file), 
                           "Failed to create test file");
    TEST_ASSERT_TRUE_MESSAGE(spiffs_storage_file_exists(test_file), 
                           "Created file doesn't exist");
    
    // Verify file is empty
    int32_t size = spiffs_storage_get_file_size(test_file);
    TEST_ASSERT_EQUAL_MESSAGE(0, size, "New file should be empty");
    
    cleanup_test_files();
}

// Test case: File writing and reading
static void test_file_write_read(void) {
    cleanup_test_files();
    
    // Write to file
    TEST_ASSERT_TRUE_MESSAGE(spiffs_storage_write_file(test_file, test_data, false), 
                           "Failed to write to file");
    
    // Verify content and size
    verify_file_content(test_file, test_data);
    int32_t size = spiffs_storage_get_file_size(test_file);
    TEST_ASSERT_EQUAL_MESSAGE(strlen(test_data), size, "File size doesn't match content length");
    
    cleanup_test_files();
}

// Test case: File appending
static void test_file_append(void) {
    cleanup_test_files();
    
    // Write initial data
    TEST_ASSERT_TRUE(spiffs_storage_write_file(test_file, test_data, false));
    
    // Append more data
    TEST_ASSERT_TRUE(spiffs_storage_write_file(test_file, test_data2, true));
    
    // Verify combined content
    char expected[512];
    snprintf(expected, sizeof(expected), "%s%s", test_data, test_data2);
    verify_file_content(test_file, expected);
    
    cleanup_test_files();
}

// Test case: File renaming
static void test_file_rename(void) {
    cleanup_test_files();
    
    // Create and write to test file
    TEST_ASSERT_TRUE(spiffs_storage_write_file(test_file, test_data, false));
    
    // Rename the file
    TEST_ASSERT_TRUE(spiffs_storage_rename_file(test_file, test_rename));
    
    // Verify the old file doesn't exist and new one has the content
    TEST_ASSERT_FALSE(spiffs_storage_file_exists(test_file));
    TEST_ASSERT_TRUE(spiffs_storage_file_exists(test_rename));
    verify_file_content(test_rename, test_data);
    
    // Test renaming to existing file (should fail)
    TEST_ASSERT_TRUE(spiffs_storage_write_file(test_file, test_data2, false));
    TEST_ASSERT_FALSE(spiffs_storage_rename_file(test_rename, test_file));
    
    cleanup_test_files();
}

// Test case: File deletion
static void test_file_deletion(void) {
    cleanup_test_files();
    
    // Create a test file
    TEST_ASSERT_TRUE(spiffs_storage_write_file(test_file, test_data, false));
    
    // Delete the file
    TEST_ASSERT_TRUE(spiffs_storage_delete_file(test_file));
    
    // Verify it's gone
    TEST_ASSERT_FALSE(spiffs_storage_file_exists(test_file));
    
    // Test deleting non-existent file
    TEST_ASSERT_FALSE(spiffs_storage_delete_file(nonexistent_file));
    
    cleanup_test_files();
}

// Test case: File size check
static void test_file_size(void) {
    cleanup_test_files();
    
    // Test empty file
    TEST_ASSERT_TRUE(spiffs_storage_create_file(test_file));
    TEST_ASSERT_EQUAL(0, spiffs_storage_get_file_size(test_file));
    
    // Test with content
    TEST_ASSERT_TRUE(spiffs_storage_write_file(test_file2, test_data, false));
    TEST_ASSERT_EQUAL(strlen(test_data), spiffs_storage_get_file_size(test_file2));
    
    // Test non-existent file
    TEST_ASSERT_EQUAL(-1, spiffs_storage_get_file_size(nonexistent_file));
    
    cleanup_test_files();
}

// Test case: Error cases
static void test_error_cases(void) {
    cleanup_test_files();
    
    // Test reading from non-existent file
    char buffer[256] = {0};
    TEST_ASSERT_FALSE(spiffs_storage_read_file(nonexistent_file, buffer, sizeof(buffer)));
    
    // Test reading with NULL buffer
    TEST_ASSERT_FALSE(spiffs_storage_read_file(test_file, NULL, 100));
    
    // Test reading with zero buffer size
    TEST_ASSERT_FALSE(spiffs_storage_read_file(test_file, buffer, 0));
    
    // Test writing with NULL data
    TEST_ASSERT_FALSE(spiffs_storage_write_file(test_file, NULL, false));
    
    // Test creating file with NULL filename
    TEST_ASSERT_FALSE(spiffs_storage_create_file(NULL));
    
    cleanup_test_files();
}

// Test case: File listing
static void test_file_listing(void) {
    cleanup_test_files();
    
    // Create test files
    TEST_ASSERT_TRUE(spiffs_storage_create_file(test_file));
    TEST_ASSERT_TRUE(spiffs_storage_create_file(test_file2));
    
    // Get file list
    TEST_ASSERT_TRUE(spiffs_storage_list_files());
    
    cleanup_test_files();
}

// Test case: File content verification
static void test_file_content_verification(void) {
    cleanup_test_files();
    
    const char *content = "Line 1\nLine 2\nLine 3";
    
    // Write multi-line content
    TEST_ASSERT_TRUE(spiffs_storage_write_file(test_file, content, false));
    
    // Verify content using read_file_line
    FILE *f = fopen(test_file, "r");
    TEST_ASSERT_NOT_NULL(f);
    
    char line[256];
    int line_num = 0;
    while (fgets(line, sizeof(line), f) != NULL) {
        line_num++;
        char expected[32];
        snprintf(expected, sizeof(expected), "Line %d", line_num);
        // Remove newline if present
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        TEST_ASSERT_EQUAL_STRING(expected, line);
    }
    
    fclose(f);
    TEST_ASSERT_EQUAL(3, line_num);
    
    cleanup_test_files();
}

// Main test runner
void run_spiffs_storage_tests(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Starting SPIFFS storage test suite");
    ESP_LOGI(TAG, "========================================\n");
    
    // Initialize SPIFFS for testing
    spiffs_storage_init();
    
    // Run test cases
    RUN_TEST(test_file_creation);
    RUN_TEST(test_file_write_read);
    RUN_TEST(test_file_append);
    RUN_TEST(test_file_rename);
    RUN_TEST(test_file_deletion);
    RUN_TEST(test_file_size);
    RUN_TEST(test_error_cases);
    RUN_TEST(test_file_listing);
    RUN_TEST(test_file_content_verification);
    
    // Clean up SPIFFS
    spiffs_storage_deinit();
    
    ESP_LOGI(TAG, "\n========================================");
    ESP_LOGI(TAG, "SPIFFS storage test suite completed");
    ESP_LOGI(TAG, "========================================");
}
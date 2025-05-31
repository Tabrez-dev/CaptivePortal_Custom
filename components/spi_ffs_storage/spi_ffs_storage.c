#include "spi_ffs_storage.h"

#define TAG "SPIFFS_STORAGE"

void spiffs_storage_init(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    ESP_LOGI(TAG, "Performing SPIFFS_check().");
    ret = esp_spiffs_check(conf.partition_label);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
        return;
    }
    else
    {
        ESP_LOGI(TAG, "SPIFFS_check() successful");
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s). Formatting...", esp_err_to_name(ret));
        esp_spiffs_format(conf.partition_label);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    // Check consistency of reported partiton size info.
    if (used > total)
    {
        ESP_LOGW(TAG, "Number of used bytes cannot be larger than total. Performing SPIFFS_check().");
        ret = esp_spiffs_check(conf.partition_label);
        // Could be also used to mend broken files, to clean unreferenced pages, etc.
        // More info at https://github.com/pellepl/spiffs/wiki/FAQ#powerlosses-contd-when-should-i-run-spiffs_check
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
            return;
        }
        else
        {
            ESP_LOGI(TAG, "SPIFFS_check() successful");
        }
    }

    spiffs_storage_list_files();
    
}

void spiffs_storage_test(void)
{
    ESP_LOGI(TAG, "[%s] Starting SPIFFS storage test suite", __func__);
    
    // 1. Test file creation
    ESP_LOGI(TAG, "[%s]\n=== 1. Testing file creation ===", __func__);
    const char *test_file = "/spiffs/test_file.txt";
    const char *test_file2 = "/spiffs/test_file2.txt";
    const char *test_rename = "/spiffs/renamed_file.txt";
    
    // Clean up any existing test files
    ESP_LOGI(TAG, "[%s] Cleaning up any existing test files...", __func__);
    spiffs_storage_delete_file(test_file);
    spiffs_storage_delete_file(test_file2);
    spiffs_storage_delete_file(test_rename);
    
    // 1. Test create_file
    ESP_LOGI(TAG, "[%s] 1. Testing spiffs_storage_create_file...", __func__);
    bool result = spiffs_storage_create_file(test_file);
    ESP_LOGI(TAG, "[%s] 1. Create file %s: %s", __func__, test_file, result ? "SUCCESS" : "FAILED");
    
    // 2. Test file_exists
    ESP_LOGI(TAG, "[%s]\n=== 2. Testing file existence ===", __func__);
    bool exists = spiffs_storage_file_exists(test_file);
    ESP_LOGI(TAG, "[%s] 2. File %s exists: %s", __func__, test_file, exists ? "YES" : "NO");
    
    // 3. Test file size
    ESP_LOGI(TAG, "[%s]\n=== 3. Testing file size ===", __func__);
    int32_t file_size = spiffs_storage_get_file_size(test_file);
    ESP_LOGI(TAG, "[%s] 3. File size: %ld bytes", __func__, (long)file_size);
    
    // 4. Test writing to file (overwrite)
    ESP_LOGI(TAG, "[%s]\n=== 4. Testing file writing (overwrite) ===", __func__);
    const char *test_data = "This is a test line 1\nThis is a test line 2\n";
    result = spiffs_storage_write_file(test_file, test_data, false);
    ESP_LOGI(TAG, "[%s] 4. Write to file (overwrite): %s", __func__, result ? "SUCCESS" : "FAILED");
    
    // 5. Test appending to file
    ESP_LOGI(TAG, "[%s]\n=== 5. Testing file appending ===", __func__);
    const char *append_data = "This is an appended line\n";
    result = spiffs_storage_write_file(test_file, append_data, true);
    ESP_LOGI(TAG, "[%s] 5. Append to file: %s", __func__, result ? "SUCCESS" : "FAILED");
    
    // 6. Test reading entire file
    ESP_LOGI(TAG, "[%s]\n=== 6. Testing file reading ===", __func__);
    char read_buffer[256] = {0};
    result = spiffs_storage_read_file(test_file, read_buffer, sizeof(read_buffer));
    ESP_LOGI(TAG, "[%s] 6. Read file %s: %s", __func__, test_file, result ? "SUCCESS" : "FAILED");
    if (result) {
        ESP_LOGI(TAG, "[%s] 6. File content:\n%s", __func__, read_buffer);
    }
    
    // 7. Test reading first line using spiffs_storage_read_file_line
    ESP_LOGI(TAG, "[%s]\n=== 7. Testing spiffs_storage_read_file_line ===", __func__);
    char line_buffer[128] = {0};
    
    bool read_result = spiffs_storage_read_file_line(test_file, line_buffer, sizeof(line_buffer));
    if (read_result) {
        ESP_LOGI(TAG, "[%s] 7. First line: %s", __func__, line_buffer);
    } else {
        ESP_LOGE(TAG, "[%s] 7. Failed to read first line using spiffs_storage_read_file_line", __func__);
    }
    
    // 8. Test file renaming
    ESP_LOGI(TAG, "[%s]\n=== 8. Testing file renaming ===", __func__);
    result = spiffs_storage_rename_file(test_file, test_rename);
    ESP_LOGI(TAG, "[%s] 8. Rename %s to %s: %s", __func__, test_file, test_rename, result ? "SUCCESS" : "FAILED");
    
    // 9. Verify rename by checking existence
    exists = spiffs_storage_file_exists(test_rename);
    ESP_LOGI(TAG, "[%s] 9. Renamed file exists: %s", __func__, exists ? "YES" : "NO");
    
    // 10. Test file listing
    ESP_LOGI(TAG, "[%s]\n=== 10. Testing file listing ===", __func__);
    spiffs_storage_list_files();
    
    // 11. Test file deletion
    ESP_LOGI(TAG, "[%s]\n=== 11. Testing file deletion ===", __func__);
    result = spiffs_storage_delete_file(test_rename);
    ESP_LOGI(TAG, "[%s] 11. Delete file %s: %s", __func__, test_rename, result ? "SUCCESS" : "FAILED");
    
    // 12. Verify deletion
    exists = spiffs_storage_file_exists(test_rename);
    ESP_LOGI(TAG, "[%s] 12. File still exists after deletion: %s", __func__, exists ? "YES (ERROR)" : "NO (CORRECT)");
    
    // Test error cases
    ESP_LOGI(TAG, "[%s]\n=== 13. Testing error cases ===", __func__);
    
    // 13.1 Test reading non-existent file
    ESP_LOGI(TAG, "[%s] 13.1 Testing read non-existent file...", __func__);
    result = spiffs_storage_read_file("/spiffs/nonexistent.txt", read_buffer, sizeof(read_buffer));
    ESP_LOGI(TAG, "[%s] 13.1 Read non-existent file: %s (expected to fail)", __func__, result ? "SUCCESS (UNEXPECTED)" : "FAILED (EXPECTED)");
    
    // 13.2 Test deleting non-existent file
    ESP_LOGI(TAG, "[%s] 13.2 Testing delete non-existent file...", __func__);
    result = spiffs_storage_delete_file("/spiffs/nonexistent.txt");
    ESP_LOGI(TAG, "[%s] 13.2 Delete non-existent file: %s (expected to fail)", __func__, result ? "SUCCESS (UNEXPECTED)" : "FAILED (EXPECTED)");
    
    // 13.3 Test renaming non-existent file
    ESP_LOGI(TAG, "[%s] 13.3 Testing rename non-existent file...", __func__);
    result = spiffs_storage_rename_file("/spiffs/nonexistent.txt", "/spiffs/new_name.txt");
    ESP_LOGI(TAG, "[%s] 13.3 Rename non-existent file: %s (expected to fail)", __func__, result ? "SUCCESS (UNEXPECTED)" : "FAILED (EXPECTED)");
    
    // 14. Test NULL/invalid parameters
    ESP_LOGI(TAG, "[%s]\n=== 14. Testing NULL/invalid parameters ===", __func__);
    
    // 14.1 Test create with NULL filename
    ESP_LOGI(TAG, "[%s] 14.1 Testing create with NULL filename...", __func__);
    result = spiffs_storage_create_file(NULL);
    ESP_LOGI(TAG, "[%s] 14.1 Create with NULL filename: %s (EXPECTED TO FAIL)", __func__, result ? "SUCCESS" : "FAILED (EXPECTED)");
    if (!result) {
        ESP_LOGI(TAG, "[%s] 14.1 Correctly handled NULL filename parameter", __func__);
    }
    
    // 14.2 Test read with NULL filename
    ESP_LOGI(TAG, "[%s] 14.2 Testing read with NULL filename...", __func__);
    result = spiffs_storage_read_file(NULL, read_buffer, sizeof(read_buffer));
    ESP_LOGI(TAG, "[%s] 14.2 Read file with NULL name: %s (expected to fail)", __func__, result ? "SUCCESS (UNEXPECTED)" : "FAILED (EXPECTED)");
    
    // 14.3 Test write with NULL data
    ESP_LOGI(TAG, "[%s] 14.3 Testing write with NULL data...", __func__);
    result = spiffs_storage_write_file(test_file, NULL, false);
    ESP_LOGI(TAG, "[%s] 14.3 Write NULL data to file: %s (expected to fail)", __func__, result ? "SUCCESS (UNEXPECTED)" : "FAILED (EXPECTED)");
    
    ESP_LOGI(TAG, "[%s]\n=== SPIFFS storage test suite completed ===", __func__);
}

void spiffs_storage_deinit(void)
{
    esp_err_t ret = esp_vfs_spiffs_unregister(NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to unregister SPIFFS (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "SPIFFS unmounted");
    }
}

bool spiffs_storage_create_file(const char *filename)
{
    if (filename == NULL) {
        ESP_LOGE(TAG, "Cannot create file: filename is NULL");
        return false;
    }
    
    FILE *f = fopen(filename, "w");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to create file: %s", filename);
        return false;
    }
    ESP_LOGI(TAG, "File created successfully: %s", filename);
    fclose(f);
    return true;
}

bool spiffs_storage_list_files(void)
{
    // List files in the SPIFFS filesystem
    ESP_LOGI(TAG, "Listing files in SPIFFS");
    // Open the directory
    DIR *dir = opendir("/spiffs");

    if (dir == NULL)
    {
        ESP_LOGE(TAG, "Failed to open directory");
        return false;
    }

    struct dirent *entry;
    // Iterate through the directory entries
    while ((entry = readdir(dir)) != NULL)
    {
        ESP_LOGI(TAG, "%s Found file: %s",__func__, entry->d_name);
    }

    closedir(dir);

    return true;
}

bool spiffs_storage_file_exists(const char *filename)
{
    struct stat st;

    // Check if the file exists by trying to get its status
    if (stat(filename, &st) != 0)
    {
        ESP_LOGI(TAG, "File does not exist: %s", filename);
        return false;
    }
    ESP_LOGI(TAG, "File exists: %s", filename);

    return true;
}

int32_t spiffs_storage_get_file_size(const char *filename)
{
    struct stat st;

    // Call stat directly to fill the structure
    if (stat(filename, &st) != 0) {
        ESP_LOGE(TAG, "Failed to get file info: %s", filename);
        return -1;  // Error getting file information
    }
    
    ESP_LOGI(TAG, "File size of '%s': %d bytes", filename, (int)st.st_size);

    return (int32_t)st.st_size;
}

bool spiffs_storage_delete_file(const char *filename)
{
    if (!spiffs_storage_file_exists(filename))
    {
        return false;
    }

    // Attempt to delete the file
    if (unlink(filename) != 0)
    {
        ESP_LOGE(TAG, "Failed to delete file: %s", filename);
        return false;
    }

    ESP_LOGI(TAG, "File deleted successfully: %s", filename);

    return true;
}

bool spiffs_storage_rename_file(const char *old_filename, const char *new_filename)
{
    // Check if the old file exists
    struct stat st;

    if (stat(old_filename, &st) != 0)
    {
        ESP_LOGE(TAG, "Old file does not exist: %s", old_filename);
        return false; // Old file does not exist
    }

    // Check if the new file already exists
    if (stat(new_filename, &st) == 0)
    {
        ESP_LOGE(TAG, "New file already exists: %s", new_filename);
        return false; // New file already exists
    }

    // Rename the file
    if (rename(old_filename, new_filename) != 0)
    {
        ESP_LOGE(TAG, "Failed to rename file: %s", old_filename);
        return false;
    }

    ESP_LOGI(TAG, "File renamed successfully: %s -> %s", old_filename, new_filename);

    return true;
}

bool spiffs_storage_write_file(const char *filename, const char *data, bool append)
{
    if (filename == NULL) {
        ESP_LOGE(TAG, "Cannot write to file: filename is NULL");
        return false;
    }
    
    if (data == NULL) {
        ESP_LOGE(TAG, "Cannot write NULL data to file: %s", filename);
        return false;
    }
    
    FILE *f;
    if (append)
    {
        f = fopen(filename, "a"); // Open for appending
    }
    else
    {
        f = fopen(filename, "w"); // Open for writing (overwrite)
    }

    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filename);
        return false;
    }
    // Write data to the file
    fprintf(f, "%s", data);
    ESP_LOGI(TAG, "Writing to file: %s", filename);
    ESP_LOGI(TAG, "Data written to file: %s", data);

    fclose(f);
    return true;
}

bool spiffs_storage_read_file(const char *filename, char *buffer, size_t buffer_size)
{
    if (filename == NULL) {
        ESP_LOGE(TAG, "Cannot read file: filename is NULL");
        return false;
    }
    
    if (buffer == NULL || buffer_size == 0) {
        ESP_LOGE(TAG, "Invalid buffer or buffer size");
        return false;
    }
    
    FILE *f = fopen(filename, "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", filename);
        return false;
    }
    fread(buffer, 1, buffer_size, f);
    ESP_LOGI(TAG, "Reading file: %s", filename);
    ESP_LOGI(TAG, "Data read from file: %s", buffer);
    // Ensure the buffer is null-terminated
    buffer[buffer_size - 1] = '\0';
    fclose(f);
    return true;
}

bool spiffs_storage_read_file_line(const char *filename, char *buffer, size_t buffer_size)
{
    if (filename == NULL) {
        ESP_LOGE(TAG, "Cannot read file: filename is NULL");
        return false;
    }
    
    if (buffer == NULL || buffer_size == 0) {
        ESP_LOGE(TAG, "Invalid buffer or buffer size");
        return false;
    }
    
    FILE *f = fopen(filename, "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", filename);
        return false;
    }
    // Read a single line from the file
    if (fgets(buffer, buffer_size, f) == NULL)
    {
        ESP_LOGE(TAG, "Failed to read line from file: %s", filename);
        fclose(f);
        return false;
    }

    fclose(f);

    return true;
}

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
    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(TAG, "Opening file");
    FILE *f = fopen("/spiffs/hello.txt", "w");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello World!\n");
    fclose(f);
    ESP_LOGI(TAG, "File written");

    // Check if destination file exists before renaming
    struct stat st;
    if (stat("/spiffs/foo.txt", &st) == 0)
    {
        // Delete it if it exists
        unlink("/spiffs/foo.txt");
    }

    // Rename original file
    ESP_LOGI(TAG, "Renaming file");
    if (rename("/spiffs/hello.txt", "/spiffs/foo.txt") != 0)
    {
        ESP_LOGE(TAG, "Rename failed");
        return;
    }

    // Open renamed file for reading
    ESP_LOGI(TAG, "Reading file");
    f = fopen("/spiffs/foo.txt", "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }
    char line[64];
    fgets(line, sizeof(line), f);
    fclose(f);
    // strip newline
    char *pos = strchr(line, '\n');
    if (pos)
    {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line);
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
    FILE *f = fopen(filename, "w");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to create file");
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
        ESP_LOGI(TAG, "Found file: %s", entry->d_name);
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
    // // Check if the file exists
    // if (!spiffs_storage_file_exists(filename))
    // {
    //     return false;
    // }

    // ESP_LOGI(TAG, "File size of '%s': %d bytes", filename, (int)st.st_size);

    // if (st.st_size < 0)
    // {
    //     ESP_LOGE(TAG, "Error getting file size for '%s'", filename);
    //     return -1; // Error getting file size
    // }

    // return (int32_t)st.st_size;
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

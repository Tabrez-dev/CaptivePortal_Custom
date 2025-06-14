#ifndef SPIFFS_STORAGE_H
#define SPIFFS_STORAGE_H

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"

void spiffs_storage_init(void);
void spiffs_storage_deinit(void);
//nvs-dual copy
/**
 * -opening or creating a file: read-only, read-write, write-only
 * -reading a file - read-only
 * -writing a file - write-only, append, overwrite
 * -renaming a file - new name & old name
 * -deleting a file - name of the file
 * -checking if a file exists - name of the file
 * -checking file size - name of the file
 *
 */
bool spiffs_storage_file_exists(const char *filename);
int32_t spiffs_storage_get_file_size(const char *filename);
bool spiffs_storage_delete_file(const char *filename);
bool spiffs_storage_rename_file(const char *old_filename, const char *new_filename);
bool spiffs_storage_write_file(const char *filename, const char *data, bool append);
bool spiffs_storage_read_file(const char *filename, char *buffer, size_t buffer_size);
bool spiffs_storage_read_file_line(const char *filename, char *buffer, size_t buffer_size);
bool spiffs_storage_create_file(const char *filename);
bool spiffs_storage_list_files(void);

#endif // SPIFFS_STORAGE_H
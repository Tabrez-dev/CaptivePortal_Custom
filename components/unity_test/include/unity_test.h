#ifndef UNITY_TEST_H
#define UNITY_TEST_H

#include "unity.h"
#include "spi_ffs_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Runs all SPIFFS storage tests
 * 
 * This function runs a series of tests on the SPIFFS storage component
 * to verify its functionality, including file creation, reading, writing,
 * renaming, and deletion operations.
 */
void run_spiffs_storage_tests(void);

#ifdef __cplusplus
}
#endif

#endif // UNITY_TEST_H

/* Example test application for testable component.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "unity.h"

// #include "esp_task_wdt.h"

#include "esp_log.h"
#include "spi_ffs_storage.h"

static void print_banner(const char *text);

static const char *TAG = "example_unit_test";

void app_main(void)
{

    // Disable the watchdog timer to prevent it from resetting the system
    // during the test execution, especially when using unity_run_menu().
    // esp_task_wdt_delete(NULL); // Delete the task watchdog for the current task

    /* These are the different ways of running registered tests.
     * In practice, only one of them is usually needed.
     *
     * UNITY_BEGIN() and UNITY_END() calls tell Unity to print a summary
     * (number of tests executed/failed/ignored) of tests executed between these calls.
     */
    // print_banner("Executing one test by its name");
    // UNITY_BEGIN();
    // unity_run_test_by_name("Mean of an empty array is zero");
    // UNITY_END();

    // print_banner("Running tests with [mean] tag");
    // UNITY_BEGIN();
    // unity_run_tests_by_tag("[mean]", false);
    // UNITY_END();

    // print_banner("Running tests without [fails] tag");
    // UNITY_BEGIN();
    // unity_run_tests_by_tag("[fails]", true);
     UNITY_END();

    print_banner("Running all the registered tests");
    //UNITY_BEGIN();

    ESP_LOGI(TAG, "Initializing SPI FFS storage");
    spiffs_storage_init();

    //unity_run_all_tests();

    UNITY_END();

    // print_banner("Starting interactive test menu");
    /* This function will not return, and will be busy waiting for UART input.
     * Make sure that task watchdog is disabled if you use this function.
     */
    unity_run_menu();
}

static void print_banner(const char *text)
{
    printf("\n#### %s #####\n\n", text);
}

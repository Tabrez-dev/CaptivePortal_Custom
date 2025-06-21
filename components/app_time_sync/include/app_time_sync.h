#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize time synchronization
 * 
 * This function starts a background task to synchronize time with NTP servers.
 * It does not block the calling task.
 */
void app_time_sync_init(void);

/**
 * @brief Wait for time synchronization to complete with timeout
 * 
 * @param timeout_ms Timeout in milliseconds
 * @return true if time sync completed, false if timeout
 */
bool app_time_sync_wait(uint32_t timeout_ms);

/**
 * @brief Check if time synchronization is completed
 * 
 * @return true if time sync completed, false otherwise
 */
bool app_time_sync_is_completed(void);

#ifndef AWS_IOT_H
#define AWS_IOT_H

#include "esp_err.h"

/**
 * @brief Initialize AWS IoT connection
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t aws_iot_init(void);

/**
 * @brief Publish message to AWS IoT
 * @param topic MQTT topic
 * @param message Message payload
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t aws_iot_publish(const char *topic, const char *message);

#endif // AWS_IOT_H

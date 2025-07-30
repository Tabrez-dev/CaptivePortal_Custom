#ifndef COMPONENTS_AWS_IOT_INCLUDE_AWS_IOT_H_
#define COMPONENTS_AWS_IOT_INCLUDE_AWS_IOT_H_

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief AWS IoT Client ID - This should match your Thing name in AWS IoT
 */
#define CONFIG_AWS_EXAMPLE_CLIENT_ID ""  // Your Thing name from AWS IoT

/**
 * @brief MQTT topic for publishing sensor data
 */
#define AWS_IOT_SENSOR_TOPIC ""

/**
 * @brief AWS IoT endpoint - Your actual endpoint from AWS IoT Console
 */
#define AWS_IOT_MQTT_HOST ""

/**
 * @brief Initialize and start AWS IoT connection
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t aws_iot_start(void);

/**
 * @brief Publish sensor data to AWS IoT
 * @param temperature Temperature value in Celsius
 * @param humidity Humidity value in percentage
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t aws_iot_publish_sensor_data(float temperature, float humidity);

/**
 * @brief Check if AWS IoT connection is established
 * @return true if connected, false otherwise
 */
bool aws_iot_is_connected(void);

/**
 * @brief Callback function type for received MQTT messages
 * @param topic The topic on which the message was received
 * @param topic_len Length of the topic string
 * @param data The message data
 * @param data_len Length of the message data
 */
typedef void (*aws_iot_message_callback_t)(const char *topic, int topic_len, const char *data, int data_len);

/**
 * @brief Set the callback function for received messages
 * @param callback Function to call when a message is received
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t aws_iot_set_message_callback(aws_iot_message_callback_t callback);

/**
 * @brief Subscribe to an MQTT topic
 * @param topic The topic to subscribe to
 * @param qos Quality of Service level (0, 1, or 2)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t aws_iot_subscribe(const char *topic, int qos);

/**
 * @brief Unsubscribe from an MQTT topic
 * @param topic The topic to unsubscribe from
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t aws_iot_unsubscribe(const char *topic);

/**
 * @brief Default command topic for receiving commands from AWS IoT
 */
#define AWS_IOT_COMMAND_TOPIC "esp32/command"

#endif /* COMPONENTS_AWS_IOT_INCLUDE_AWS_IOT_H_ */

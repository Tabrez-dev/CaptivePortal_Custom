#include "aws_iot.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_tls.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "AWS_IOT";

// MQTT client handle
static esp_mqtt_client_handle_t mqtt_client = NULL;

// Connection status flag
static bool is_connected = false;

// Message callback function
static aws_iot_message_callback_t message_callback = NULL;

// Event group to signal connection status
static EventGroupHandle_t aws_iot_event_group;
#define AWS_IOT_CONNECTED_BIT BIT0

/**
 * @brief MQTT event handler
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, (long)event_id);
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            is_connected = true;
            xEventGroupSetBits(aws_iot_event_group, AWS_IOT_CONNECTED_BIT);
            
            // Subscribe to sensor data topic to receive messages
            ESP_LOGI(TAG, "Subscribing to sensor data topic: %s", AWS_IOT_SENSOR_TOPIC);
            int msg_id = esp_mqtt_client_subscribe(mqtt_client, AWS_IOT_SENSOR_TOPIC, 1);
            if (msg_id < 0) {
                ESP_LOGE(TAG, "Failed to subscribe to sensor data topic");
            } else {
                ESP_LOGI(TAG, "Subscription sent, msg_id=%d", msg_id);
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            is_connected = false;
            xEventGroupClearBits(aws_iot_event_group, AWS_IOT_CONNECTED_BIT);
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            ESP_LOGI(TAG, "Topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "Data: %.*s", event->data_len, event->data);
            
            // Call user callback if registered
            if (message_callback != NULL) {
                message_callback(event->topic, event->topic_len, event->data, event->data_len);
            }
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGI(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
                ESP_LOGI(TAG, "Last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
                        strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;
            
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

// Removed automatic publishing task - publishing will be triggered from HTTP handler

esp_err_t aws_iot_start(void)
{
    ESP_LOGI(TAG, "Initializing AWS IoT");
    
    // Create event group for AWS IoT connection status
    aws_iot_event_group = xEventGroupCreate();
    
    // Configure MQTT client - only need start pointers for certificates
    // Note: hyphens in filenames are converted to underscores in the symbol names
    extern const uint8_t amazon_root_ca1_pem_start[] asm("_binary_AmazonRootCA1_pem_start");
    extern const uint8_t device_certificate_pem_start[] asm("_binary_device_certificate_pem_start");
    extern const uint8_t private_key_pem_start[] asm("_binary_private_key_pem_start");
    
    // Build the full URI using the defined endpoint
    char mqtt_uri[256];
    snprintf(mqtt_uri, sizeof(mqtt_uri), "mqtts://%s:8883", AWS_IOT_MQTT_HOST);
    
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = mqtt_uri,
            .verification.certificate = (const char *)amazon_root_ca1_pem_start,
        },
        .credentials = {
            .client_id = CONFIG_AWS_EXAMPLE_CLIENT_ID,
            .authentication = {
                .certificate = (const char *)device_certificate_pem_start,
                .key = (const char *)private_key_pem_start,
            },
        },
        .network = {
            .reconnect_timeout_ms = 10000,
        },
        .session = {
            .keepalive = 60,
        },
    };
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    
    // Register MQTT event handler
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    // Start MQTT client
    esp_err_t err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        return err;
    }
    
    // No automatic publishing task - publishing will be triggered from HTTP handler
    
    return ESP_OK;
}

esp_err_t aws_iot_publish_sensor_data(float temperature, float humidity)
{
    if (!is_connected || mqtt_client == NULL) {
        ESP_LOGE(TAG, "Cannot publish - not connected to AWS IoT");
        return ESP_FAIL;
    }
    
    // Create JSON payload
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temperature", temperature);
    cJSON_AddNumberToObject(root, "humidity", humidity);
    cJSON_AddStringToObject(root, "device_id", CONFIG_AWS_EXAMPLE_CLIENT_ID);
    
    // Get timestamp if available
    time_t now;
    time(&now);
    if (now > 1600000000) {  // Sanity check for valid time (after 2020-09-13)
        cJSON_AddNumberToObject(root, "timestamp", now);
    }
    
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (payload == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON payload");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Publishing to topic %s: %s", AWS_IOT_SENSOR_TOPIC, payload);
    
    int msg_id = esp_mqtt_client_publish(mqtt_client, AWS_IOT_SENSOR_TOPIC, payload, 0, 1, 0);
    free(payload);
    
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish message");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Successfully published message, msg_id=%d", msg_id);
    return ESP_OK;
}

bool aws_iot_is_connected(void)
{
    return is_connected;
}

esp_err_t aws_iot_set_message_callback(aws_iot_message_callback_t callback)
{
    if (callback == NULL) {
        ESP_LOGE(TAG, "Callback function cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    message_callback = callback;
    ESP_LOGI(TAG, "Message callback registered");
    return ESP_OK;
}

esp_err_t aws_iot_subscribe(const char *topic, int qos)
{
    if (!is_connected || mqtt_client == NULL) {
        ESP_LOGE(TAG, "Cannot subscribe - not connected to AWS IoT");
        return ESP_FAIL;
    }
    
    if (topic == NULL || strlen(topic) == 0) {
        ESP_LOGE(TAG, "Invalid topic");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (qos < 0 || qos > 2) {
        ESP_LOGE(TAG, "Invalid QoS level. Must be 0, 1, or 2");
        return ESP_ERR_INVALID_ARG;
    }
    
    int msg_id = esp_mqtt_client_subscribe(mqtt_client, topic, qos);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to subscribe to topic: %s", topic);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Subscribed to topic: %s with QoS %d, msg_id=%d", topic, qos, msg_id);
    return ESP_OK;
}

esp_err_t aws_iot_unsubscribe(const char *topic)
{
    if (!is_connected || mqtt_client == NULL) {
        ESP_LOGE(TAG, "Cannot unsubscribe - not connected to AWS IoT");
        return ESP_FAIL;
    }
    
    if (topic == NULL || strlen(topic) == 0) {
        ESP_LOGE(TAG, "Invalid topic");
        return ESP_ERR_INVALID_ARG;
    }
    
    int msg_id = esp_mqtt_client_unsubscribe(mqtt_client, topic);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to unsubscribe from topic: %s", topic);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Unsubscribed from topic: %s, msg_id=%d", topic, msg_id);
    return ESP_OK;
}

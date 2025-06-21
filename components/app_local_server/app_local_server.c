/**
 * @file app_local_server.c
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <sys/param.h>
#include "esp_http_server.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include <cJSON.h>
#include "time.h"
#include "app_local_server.h"
#include "esp_log.h"
#include "dns_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_storage.h"
#include "rfid_manager.h"
#include "../app_time_sync/include/app_time_sync.h"
#include "freertos/timers.h"

// DEFINES
#define HTTP_SERVER_MAX_URI_HANDLERS (20u)
#define HTTP_SERVER_RECEIVE_WAIT_TIMEOUT (10u) // in seconds
#define HTTP_SERVER_SEND_WAIT_TIMEOUT (10u)    // in seconds
#define HTTP_SERVER_MONITOR_QUEUE_LEN (3u)

#define OTA_UPDATE_PENDING (0)
#define OTA_UPDATE_SUCCESSFUL (1)
#define OTA_UPDATE_FAILED (-1)

// Increased buffer size to handle more RFID cards in JSON format
// Previous size (3*1024) was too small for systems with many cards
#define HTTP_SERVER_BUFFER_SIZE (10 * 1024)

static char http_server_buffer[HTTP_SERVER_BUFFER_SIZE] = {0};
static const char *TAG = "app_local_server";
// GLOBAL VARIABLES
extern const char jquery_3_3_1_min_js_start[] asm("_binary_jquery_3_3_1_min_js_start");
extern const char jquery_3_3_1_min_js_end[] asm("_binary_jquery_3_3_1_min_js_end");
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern const char app_css_start[] asm("_binary_app_css_start");
extern const char app_css_end[] asm("_binary_app_css_end");
extern const char app_js_start[] asm("_binary_app_js_start");
extern const char app_js_end[] asm("_binary_app_js_end");
extern const char favicon_ico_start[] asm("_binary_favicon_ico_start");
extern const char favicon_ico_end[] asm("_binary_favicon_ico_end");
extern const char root_start[] asm("_binary_root_html_start");
extern const char root_end[] asm("_binary_root_html_end");
extern const char rfid_management_html_start[] asm("_binary_rfid_management_html_start");
extern const char rfid_management_html_end[] asm("_binary_rfid_management_html_end");
extern const char rfid_management_js_start[] asm("_binary_rfid_management_js_start");
extern const char rfid_management_js_end[] asm("_binary_rfid_management_js_end");

static httpd_handle_t http_server_handle = NULL;
// Queue Handle used to manipulate the main queue of events
static QueueHandle_t http_server_monitor_q_handle;
// Firmware Update Status
static int fw_update_status = OTA_UPDATE_PENDING;
// Local Time Status
static bool g_is_local_time_set = false;

// ESP32 Timer Configuration Passed to esp_timer_create
static const esp_timer_create_args_t fw_update_reset_args =
    {
        .callback = &http_server_fw_update_reset_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "fw_update_reset"};
esp_timer_handle_t fw_update_reset;

static http_server_wifi_connect_status_e g_wifi_connect_status = HTTP_WIFI_STATUS_CONNECT_NONE;

// FUNCTION PROTOTYPES
static BaseType_t http_server_monitor_send_msg(http_server_msg_e msg_id);
static void http_server_monitor(void);
static void start_webserver(void);
static void http_server_fw_update_reset_timer(void);

static esp_err_t http_server_j_query_handler(httpd_req_t *req);
static esp_err_t http_server_index_html_handler(httpd_req_t *req);
static esp_err_t http_server_app_css_handler(httpd_req_t *req);
static esp_err_t http_server_app_js_handler(httpd_req_t *req);
static esp_err_t http_server_favicon_handler(httpd_req_t *req);
static esp_err_t http_server_ota_update_handler(httpd_req_t *req);
static esp_err_t http_server_ota_status_handler(httpd_req_t *req);
static esp_err_t http_server_ssid_handler(httpd_req_t *req);
static esp_err_t http_server_time_handler(httpd_req_t *req);
static esp_err_t http_server_sensor_handler(httpd_req_t *req);
static esp_err_t http_server_wifi_connect_handler(httpd_req_t *req);
static esp_err_t http_server_wifi_connect_status_handler(httpd_req_t *req);
static esp_err_t http_server_wifi_connect_info_handler(httpd_req_t *req);
static esp_err_t http_server_wifi_disconnect_handler(httpd_req_t *req);
static esp_err_t http_server_get_saved_station_ssid_handler(httpd_req_t *req);

// RFID Management Webpage Handlers
static esp_err_t http_server_rfid_management_html_handler(httpd_req_t *req);
static esp_err_t http_server_rfid_management_js_handler(httpd_req_t *req);

// RFID Management API Handlers
static esp_err_t http_server_rfid_list_cards_handler(httpd_req_t *req);
static esp_err_t http_server_rfid_add_card_handler(httpd_req_t *req);
static esp_err_t http_server_rfid_remove_card_handler(httpd_req_t *req);
static esp_err_t http_server_rfid_get_card_count_handler(httpd_req_t *req);
static esp_err_t http_server_rfid_check_card_handler(httpd_req_t *req);
static esp_err_t http_server_rfid_reset_handler(httpd_req_t *req);

static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err);

static esp_err_t http_server_get_data_handler(httpd_req_t *req);
static int16_t get_humidity(void);
static int16_t get_temperature(void);
bool get_data_rsp_string(char *key, char *buffer, uint16_t len);
void get_local_time_string(char *buffer, size_t len);
void get_local_time_string_utc(char *buffer, size_t len);

static const httpd_uri_t uri_handlers[] = {
    {.uri = "/jquery-3.3.1.min.js",
     .method = HTTP_GET,
     .handler = http_server_j_query_handler,
     .user_ctx = NULL},
    {.uri = "/",
     .method = HTTP_GET,
     .handler = http_server_index_html_handler,
     .user_ctx = NULL},
    {.uri = "/app.css",
     .method = HTTP_GET,
     .handler = http_server_app_css_handler,
     .user_ctx = NULL},
    {.uri = "/app.js",
     .method = HTTP_GET,
     .handler = http_server_app_js_handler,
     .user_ctx = NULL},
    {.uri = "/favicon.ico",
     .method = HTTP_GET,
     .handler = http_server_favicon_handler,
     .user_ctx = NULL},
    {.uri = "/OTAupdate",
     .method = HTTP_POST,
     .handler = http_server_ota_update_handler,
     .user_ctx = NULL},
    {.uri = "/OTAstatus",
     .method = HTTP_POST,
     .handler = http_server_ota_status_handler,
     .user_ctx = NULL},
    {.uri = "/apSSID",
     .method = HTTP_GET,
     .handler = http_server_ssid_handler,
     .user_ctx = NULL},
    {.uri = "/localTime",
     .method = HTTP_GET,
     .handler = http_server_time_handler,
     .user_ctx = NULL},
    {.uri = "/Sensor",
     .method = HTTP_GET,
     .handler = http_server_sensor_handler,
     .user_ctx = NULL},
    {.uri = "/getData",
     .method = HTTP_POST,
     .handler = http_server_get_data_handler,
     .user_ctx = NULL},
    {.uri = "/wifiConnect",
     .method = HTTP_POST,
     .handler = http_server_wifi_connect_handler,
     .user_ctx = NULL},
    {.uri = "/wifiConnectStatus",
     .method = HTTP_POST,
     .handler = http_server_wifi_connect_status_handler,
     .user_ctx = NULL},
    {.uri = "/wifiConnectInfo",
     .method = HTTP_GET,
     .handler = http_server_wifi_connect_info_handler,
     .user_ctx = NULL},
    {.uri = "/wifiDisconnect",
     .method = HTTP_DELETE,
     .handler = http_server_wifi_disconnect_handler,
     .user_ctx = NULL},
    // Saved Station SSID Endpoint
    {
        .uri = "/getSavedStationSSID",
        .method = HTTP_GET,
        .handler = http_server_get_saved_station_ssid_handler,
        .user_ctx = NULL},
    // RFID Management Webpage
    {
        .uri = "/rfid_management.html",
        .method = HTTP_GET,
        .handler = http_server_rfid_management_html_handler,
        .user_ctx = NULL},
    {.uri = "/rfid_management.js",
     .method = HTTP_GET,
     .handler = http_server_rfid_management_js_handler,
     .user_ctx = NULL},
    // RFID Endpoints
    {
        .uri = "/cards/Get",
        .method = HTTP_GET,
        .handler = http_server_rfid_list_cards_handler,
        .user_ctx = NULL},
    {.uri = "/cards/Add",
     .method = HTTP_POST,
     .handler = http_server_rfid_add_card_handler,
     .user_ctx = NULL},
    {.uri = "/cards/Delete",
     .method = HTTP_DELETE,
     .handler = http_server_rfid_remove_card_handler,
     .user_ctx = NULL},
    {.uri = "/cards/Count",
     .method = HTTP_GET,
     .handler = http_server_rfid_get_card_count_handler,
     .user_ctx = NULL},
    {.uri = "/cards/Check",
     .method = HTTP_POST,
     .handler = http_server_rfid_check_card_handler,
     .user_ctx = NULL},
    {.uri = "/cards/Reset",
     .method = HTTP_POST,
     .handler = http_server_rfid_reset_handler,
     .user_ctx = NULL}

};

// Calculate the number of URI handlers
#define URI_HANDLERS_COUNT (sizeof(uri_handlers) / sizeof(uri_handlers[0]))

// FUNCTIONS
bool app_local_server_init(void)
{
    // create a message queue
    http_server_monitor_q_handle = xQueueCreate(HTTP_SERVER_MONITOR_QUEUE_LEN,
                                                sizeof(http_server_q_msg_t));
    return true;
}

bool app_local_server_start(void)
{
    // Start the web server
    start_webserver();
    start_dns_server();
    return true;
}

bool app_local_server_process(void)
{
    // Process the HTTP Server Monitor Task
    http_server_monitor();

    return true;
}

/*
 * Timer Callback function which calls esp_restart function upon successful
 * firmware update
 */
void http_server_fw_update_reset_cb(void *arg)
{
    ESP_LOGI(TAG, "http_fw_update_reset_cb: Timer timed-out, restarting the device");
    esp_restart();
}

/*
 * Sends a message to the Queue
 * @param msg_id Message ID from the http_server_msg_e enum
 * @return pdTRUE if an item was successfully sent to the queue, otherwise pdFALSE
 */
static BaseType_t http_server_monitor_send_msg(http_server_msg_e msg_id)
{
    http_server_q_msg_t msg;
    msg.msg_id = msg_id;
    return xQueueSend(http_server_monitor_q_handle, &msg, portMAX_DELAY);
}

/*
 * HTTP Server Monitor Task used to track events of the HTTP Server.
 * @param pvParameter parameters which can be passed to the task
 * @return http server instance handle if successful, NULL otherwise
 */
static void http_server_monitor(void)
{
    http_server_q_msg_t msg;

    if (xQueueReceive(http_server_monitor_q_handle, &msg, portMAX_DELAY))
    {
        switch (msg.msg_id)
        {
        case HTTP_MSG_WIFI_CONNECT_INIT:
            ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_INIT");
            g_wifi_connect_status = HTTP_WIFI_STATUS_CONNECTING;
            break;
        case HTTP_MSG_WIFI_CONNECT_SUCCESS:
            ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_SUCCESS");
            g_wifi_connect_status = HTTP_WIFI_STATUS_CONNECT_SUCCESS;
            break;
        case HTTP_MSG_WIFI_CONNECT_FAIL:
            ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_FAIL");
            g_wifi_connect_status = HTTP_WIFI_STATUS_CONNECT_FAILED;
            break;
        case HTTP_MSG_WIFI_USER_DISCONNECT:
            ESP_LOGI(TAG, "HTTP_MSG_WIFI_USER_DISCONNECT");
            g_wifi_connect_status = HTTP_WIFI_STATUS_DISCONNECTED;
            break;
        case HTTP_MSG_WIFI_OTA_UPDATE_SUCCESSFUL:
            ESP_LOGI(TAG, "HTTP_MSG_OTA_UPDATE_SUCCESSFUL");
            fw_update_status = OTA_UPDATE_SUCCESSFUL;
            http_server_fw_update_reset_timer();
            break;
        case HTTP_MSG_WIFI_OTA_UPDATE_FAILED:
            ESP_LOGI(TAG, "HTTP_MSG_OTA_UPDATE_FAILED");
            fw_update_status = OTA_UPDATE_FAILED;
            break;
        case HTTP_MSG_TIME_SERVICE_INITIALIZED:
            ESP_LOGI(TAG, "HTTP_MSG_TIME_SERVICE_INITIALIZED");
            g_is_local_time_set = true;
            break;
        default:
            break;
        }
    }
}

static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = URI_HANDLERS_COUNT;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    
    // Increase buffer size for handling larger files
    config.recv_wait_timeout = 15;  // Increase timeout for receiving data
    config.send_wait_timeout = 15;  // Increase timeout for sending data
    config.stack_size = 8192;       // Increase stack size for the HTTP server task
    
    ESP_LOGI(TAG, "Starting on port: '%d'", config.server_port);
    if (httpd_start(&http_server_handle, &config) == ESP_OK)
    {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        // Register all handlers from the global array
        for (int i = 0; i < URI_HANDLERS_COUNT; i++)
        {
            if (httpd_register_uri_handler(http_server_handle, &uri_handlers[i]) != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to register handler for %s", uri_handlers[i].uri);
            }
            else
            {
                ESP_LOGI(TAG, "Registered handler for %s", uri_handlers[i].uri);
            }
        }
        httpd_register_err_handler(http_server_handle, HTTPD_404_NOT_FOUND, http_404_error_handler);
    }
}

/*
 * Check the fw_update_status and creates the fw_update_reset time if the
 * fw_update_status is true
 */
static void http_server_fw_update_reset_timer(void)
{
    if (fw_update_status == OTA_UPDATE_SUCCESSFUL)
    {
        ESP_LOGI(TAG, "http_server_fw_update_reset_timer: FW Update successful starting FW update reset timer");
        // Give the web page a chance to receive an acknowledge back and initialize the timer
        ESP_ERROR_CHECK(esp_timer_create(&fw_update_reset_args, &fw_update_reset));
        ESP_ERROR_CHECK(esp_timer_start_once(fw_update_reset, 8 * 1000 * 1000));
    }
    else
    {
        ESP_LOGI(TAG, "http_server_fw_update_reset_timer: FW Update unsuccessful");
    }
}

/*
 * jQuery get handler requested when accessing the web page.
 * @param req HTTP request for which the uri needs to be handled
 * @return ESP_OK
 */
static esp_err_t http_server_j_query_handler(httpd_req_t *req)
{
    esp_err_t error;
    ESP_LOGI(TAG, "JQuery Requested");
    
    // Set content type
    httpd_resp_set_type(req, "application/javascript");
    
    // Add caching headers - cache for 1 hour (3600 seconds)
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600, public");
    httpd_resp_set_hdr(req, "ETag", "jquery-3.3.1");
    
    // Check if client has this file cached (If-None-Match header)
    char if_none_match[32];
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", if_none_match, sizeof(if_none_match)) == ESP_OK) {
        if (strcmp(if_none_match, "jquery-3.3.1") == 0) {
            // Client has a valid cached version
            httpd_resp_set_status(req, "304 Not Modified");
            httpd_resp_send(req, NULL, 0);
            ESP_LOGI(TAG, "http_server_j_query_handler: Sent 304 Not Modified");
            return ESP_OK;
        }
    }
    
    // Send the file with chunked encoding for large files
    httpd_resp_set_hdr(req, "Transfer-Encoding", "chunked");
    
    // Calculate file size
    size_t file_size = jquery_3_3_1_min_js_end - jquery_3_3_1_min_js_start;
    const char* file_data = jquery_3_3_1_min_js_start;
    
    // Send in chunks of 4KB
    const size_t chunk_size = 4096;
    size_t bytes_sent = 0;
    
    while (bytes_sent < file_size) {
        size_t current_chunk_size = (file_size - bytes_sent > chunk_size) ? 
                                    chunk_size : (file_size - bytes_sent);
        
        error = httpd_resp_send_chunk(req, file_data + bytes_sent, current_chunk_size);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "http_server_j_query_handler: Error %d while sending chunk", error);
            httpd_resp_sendstr_chunk(req, NULL); // End chunked response on error
            return error;
        }
        
        bytes_sent += current_chunk_size;
        // Small delay to prevent overwhelming the socket buffer
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
    
    // End chunked response
    error = httpd_resp_sendstr_chunk(req, NULL);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "http_server_j_query_handler: Error %d ending chunked response", error);
    } else {
        ESP_LOGI(TAG, "http_server_j_query_handler: Response Sent Successfully");
    }
    
    return error;
}

/*
 * Send the index HTML page
 * @param req HTTP request for which the uri needs to be handled
 * @return ESP_OK
 */
static esp_err_t http_server_index_html_handler(httpd_req_t *req)
{
    esp_err_t error;
    ESP_LOGI(TAG, "Index HTML Requested");
    
    // Set content type
    httpd_resp_set_type(req, "text/html");
    
    // Add caching headers - cache for 1 hour (3600 seconds)
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600, public");
    httpd_resp_set_hdr(req, "ETag", "index-html-v1");
    
    // Check if client has this file cached (If-None-Match header)
    char if_none_match[32];
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", if_none_match, sizeof(if_none_match)) == ESP_OK) {
        if (strcmp(if_none_match, "index-html-v1") == 0) {
            // Client has a valid cached version
            httpd_resp_set_status(req, "304 Not Modified");
            httpd_resp_send(req, NULL, 0);
            ESP_LOGI(TAG, "http_server_index_html_handler: Sent 304 Not Modified");
            return ESP_OK;
        }
    }
    
    // For small files like HTML, we can send directly without chunking
    error = httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "http_server_index_html_handler: Error %d while sending Response", error);
    }
    else
    {
        ESP_LOGI(TAG, "http_server_index_html_handler: Response Sent Successfully");
    }
    return error;
}

/*
 * app.css get handler is requested when accessing the web page
 * @param req HTTP request for which the uri needs to be handled
 * @return ESP_OK
 */
static esp_err_t http_server_app_css_handler(httpd_req_t *req)
{
    esp_err_t error;
    ESP_LOGI(TAG, "APP CSS Requested");
    
    // Set content type
    httpd_resp_set_type(req, "text/css");
    
    // Add caching headers - cache for 1 hour (3600 seconds)
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600, public");
    httpd_resp_set_hdr(req, "ETag", "app-css-v1");
    
    // Check if client has this file cached (If-None-Match header)
    char if_none_match[32];
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", if_none_match, sizeof(if_none_match)) == ESP_OK) {
        if (strcmp(if_none_match, "app-css-v1") == 0) {
            // Client has a valid cached version
            httpd_resp_set_status(req, "304 Not Modified");
            httpd_resp_send(req, NULL, 0);
            ESP_LOGI(TAG, "http_server_app_css_handler: Sent 304 Not Modified");
            return ESP_OK;
        }
    }
    
    // Send the file
    error = httpd_resp_send(req, (const char *)app_css_start, app_css_end - app_css_start);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "http_server_app_css_handler: Error %d while sending Response", error);
    }
    else
    {
        ESP_LOGI(TAG, "http_server_app_css_handler: Response Sent Successfully");
    }
    return error;
}

/*
 * app.js get handler requested when accessing the web page.
 * @param req HTTP request for which the uri needs to be handled
 * @return ESP_OK
 */
static esp_err_t http_server_app_js_handler(httpd_req_t *req)
{
    esp_err_t error;
    ESP_LOGI(TAG, "APP JS Requested");
    
    // Set content type
    httpd_resp_set_type(req, "application/javascript");
    
    // Add caching headers - cache for 1 hour (3600 seconds)
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600, public");
    httpd_resp_set_hdr(req, "ETag", "app-js-v1");
    
    // Check if client has this file cached (If-None-Match header)
    char if_none_match[32];
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", if_none_match, sizeof(if_none_match)) == ESP_OK) {
        if (strcmp(if_none_match, "app-js-v1") == 0) {
            // Client has a valid cached version
            httpd_resp_set_status(req, "304 Not Modified");
            httpd_resp_send(req, NULL, 0);
            ESP_LOGI(TAG, "http_server_app_js_handler: Sent 304 Not Modified");
            return ESP_OK;
        }
    }
    
    // Send the file
    error = httpd_resp_send(req, (const char *)app_js_start, app_js_end - app_js_start);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "http_server_app_js_handler: Error %d while sending Response", error);
    }
    else
    {
        ESP_LOGI(TAG, "http_server_app_js_handler: Response Sent Successfully");
    }
    return error;
}

/*
 * Sends the .ico file when accessing the web page
 * @param req HTTP request for which the uri needs to be handled
 * @return ESP_OK
 */
static esp_err_t http_server_favicon_handler(httpd_req_t *req)
{
    esp_err_t error;
    ESP_LOGI(TAG, "Favicon.ico Requested");
    
    // Set content type
    httpd_resp_set_type(req, "image/x-icon");
    
    // Add caching headers - cache for 1 day (86400 seconds) since favicon rarely changes
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400, public");
    httpd_resp_set_hdr(req, "ETag", "favicon-v1");
    
    // Check if client has this file cached (If-None-Match header)
    char if_none_match[32];
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", if_none_match, sizeof(if_none_match)) == ESP_OK) {
        if (strcmp(if_none_match, "favicon-v1") == 0) {
            // Client has a valid cached version
            httpd_resp_set_status(req, "304 Not Modified");
            httpd_resp_send(req, NULL, 0);
            ESP_LOGI(TAG, "http_server_favicon_handler: Sent 304 Not Modified");
            return ESP_OK;
        }
    }
    
    // Send the file
    error = httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_end - favicon_ico_start);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "http_server_favicon_handler: Error %d while sending Response", error);
    }
    else
    {
        ESP_LOGI(TAG, "http_server_favicon_handler: Response Sent Successfully");
    }
    return error;
}

/**
 * @brief Receives the *.bin file via the web page and handles the firmware update
 * @param req HTTP request for which the uri needs to be handled
 * @return ESP_OK, other ESP_FAIL if timeout occurs and the update canot be started
 */
static esp_err_t http_server_ota_update_handler(httpd_req_t *req)
{
    esp_err_t error;
    esp_ota_handle_t ota_handle;
    char ota_buffer[1024];
    int content_len = req->content_len; // total content length
    int content_received = 0;
    int recv_len = 0;
    bool is_req_body_started = false;
    bool flash_successful = false;

    // get the next OTA app partition which should be written with a new firmware
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

    // our ota_buffer is not sufficient to receive all data in a one go
    // hence we will read the data in chunks and write in chunks, read the below
    // mentioned comments for more information
    do
    {
        // The following is the API to read content of data from the HTTP request
        /* This API will read HTTP content data from the HTTP request into the
         * provided buffer. Use content_len provided in the httpd_req_t structure to
         *  know the length of the data to be fetched.
         *  If the content_len is to large for the buffer then the user may have to
         *  make multiple calls to this functions (as done here), each time fetching
         *  buf_len num of bytes (which is ota_buffer length here), while the pointer
         *  to content data is incremented internally by the same number
         *  This function returns
         *  Bytes: Number of bytes read into the buffer successfully
         *  0: Buffer length parameter is zero/connection closed by peer.
         *  HTTPD_SOCK_ERR_INVALID: Invalid Arguments
         *  HTTPD_SOCK_ERR_TIMEOUT: Timeout/Interrupted while calling socket recv()
         *  HTTPD_SOCK_ERR_FAIL: Unrecoverable error while calling socket recv()
         *  Parameters to this function are:
         *  req: The request being responded to
         *  ota_buffer: Pointer to a buffer that the data will be read into
         *  length: length of the buffer which ever is minimum (as we don't want to
         *          read more data which buffer can't handle)
         */
        recv_len = httpd_req_recv(req, ota_buffer, MIN(content_len, sizeof(ota_buffer)));
        // if recv_len is less than zero, it means some problem (but if timeout error, then try again)
        if (recv_len < 0)
        {
            // Check if timeout occur, then we will retry again
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
            {
                ESP_LOGI(TAG, "http_server_ota_update_handler: Socket Timeout");
                continue; // Retry Receiving if Timeout Occurred
            }
            // If there is some other error apart from Timeout, then exit with fail
            ESP_LOGI(TAG, "http_server_ota_update_handler: OTA Other Error, %d", recv_len);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "http_server_ota_update_handler: OTA RX: %d of %d", content_received, content_len);

        // We are here which means that "recv_len" is positive, now we have to check
        // if this is the first data we are receiving or not, If so, it will have
        // the information in the header that we need
        if (!is_req_body_started)
        {
            is_req_body_started = true;
            // Now we have to identify from where the binary file content is starting
            // this can be done by actually checking the escape characters i.e. \r\n\r\n
            // Get the location of the *.bin file content (remove the web form data)
            // the strstr will return the pointer to the \r\n\r\n in the ota_buffer
            // and then by adding 4 we reach to the start of the binary content/start
            char *body_start_p = strstr(ota_buffer, "\r\n\r\n") + 4u;
            int body_part_len = recv_len - (body_start_p - ota_buffer);
            ESP_LOGI(TAG, "http_server_ota_update_handler: OTA File Size: %d", content_len);
            /*
             * esp_ota_begin function commence an OTA update writing to the specified
             * partition. The specified partition is erased to the specified image
             * size. If the image size is not yet known, OTA_SIZE_UNKNOWN is passed
             * which will cause the entire partition to be erased.
             * On Success this function allocates memory that remains in use until
             * esp_ota_end is called with the return handle.
             */
            error = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
            if (error != ESP_OK)
            {
                ESP_LOGI(TAG, "http_server_ota_update_handler: Error with OTA Begin, Canceling OTA");
                return ESP_FAIL;
            }
            else
            {
                ESP_LOGI(TAG, "http_server_ota_update_handler: Writing to partition subtype %d at offset 0x%lx", update_partition->subtype, update_partition->address);
            }
            /*
             * esp_ota_write function, writes the OTA update to the partition.
             * This function can be called multiple times as data is received during
             * the OTA operation. Data is written sequentially to the partition.
             * Here we are writing the body start for the first time.
             */
            esp_ota_write(ota_handle, body_start_p, body_part_len);
            content_received += body_part_len;
        }
        else
        {
            /* Continue to receive data above using httpd_req_recv function, and write
             * using esp_ota_write (below), until all the content is received. */
            esp_ota_write(ota_handle, ota_buffer, recv_len);
            content_received += recv_len;
        }

    } while ((recv_len > 0) && (content_received < content_len));
    // till complete data is received and written or some error is there we will
    // remain in the above mentioned do-while loop

    /* Finish the OTA update and validate newly written app image.
     * After calling esp_ota_end, the handle is no longer valid and memory associated
     * with it is freed (regardless of the results).
     */
    if (esp_ota_end(ota_handle) == ESP_OK)
    {
        // let's update the partition i.e. configure OTA data for new boot partition
        if (esp_ota_set_boot_partition(update_partition) == ESP_OK)
        {
            const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
            ESP_LOGI(TAG, "http_server_ota_update_handler: Next boot partition subtype %d at offset 0x%lx", boot_partition->subtype, boot_partition->address);
            flash_successful = true;
        }
        else
        {
            ESP_LOGI(TAG, "http_server_ota_update_handler: Flash Error");
        }
    }
    else
    {
        ESP_LOGI(TAG, "http_server_ota_update_handler: esp_ota_end Error");
    }

    // We won't update the global variables throughout the file, so send the message about the status
    if (flash_successful)
    {
        http_server_monitor_send_msg(HTTP_MSG_WIFI_OTA_UPDATE_SUCCESSFUL);
    }
    else
    {
        http_server_monitor_send_msg(HTTP_MSG_WIFI_OTA_UPDATE_FAILED);
    }
    return ESP_OK;
}

/*
 * OTA status handler responds with the firmware update status after the OTA
 * update is started and responds with the compile time & date when the page is
 * first requested
 * @param req HTTP request for which the URI needs to be handled
 * @return ESP_OK
 */
static esp_err_t http_server_ota_status_handler(httpd_req_t *req)
{
    char ota_JSON[100];
    ESP_LOGI(TAG, "OTA Status Requested");
    sprintf(ota_JSON, "{\"ota_update_status\":%d,\"compile_time\":\"%s\",\"compile_date\":\"%s\"}", fw_update_status, __TIME__, __DATE__);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, ota_JSON, strlen(ota_JSON));

    return ESP_OK;
}

static esp_err_t http_server_ssid_handler(httpd_req_t *req)
{
    char ssid[100];
    ESP_LOGI(TAG, "SSID Requested");
    sprintf(ssid, "{\"ssid\":\"%s\"}", CONFIG_ESP_WIFI_AP_SSID);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, ssid, strlen(ssid));

    return ESP_OK;
}
#include <inttypes.h>
static esp_err_t http_server_time_handler(httpd_req_t *req)
{
    char time_response[100];
    time_t now;
    struct tm timeinfo;

    ESP_LOGI(TAG, "Time Requested");

    // Check if time sync is completed using the new non-blocking API
    bool time_sync_completed = app_time_sync_is_completed();
    
    if (!g_is_local_time_set && time_sync_completed)
    {
        ESP_LOGI(TAG, "Time synchronization completed");
        g_is_local_time_set = true;
        http_server_monitor_send_msg(HTTP_MSG_TIME_SERVICE_INITIALIZED);
    }
    else if (!g_is_local_time_set)
    {
        ESP_LOGI(TAG, "Time synchronization still in progress");
    }

    // Get current time (localtime_r will apply timezone automatically)
    time(&now);
    localtime_r(&now, &timeinfo);

    // Log raw UTC time
    ESP_LOGI(TAG, "Raw UTC Time: %" PRId64, (int64_t)now);

    // Format time as JSON
    if (timeinfo.tm_year > (1970 - 1900)) // Ensure valid time
    {
        char time_str[32];
        // First format just the time part
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %I:%M:%S %p", &timeinfo);
        // Then create the complete JSON response
        snprintf(time_response, sizeof(time_response), 
                 "{\"time\":\"%s\", \"synced\":%s}", 
                 time_str, time_sync_completed ? "true" : "false");
        ESP_LOGI(TAG, "Formatted Local Time: %s", time_response);
    }
    else
    {
        snprintf(time_response, sizeof(time_response), 
                 "{\"error\":\"Time not synchronized\", \"synced\":false, \"in_progress\":%s}", 
                 time_sync_completed ? "false" : "true");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, time_response, strlen(time_response));

    return ESP_OK;
}
#include <stdlib.h>
#include <time.h>
static esp_err_t http_server_sensor_handler(httpd_req_t *req)
{
    char sensor_response[100];

    ESP_LOGI(TAG, "Sensor Data Requested");

    // Simulated Temperature: 20.0°C to 30.0°C
    float temp = 20.0f + ((float)rand() / RAND_MAX) * 10.0f;

    // Simulated Humidity: 40.0% to 60.0%
    float humidity = 40.0f + ((float)rand() / RAND_MAX) * 20.0f;

    ESP_LOGI(TAG, "Simulated Temperature: %.2f°C, Humidity: %.2f%%", temp, humidity);

    // Format as JSON response
    snprintf(sensor_response, sizeof(sensor_response),
             "{\"temp\": %.2f, \"humidity\": %.2f}", temp, humidity);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, sensor_response, strlen(sensor_response));

    return ESP_OK;
}

// HTTP Error (404) Handler - Redirects all requests to the root page
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}

static esp_err_t http_server_get_data_handler(httpd_req_t *req)
{
    bool isComma = false;
    int32_t length = 0;
    uint16_t rsp_len = 0;
    char temp_buff[256] = {0};
    ESP_LOGI(TAG, "Parameters Request Received");

    // Read request content
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0)
    {
        ESP_LOGE(TAG, "Failed to receive request data");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Process parameters (this is a placeholder for actual processing logic)
    ESP_LOGI(TAG, "Received parameters: %s", buf);

    // Parse JSON
    cJSON *json = cJSON_Parse(buf);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Extract the "kay" value
    //
    // { "key": "SSID" } -> { "SSID": "NetworkA" }
    cJSON *key_obj = cJSON_GetObjectItemCaseSensitive(json, "key");
    if (cJSON_IsString(key_obj) && (key_obj->valuestring != NULL))
    {
        // ESP_LOGI(TAG, "Key value: %s", key_obj->valuestring);
        // I am sending comma separated values in key_obj->valuestring like "SSID,Temp,Humidity"
        // extract them based on the comma token, give me code to extract them
        // ',' ASCII value of comma
        // "," const string
        // {"SSID":"NetworkA", "Temp":"25", "Humidity":"60"}
        char *token;

        token = strtok(key_obj->valuestring, ",");

        memset(http_server_buffer, 0, HTTP_SERVER_BUFFER_SIZE);

        length = snprintf(http_server_buffer,
                          HTTP_SERVER_BUFFER_SIZE,
                          "{");

        while (token != NULL)
        {
            ESP_LOGI(TAG, "Key value: %s", token);
            memset(temp_buff, 0, sizeof(temp_buff));
            get_data_rsp_string(token, temp_buff, sizeof(temp_buff));
            length += snprintf(http_server_buffer + length,
                               HTTP_SERVER_BUFFER_SIZE - length,
                               "%s%s",
                               isComma ? "," : "",
                               temp_buff);
            token = strtok(NULL, ",");
            isComma = true;
        }

        length += snprintf(http_server_buffer + length,
                           HTTP_SERVER_BUFFER_SIZE - length,
                           "}");

        ESP_LOGI(TAG, "%s [%ld]: %s", key_obj->valuestring, length, http_server_buffer);
    }
    else
    {
        ESP_LOGE(TAG, "Invalid or missing 'name' key in JSON");
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Clean up JSON object
    cJSON_Delete(json);

    // Send success response
    const char *response = NULL;

    if (length > 0)
    {
        response = http_server_buffer;
        rsp_len = length;
    }
    else
    {
        response = "{\"status\": \"error\"}";
        rsp_len = strlen(response);
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t error = httpd_resp_send(req, response, rsp_len);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "Error %d while sending params response", error);
    }
    else
    {
        ESP_LOGI(TAG, "Params response sent successfully");
    }

    return error;
}

void get_local_time_string_utc(char *buffer, size_t len)
{
    time_t now;
    struct tm timeinfo;

    time(&now);
    gmtime_r(&now, &timeinfo); // Get UTC time

    if (timeinfo.tm_year > (1970 - 1900)) // Ensure valid time
    {
        strftime(buffer, len, "%Y-%m-%d %H:%M:%S", &timeinfo);
    }
    else
    {
        snprintf(buffer, len, "Time not set");
    }
}

void get_local_time_string(char *buffer, size_t len)
{
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo); // Get local time

    if (timeinfo.tm_year > (1970 - 1900)) // Ensure valid time
    {
        strftime(buffer, len, "%Y-%m-%d %H:%M:%S", &timeinfo);
    }
    else
    {
        snprintf(buffer, len, "Time not set");
    }
}

bool get_data_rsp_string(char *key, char *buffer, uint16_t len)
{
    if (!key || !buffer || len == 0)
    {
        ESP_LOGE(TAG, "Buffer is NULL or length is zero");
        return false;
    }

    if (strstr(key, "SSID") != NULL)
    {
        snprintf(buffer, len, "\"SSID\":\"%s\"", CONFIG_ESP_WIFI_SSID);
    }
    else if (strstr(key, "Temp") != NULL)
    {
        snprintf(buffer, len, "\"Temp\":\"%d\"", get_temperature()); // Placeholder for temperature
    }
    else if (strstr(key, "Humidity") != NULL)
    {
        snprintf(buffer, len, "\"Humidity\":\"%d\"", get_humidity()); // Placeholder for humidity
    }
    else if (strstr(key, "UTC") != NULL)
    {
        char time_str[20];
        get_local_time_string_utc(time_str, sizeof(time_str));
        snprintf(buffer, len, "\"UTC\":\"%s\"", time_str);
    }
    else if (strstr(key, "Local") != NULL)
    {
        char time_str[20];
        get_local_time_string(time_str, sizeof(time_str));
        snprintf(buffer, len, "\"Local\":\"%s\"", time_str);
    }
    else if (strstr(key, "CompileTime") != NULL)
    {
        snprintf(buffer, len, "\"CompileTime\":\"%s\"", __TIME__);
    }
    else if (strstr(key, "CompileDate") != NULL)
    {
        snprintf(buffer, len, "\"CompileDate\":\"%s\"", __DATE__);
    }
    else if (strstr(key, "FirmwareVersion") != NULL)
    {
        snprintf(buffer, len, "\"FirmwareVersion\":\"%s\"", "V1.0.0");
    }
    else if (strstr(key, "WiFiStatus") != NULL)
    {
        snprintf(buffer, len, "\"WiFiStatus\":\"%d\"", g_wifi_connect_status);
    }
    else
    {
        snprintf(buffer, len, "\"%s\":\"\"", key); // Default case
    }

    return true;
}

static int16_t get_temperature(void)
{
    // return random number between 0 and 100
    return (rand() % 100);
}

static int16_t get_humidity(void)
{
    // return random number between 0 and 100
    return (rand() % 100);
}

static esp_err_t http_server_wifi_connect_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WiFi Connect Request Received");
    // Variables for SSID and password
    char ssid[32] = {0};
    char password[64] = {0};
    esp_err_t error;

    // Extract SSID from header
    error = httpd_req_get_hdr_value_str(req, "my-connect-ssid", ssid, sizeof(ssid) - 1);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SSID header");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Extract password from header
    error = httpd_req_get_hdr_value_str(req, "my-connect-pswd", password, sizeof(password) - 1);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get password header");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    printf("Received Wi-Fi credentials - SSID: %s, Password: %s\n", ssid, password);
    // Store credentials in NVS and check for errors
    if (!nvs_storage_set_wifi_credentials(ssid, password))
    {
        ESP_LOGE(TAG, "Failed to store WiFi credentials in NVS");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Configure WiFi connection
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    // Disconnect before applying new config
    esp_wifi_disconnect();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    // Update connection status
    http_server_monitor_send_msg(HTTP_MSG_WIFI_CONNECT_INIT);

    // Send success response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"connecting\"}", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t http_server_wifi_connect_status_handler(httpd_req_t *req)
{
    char response[50];
    ESP_LOGI(TAG, "WiFi Connect Status Requested");

    // Create JSON response with current connection status
    snprintf(response, sizeof(response), "{\"wifi_connect_status\":%d}", g_wifi_connect_status);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

static esp_err_t http_server_wifi_connect_info_handler(httpd_req_t *req)
{
    // Only proceed if connected successfully
    if (g_wifi_connect_status != HTTP_WIFI_STATUS_CONNECT_SUCCESS)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    wifi_ap_record_t ap_info;
    esp_netif_ip_info_t ip_info;
    char response[256];

    // Get the connected AP info
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get AP info");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Get IP info
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get IP info");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Convert IP addresses to strings
    char ip[16], netmask[16], gateway[16];
    esp_ip4addr_ntoa(&ip_info.ip, ip, sizeof(ip));
    esp_ip4addr_ntoa(&ip_info.netmask, netmask, sizeof(netmask));
    esp_ip4addr_ntoa(&ip_info.gw, gateway, sizeof(gateway));

    // Create JSON response
    snprintf(response, sizeof(response),
             "{\"ap\":\"%s\",\"ip\":\"%s\",\"netmask\":\"%s\",\"gw\":\"%s\"}",
             (char *)ap_info.ssid, ip, netmask, gateway);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

static esp_err_t http_server_wifi_disconnect_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WiFi Disconnect Requested");

    // Disconnect from the AP
    esp_wifi_disconnect();

    // Update status via message queue
    http_server_monitor_send_msg(HTTP_MSG_WIFI_USER_DISCONNECT);

    // Send confirmation response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"disconnected\"}", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

/**
 * @brief HTTP GET handler for getting the saved station SSID from NVS
 *
 * Respond with a JSON object:
 * {"station_ssid": "your_saved_ssid"} if found, or
 * {"station_ssid": ""} if not found or an error occurs
 *
 * @param req HTTP request for which the URI needs to be handled
 * @return ESP_OK on success, ESP_FAIL on failure
 */
static esp_err_t http_server_get_saved_station_ssid_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Saved Station SSID Requested");
    char station_ssid[64] = {0};
    char station_password[64] = {0};

    // Create JSON response
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Try to get saved SSID from NVS
    bool success = nvs_storage_get_wifi_credentials(station_ssid, sizeof(station_ssid),
                                                    station_password, sizeof(station_password));

    if (success && station_ssid[0] != '\0')
    {
        ESP_LOGI(TAG, "Found saved SSID: %s", station_ssid);
        cJSON_AddStringToObject(root, "station_ssid", station_ssid);
    }
    else
    {
        ESP_LOGW(TAG, "No saved station SSID found in NVS");
        cJSON_AddStringToObject(root, "station_ssid", "");
    }

    // Generate JSON string
    const char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL)
    {
        ESP_LOGE(TAG, "Failed to generate JSON string");
        cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Send response
    httpd_resp_set_type(req, "application/json");
    esp_err_t send_err = httpd_resp_send(req, json_str, strlen(json_str));

    // Cleanup
    cJSON_Delete(root);
    free((void *)json_str);

    if (send_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send response: %s", esp_err_to_name(send_err));
        return send_err;
    }

    return ESP_OK;
}

// --- RFID Management Webpage Handlers ---

/*
 * Sends the rfid_management.html page
 * @param req HTTP request for which the uri needs to be handled
 * @return ESP_OK
 */
static esp_err_t http_server_rfid_management_html_handler(httpd_req_t *req)
{
    esp_err_t error;
    ESP_LOGI(TAG, "RFID Management HTML Requested");
    
    // Set content type
    httpd_resp_set_type(req, "text/html");
    
    // Strong cache control headers to prevent any caching of dynamic content
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0, post-check=0, pre-check=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "-1");
    // Add a Vary header to ensure proxy servers don't cache
    httpd_resp_set_hdr(req, "Vary", "*");
    
    // For small files like HTML, we can send directly without chunking
    error = httpd_resp_send(req, (const char *)rfid_management_html_start, rfid_management_html_end - rfid_management_html_start);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "http_server_rfid_management_html_handler: Error %d while sending Response", error);
    }
    else
    {
        ESP_LOGI(TAG, "http_server_rfid_management_html_handler: Response Sent Successfully");
    }
    return error;
}

/*
 * rfid_management.js get handler requested when accessing the rfid management page.
 * @param req HTTP request for which the uri needs to be handled
 * @return ESP_OK
 */
static esp_err_t http_server_rfid_management_js_handler(httpd_req_t *req)
{
    esp_err_t error;
    ESP_LOGI(TAG, "RFID Management JS Requested");
    
    // Set content type
    httpd_resp_set_type(req, "application/javascript");
    
    // Strong cache control headers to prevent any caching of dynamic content
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0, post-check=0, pre-check=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "-1");
    // Add a Vary header to ensure proxy servers don't cache
    httpd_resp_set_hdr(req, "Vary", "*");
    
    
    // Send the file
    error = httpd_resp_send(req, (const char *)rfid_management_js_start, rfid_management_js_end - rfid_management_js_start);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "http_server_rfid_management_js_handler: Error %d while sending Response", error);
    }
    else
    {
        ESP_LOGI(TAG, "http_server_rfid_management_js_handler: Response Sent Successfully");
    }
    return error;
}

// --- RFID Management API Handlers ---

// GET /api/rfid/cards - List all cards
static esp_err_t http_server_rfid_list_cards_handler(httpd_req_t *req)
{
    char *resp_str = "{\"status\":\"Failed\"}";

    httpd_resp_set_type(req, "application/json");

    ESP_LOGI(TAG, "/api/rfid/cards (GET) requested - SIMPLIFIED HANDLER");

    if (rfid_manager_get_card_list_json(http_server_buffer, HTTP_SERVER_BUFFER_SIZE) != ESP_FAIL)
    {
        resp_str = http_server_buffer; // Assigning the address of buffer to pointer

        httpd_resp_set_status(req, HTTPD_200);
    }
    else
    {
        httpd_resp_set_status(req, HTTPD_400);
    }

    esp_err_t ret = httpd_resp_send(req, resp_str, strlen(resp_str));

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Simplified handler failed to send response: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

// POST /api/rfid/cards - Add new card
static esp_err_t http_server_rfid_add_card_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/api/rfid/cards (POST) requested");
    char content[256];
    int recv_len = httpd_req_recv(req, content, sizeof(content) - 1);
    if (recv_len <= 0)
    {
        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[recv_len] = '\0';

    cJSON *json = cJSON_Parse(content);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON for add card: %s", content);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON format");
        return ESP_FAIL;
    }

    cJSON *card_id_json = cJSON_GetObjectItem(json, "id");
    cJSON *name_json = cJSON_GetObjectItem(json, "nm");

    if (!cJSON_IsNumber(card_id_json) || !cJSON_IsString(name_json))
    {
        ESP_LOGE(TAG, "Missing 'card_id' or 'name' in JSON, or not numbers & string respectively");
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'card_id' or 'name'");
        return ESP_FAIL;
    }

    uint32_t card_id = card_id_json->valueint;
    if (card_id == 0)
    {
        ESP_LOGE(TAG, "Invalid! card_id is 0");
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid! card_id is 0");
        return ESP_FAIL;
    }

    esp_err_t ret = rfid_manager_add_card(card_id, name_json->valuestring);
    cJSON_Delete(json);

    if (ret == ESP_OK)
    {
        httpd_resp_send(req, "{\"status\":\"success\", \"message\":\"Card added\"}", HTTPD_RESP_USE_STRLEN);
    }
    else if (ret == RFID_MANAGER_ERR_DUPLICATE_ID) // Check for the specific custom error code for duplicate ID
    {
        ESP_LOGW(TAG, "Attempted to add card with duplicate ID.");
        httpd_resp_set_status(req, "409 Conflict"); // Use 409 Conflict status
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"error\", \"message\":\"Card ID already exists\"}", HTTPD_RESP_USE_STRLEN);
    }
    else if (ret == ESP_ERR_NO_MEM) // This error from rfid_manager means storage is full
    {
        ESP_LOGE(TAG, "RFID database full. Sending 507.");
        httpd_resp_set_status(req, "507 Insufficient Storage"); // Set 507 status
        httpd_resp_set_type(req, "application/json");           // Set content type
        httpd_resp_send(req, "{\"status\":\"error\", \"message\":\"Database full - Insufficient Storage\"}", HTTPD_RESP_USE_STRLEN);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to add RFID card: %s", esp_err_to_name(ret));
        httpd_resp_send_500(req);
    }
    return ESP_OK;
}

// DEL /api/rfid/cards/{id} - Remove card
static esp_err_t http_server_rfid_remove_card_handler(httpd_req_t *req)
{
    char urlBuffer[256];
    uint16_t lengthOfURI = 0;
    char idStrBuffer[48];
    uint32_t cardId = 0;

    ESP_LOGI(TAG, "/cards/Delete?id= (DELETE) requested: %s", req->uri);

    memset(idStrBuffer, 0, sizeof(idStrBuffer));

    lengthOfURI = httpd_req_get_url_query_len(req) + 1; // +1 for null terminator

    if (lengthOfURI > 1)
    {
        if (httpd_req_get_url_query_str(req, urlBuffer, lengthOfURI) == ESP_OK)
        {
            ESP_LOGI(TAG, "urlBuffer:%s", urlBuffer);

            if (httpd_query_key_value(urlBuffer, "id", idStrBuffer, sizeof(idStrBuffer)) == ESP_OK)
            {
                ESP_LOGI(TAG, "idStrBuffer:%s", idStrBuffer);

                cardId = strtoul(idStrBuffer, NULL, 10);

                if (cardId == 0)
                {
                    ESP_LOGE(TAG, "Card ID missing in URI");
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Card ID missing in URI");
                    return ESP_FAIL;
                }
            }
        }
    }

    esp_err_t ret = rfid_manager_remove_card(cardId);

    if (ret == ESP_OK)
    {
        httpd_resp_send(req, "{\"status\":\"success\", \"message\":\"Card removed\"}", HTTPD_RESP_USE_STRLEN);
    }
    else if (ret == ESP_ERR_NOT_FOUND)
    {
        char err_msg[100];
        sprintf(err_msg, "Card ID %ld not found", cardId);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, err_msg);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to remove RFID card: %s (Error: %s)", esp_err_to_name(ret), ret == ESP_FAIL ? "Generic Fail" : "Other");
        httpd_resp_send_500(req);
    }
    return ESP_OK;
}

// GET /api/rfid/cards/count - Get card count
static esp_err_t http_server_rfid_get_card_count_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/api/rfid/cards/count (GET) requested");
    uint16_t count = rfid_manager_get_card_count();
    char resp_json[64];
    sprintf(resp_json, "{\"count\":%u}", count);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_json, strlen(resp_json));
    return ESP_OK;
}

// POST /api/rfid/cards/check - Check if card exists
static esp_err_t http_server_rfid_check_card_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/api/rfid/cards/check (POST) requested");
    char content[128]; // Increased size for card_id string
    int recv_len = httpd_req_recv(req, content, sizeof(content) - 1);
    if (recv_len <= 0)
    {
        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[recv_len] = '\0';

    cJSON *json = cJSON_Parse(content);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON for check card: %s", content);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON format");
        return ESP_FAIL;
    }

    cJSON *card_id_json = cJSON_GetObjectItem(json, "card_id");
    if (!cJSON_IsString(card_id_json))
    {
        ESP_LOGE(TAG, "Missing 'card_id' in JSON or not a string");
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'card_id'");
        return ESP_FAIL;
    }

    uint32_t card_id = (uint32_t)strtoul(card_id_json->valuestring, NULL, 0);
    if (card_id == 0 && strcmp(card_id_json->valuestring, "0x0") != 0 && strcmp(card_id_json->valuestring, "0") != 0)
    {
        ESP_LOGE(TAG, "Invalid card_id format: %s", card_id_json->valuestring);
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid card_id format. Must be hex (e.g. 0x1234ABCD) or decimal.");
        return ESP_FAIL;
    }

    cJSON_Delete(json);

    bool exists = rfid_manager_check_card(card_id);
    char resp_json[64];
    sprintf(resp_json, "{\"exists\":%s, \"card_id\":\"%lu\"}", exists ? "true" : "false", (unsigned long)card_id);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_json, strlen(resp_json));
    return ESP_OK;
}

// POST /api/rfid/reset - Reset to default cards
static esp_err_t http_server_rfid_reset_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/api/rfid/reset (POST) requested");
    esp_err_t ret = rfid_manager_format_database();
    if (ret == ESP_OK)
    {
        httpd_resp_send(req, "{\"status\":\"success\", \"message\":\"RFID database reset to defaults\"}", HTTPD_RESP_USE_STRLEN);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to reset RFID database: %s", esp_err_to_name(ret));
        httpd_resp_send_500(req);
    }
    return ESP_OK;
}

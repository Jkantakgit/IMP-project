/**
 * @file main.c
 * @author xholanp00
 * @brief ESP32 PIR motion sensor that triggers HTTP POST requests to a server to capture photos.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <string.h>
#include <arpa/inet.h>
#include "esp_http_client.h"
#include "esp_timer.h"
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>

#define TAG "PIR" // Tag for logging

// Wi-Fi credentials: change to your network
#define WIFI_SSID "SS"
#define WIFI_PASS "superSecret"

/* HTTP endpoint and paths */
#define RECORD_HOST "192.168.4.1"
#define RECORD_PORT 80
#define RECORD_PATH "/photo"
#define TIME_PATH "/time"
#define HTTP_TIMEOUT_MS 30000

// Time sync: offset (server_time - local_monotonic_ms) in milliseconds
static volatile int64_t g_time_offset_ms = 0;
// how often to sync time (ms) 
#define TIME_SYNC_INTERVAL_MS (5 * 60 * 1000) // 5 minutes

#define MAX_CAPTURE_RETRIES 3 // Maximum number of capture retries

#define PIR_GPIO     GPIO_NUM_4 // GPIO pin connected to PIR sensor

static QueueHandle_t gpio_evt_queue = NULL; // Queue for GPIO events
static QueueHandle_t pub_queue = NULL; // Queue for publish events


/* HTTP event handler buffer to accumulate response body */
struct http_accum {
    char *buf;
    size_t len;
    size_t cap;
};

/**
 * @brief HTTP event handler to accumulate response data
 * 
 * @param evt esp_http_client_event_t* Event data
 * @return esp_err_t ESP_OK on success, ESP_FAIL on failure
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt){

    // Handle HTTP events and accumulate response data
    struct http_accum *acc = (struct http_accum *)evt->user_data;
    if (!acc) return ESP_OK;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        if (!acc->buf) {
            acc->cap = evt->data_len + 256;
            acc->buf = malloc(acc->cap);
            acc->len = 0;
            if (!acc->buf) return ESP_FAIL;
        }
        if (acc->len + evt->data_len + 1 > acc->cap) {
            size_t new_cap = acc->cap * 2 + evt->data_len;
            char *new_buf = realloc(acc->buf, new_cap);
            if (!new_buf) return ESP_FAIL;
            acc->buf = new_buf;
            acc->cap = new_cap;
        }
        memcpy(acc->buf + acc->len, evt->data, evt->data_len);
        acc->len += evt->data_len;
        acc->buf[acc->len] = '\0';
    }
    return ESP_OK;
}

/**
 * @brief Synchronize the local time with the server time.
 * 
 * @return esp_err_t ESP_OK on success, ESP_FAIL on failure
 */
static esp_err_t sync_time_with_server(void){
    ESP_LOGI(TAG, "Starting time sync with server");
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s", RECORD_HOST, RECORD_PORT, TIME_PATH);

    struct http_accum acc = {0};

    // Configure HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = 4096,
        .event_handler = http_event_handler,
        .user_data = &acc,
    };

    // Initialize HTTP client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGW(TAG, "time sync: failed to init http client");
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Accept", "application/json");

    // Perform HTTP request
    esp_err_t err = esp_http_client_perform(client);

    // Check for errors and HTTP status code
    int status = esp_http_client_get_status_code(client);
    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "time sync: request failed: err=%s status=%d",
                 esp_err_to_name(err), status);
        if (acc.buf) free(acc.buf);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // Read response body
    char rbuf[256];
    if (acc.buf && acc.len > 0) {
        int len = (int)acc.len;
        if (len > (int)sizeof(rbuf) - 1) len = (int)sizeof(rbuf) - 1;
        memcpy(rbuf, acc.buf, len);
        rbuf[len] = '\0';
    }

    // Parse timestamp from JSON response
    unsigned long long ts = 0;
    char *p = strstr(rbuf, "\"time_ms\"");
    if (p) {
        while (*p && *p != ':') p++;
        if (*p == ':') p++;
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (*p) ts = strtoull(p, NULL, 10);
    }

    // Cleanup
    if (acc.buf) free(acc.buf);
    esp_http_client_cleanup(client);

    // Update time offset if timestamp was found
    if (ts > 0) {
        uint64_t now_ms_rel = (uint64_t)(esp_timer_get_time() / 1000ULL);
        g_time_offset_ms = (int64_t)ts - (int64_t)now_ms_rel;
        return ESP_OK;
    }
    
    return ESP_FAIL;
}

/**
 * @brief Time synchronization task.
 * 
 * @param pv 
 */
static void time_sync_task(void *pv) {
    (void)pv;
    sync_time_with_server();
    vTaskDelete(NULL);
}

/**
 * @brief WiFi event handler.
 * 
 * @param arg Argument pointer
 * @param event_base 
 * @param event_id 
 * @param event_data 
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){

    // Handle WiFi and IP events
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi started, connecting...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                {
                    wifi_event_sta_disconnected_t *dis = (wifi_event_sta_disconnected_t *)event_data;
                    ESP_LOGW(TAG, "WiFi disconnected (reason=%d), reconnecting...", dis ? dis->reason : -1);
                }
                esp_wifi_connect();
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        // If got IP event
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            char ip_str[16];
            uint32_t ip = ntohl(event->ip_info.ip.addr);
            snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
                     (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
                     (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF));
            ESP_LOGI(TAG, "Got IP: %s", ip_str);
            // Start time sync task
            xTaskCreate(time_sync_task, "time_sync", 4096, NULL, 5, NULL);
        }
    }
}

/**
 * @brief Initialize WiFi in station mode.
 * 
 */
static void wifi_init_sta(void){
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Configure WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Set WiFi configuration
    wifi_config_t wifi_config = { 0 };
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid)-1);
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password)-1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

/**
 * @brief GPIO interrupt service routine handler.
 * 
 * @param arg GPIO number
 */
static void IRAM_ATTR gpio_isr_handler(void* arg){
    // Get GPIO number from argument
    uint32_t gpio_num = (uint32_t) arg;
    // Disable further interrupts for this pin immediately
    gpio_intr_disable((gpio_num_t)gpio_num);
    // Notify the GPIO event queue
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief GPIO task to handle GPIO events.
 * 
 * @param arg Argument pointer
 */
static void gpio_task(void* arg){
    uint32_t io_num;
    // Main loop to process GPIO events
    for (;;) {
        // Wait for GPIO event
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            vTaskDelay(pdMS_TO_TICKS(50));

            // Get current timestamp in milliseconds
            uint64_t ts_ms = esp_timer_get_time() / 1000ULL;
            if (pub_queue) {
                // Send timestamp to publisher queue
                xQueueSend(pub_queue, &ts_ms, 0);
            }

            vTaskDelay(pdMS_TO_TICKS(5000));
            gpio_intr_enable((gpio_num_t)io_num);
        }
    }
}

/**
 * @brief Publisher task to perform HTTP POST requests.
 * 
 * @param arg Argument pointer
 */
static void publisher_task(void* arg){

    (void)arg;
    uint64_t ts_ms;
    uint64_t last_sync_ms = 0;
    // Main loop to process publish events
    for (;;) {
        // Wait for publish event
        if (xQueueReceive(pub_queue, &ts_ms, portMAX_DELAY) == pdTRUE) {
            uint64_t now_ms_rel = (uint64_t)(esp_timer_get_time() / 1000ULL);
            // Check if time sync is needed
            if (last_sync_ms == 0 || (now_ms_rel - last_sync_ms) > TIME_SYNC_INTERVAL_MS) {
                if (sync_time_with_server() == ESP_OK) {
                    last_sync_ms = now_ms_rel;
                }
            }

            // Calculate server time for capture
            uint64_t server_now = now_ms_rel + (uint64_t)g_time_offset_ms;

            // Prepare HTTP POST payload
            char payload[64];
            int len = snprintf(payload, sizeof(payload), "capture:%llu", (unsigned long long)server_now);

            // Configure HTTP client for capture request
            char url[128];
            snprintf(url, sizeof(url), "http://%s:%d%s", RECORD_HOST, RECORD_PORT, RECORD_PATH);

            // Set up HTTP client configuration
            esp_http_client_config_t config = {
                .url = url,
                .timeout_ms = HTTP_TIMEOUT_MS,
                .transport_type = HTTP_TRANSPORT_OVER_TCP,
                .buffer_size = 4096,
            };

            int attempt = 0;
            while (attempt <= MAX_CAPTURE_RETRIES) {
                esp_http_client_handle_t client = esp_http_client_init(&config);
                if (!client) break;
                esp_http_client_set_method(client, HTTP_METHOD_POST);
                esp_http_client_set_header(client, "Content-Type", "text/plain");
                esp_http_client_set_post_field(client, payload, len);
                esp_err_t err = esp_http_client_perform(client);
                int status = esp_http_client_get_status_code(client);
                if (err == ESP_OK && status == 200) {
                    esp_http_client_cleanup(client);
                    break;
                }

                char rbuf[512];
                int rtotal = 0;
                esp_err_t fh = esp_http_client_fetch_headers(client);
                int64_t content_len = esp_http_client_get_content_length(client);
                if (content_len > 0) {
                    int toread = (int)content_len;
                    if (toread > (int)sizeof(rbuf) - 1) toread = (int)sizeof(rbuf) - 1;
                    while (rtotal < toread) {
                        int rr = esp_http_client_read(client, rbuf + rtotal, toread - rtotal);
                        if (rr <= 0) break;
                        rtotal += rr;
                    }
                } else {
                    while (rtotal < (int)sizeof(rbuf) - 1) {
                        int rr = esp_http_client_read(client, rbuf + rtotal, sizeof(rbuf) - 1 - rtotal);
                        if (rr <= 0) break;
                        rtotal += rr;
                    }
                }
                if (rtotal > 0) rbuf[rtotal] = '\0'; else rbuf[0] = '\0';

                esp_http_client_cleanup(client);

                if (attempt < MAX_CAPTURE_RETRIES && sync_time_with_server() == ESP_OK) {
                    uint64_t now_ms_rel2 = (uint64_t)(esp_timer_get_time() / 1000ULL);
                    uint64_t server_now2 = now_ms_rel2 + (uint64_t)g_time_offset_ms + 50;
                    len = snprintf(payload, sizeof(payload), "capture:%llu", (unsigned long long)server_now2);
                } else {
                    break;
                }
                attempt++;
            }
        }
    }
}

/**
 * @brief Main application entry point.
 * 
 */
void app_main(void){
    
    // Initialize WiFi
    wifi_init_sta();

    // Configure PIR GPIO pin
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << PIR_GPIO);
    io_conf.pull_down_en = 1;
    gpio_config(&io_conf);

    // Create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // Create queue for publisher 
    pub_queue = xQueueCreate(10, sizeof(uint64_t));

    // Start gpio task
    xTaskCreate(gpio_task, "gpio_task", 2048, NULL, 5, NULL);

    // Start publisher task with large stack for HTTP/TLS work
    xTaskCreate(publisher_task, "publisher_task", 8192, NULL, 5, NULL);

    // Install GPIO ISR service and attach handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIR_GPIO, gpio_isr_handler, (void*) PIR_GPIO);

    // Main loop does nothing, all work is done in tasks
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

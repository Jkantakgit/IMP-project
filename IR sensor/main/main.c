/* PIR interrupt example

   Demonstrates using a GPIO interrupt to detect a PIR motion sensor
   and notify a FreeRTOS task which logs the event and briefly
   flashes an LED.
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

#define TAG "PIR"

// Wi-Fi credentials: change to your network
#define WIFI_SSID "180"
#define WIFI_PASS "kolbeckW"

// HTTP endpoint to post records to (change to your recorder IP)
#define RECORD_HOST "10.180.0.113"
#define RECORD_PORT 80
#define RECORD_PATH "/record"
#define HTTP_TIMEOUT_MS 5000

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi started, connecting...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
                esp_wifi_connect();
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            char ip_str[16];
            uint32_t ip = ntohl(event->ip_info.ip.addr);
            snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
                     (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
                     (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF));
            ESP_LOGI(TAG, "Got IP: %s", ip_str);
        }
    }
}

static void wifi_init_sta(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid)-1);
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password)-1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

// Change these to match your wiring
#define PIR_GPIO            GPIO_NUM_4
// Wemos built-in LED is typically on GPIO2 and is active-low
#define LED_GPIO            GPIO_NUM_2
#define LED_ACTIVE_LOW      1

// Helper macros to handle active-low built-in LEDs
#define LED_ON()  gpio_set_level(LED_GPIO, (LED_ACTIVE_LOW) ? 0 : 1)
#define LED_OFF() gpio_set_level(LED_GPIO, (LED_ACTIVE_LOW) ? 1 : 0)

static volatile uint32_t motion_count = 0;
static QueueHandle_t gpio_evt_queue = NULL;
static QueueHandle_t pub_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    // Disable further interrupts for this pin immediately
    gpio_intr_disable((gpio_num_t)gpio_num);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void gpio_task(void* arg)
{
    uint32_t io_num;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            // lightweight handling in task context
            motion_count++;
            LED_ON();
            vTaskDelay(pdMS_TO_TICKS(50));
            LED_OFF();

            // Send timestamp to publisher task (non-blocking)
            {
                uint64_t ts_ms = esp_timer_get_time() / 1000ULL;
                if (pub_queue) {
                    BaseType_t ok = xQueueSend(pub_queue, &ts_ms, 0);
                    if (ok != pdTRUE) {
                        ESP_LOGW(TAG, "Pub queue full, dropping event");
                    }
                }
            }

            // Hold interrupts disabled for 1 second to debounce / prevent retrigger
            vTaskDelay(pdMS_TO_TICKS(1000));

            // Re-enable interrupt for the pin
            gpio_intr_enable((gpio_num_t)io_num);
        }
    }
}

// Publisher task: performs HTTP POSTs. Runs with a larger stack.
static void publisher_task(void* arg)
{
    (void)arg;
    uint64_t ts_ms;
    for (;;) {
        if (xQueueReceive(pub_queue, &ts_ms, portMAX_DELAY) == pdTRUE) {
            char payload[64];
            int len = snprintf(payload, sizeof(payload), "record:%llu", (unsigned long long)ts_ms);

                char url[128];
                snprintf(url, sizeof(url), "http://%s:%d%s", RECORD_HOST, RECORD_PORT, RECORD_PATH);

                esp_http_client_config_t config = {
                    .url = url,
                    .timeout_ms = HTTP_TIMEOUT_MS,
                    .transport_type = HTTP_TRANSPORT_OVER_TCP,
                    .buffer_size = 2048,
                };
                ESP_LOGI(TAG, "POST -> %s payload='%s'", url, payload);
            esp_http_client_handle_t client = esp_http_client_init(&config);
            if (client) {
                esp_http_client_set_method(client, HTTP_METHOD_POST);
                esp_http_client_set_header(client, "Content-Type", "text/plain");
                esp_http_client_set_post_field(client, payload, len);
                esp_err_t err = esp_http_client_perform(client);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "POST sent, status=%d", esp_http_client_get_status_code(client));
                } else {
                    ESP_LOGW(TAG, "POST failed: %s", esp_err_to_name(err));
                }
                esp_http_client_cleanup(client);
            } else {
                ESP_LOGW(TAG, "Failed to init HTTP client");
            }
        }
    }
}

void app_main(void)
{
    // Configure LED GPIO
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << LED_GPIO);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    // Ensure LED starts in OFF state
    LED_OFF();

    // Initialize Wi-Fi in station mode
    wifi_init_sta();

    // Configure PIR GPIO as input with rising edge interrupt
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << PIR_GPIO);
    io_conf.pull_down_en = 1; // enable pull-down if sensor leaves line floating
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    // Create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // Create queue for publisher (timestamps)
    pub_queue = xQueueCreate(10, sizeof(uint64_t));

    // Start gpio task (lower priority)
    xTaskCreate(gpio_task, "gpio_task", 2048, NULL, 5, NULL);

    // Start publisher task with large stack for HTTP/TLS work
    xTaskCreate(publisher_task, "publisher_task", 8192, NULL, 5, NULL);

    // Install GPIO ISR service and attach handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIR_GPIO, gpio_isr_handler, (void*) PIR_GPIO);

    ESP_LOGI(TAG, "PIR interrupt setup complete (PIR_GPIO=%d, LED_GPIO=%d)", PIR_GPIO, LED_GPIO);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

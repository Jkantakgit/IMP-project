

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "recorder.h"
#include "wifi_helpers.h"
#include "sd_card_helpers.h"
#include "file_server.h"
#include "driver/gpio.h"

static const char *TAG = "example";


void app_main(void)
{
    /* Initialize NVS and network stack */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Start Wi-Fi Access Point */
    const char *ap_ssid = "SS";
    const char *ap_pass = "superSecret";
    ESP_ERROR_CHECK(wifi_helpers_init_ap(ap_ssid, ap_pass));

    /* Initialize recorder for video */
    esp_err_t rc = recorder_init();
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "recorder_init failed (%s)", esp_err_to_name(rc));
    } else {
        ESP_LOGI(TAG, "Video recorder initialized");
    }

    /* Disable LED on GPIO4 to prevent interference */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_4),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_4, 0);

    /* Mount SD card via SDSPI */
    const char* base_path = "/data";
    esp_err_t err = sd_card_mount_sdspi(base_path, GPIO_NUM_15, GPIO_NUM_2, GPIO_NUM_14, GPIO_NUM_13);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "SD card mounted at %s", base_path);
    }

    /* Start file server */
    ESP_LOGI(TAG, "Starting file server");
    ESP_ERROR_CHECK(example_start_file_server("/data"));
    ESP_LOGI(TAG, "File server at http://192.168.4.1/");

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

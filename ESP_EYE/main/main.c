

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "recorder.h"
#include "wifi_helpers.h"
#include "sd_card_helpers.h"
#include "file_server.h"
#include "driver/gpio.h"

static const char *TAG = "example"; // Tag for logging


void app_main(void){
    /* Initialize NVS and network stack */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(wifi_helpers_init_ap("SS", "superSecret"));
    ESP_ERROR_CHECK(recorder_init());

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_4),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_4, 0);

    ESP_ERROR_CHECK(sd_card_mount("/data"));

    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_vfs_spiffs_register(&spiffs_conf);

    ESP_ERROR_CHECK(example_start_file_server("/spiffs", "/data"));

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}



#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "nvs_flash.h"
#include "recorder.h"
#include "wifi_helpers.h"
#include "sd_card_helpers.h"
#include "file_server.h"


static const char *TAG = "example";

void app_main(void)
{
    size_t heap_total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    size_t heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);

    esp_err_t psram_rc = esp_psram_init();
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Heap total=%d free=%d, PSRAM total=%d free=%d, psram_init=%s", (int)heap_total, (int)heap_free, (int)psram_total, (int)psram_free, esp_err_to_name(psram_rc));

    /* Initialize NVS and network stack before other subsystems (Wi‑Fi needs NVS). */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Configure and start Wi-Fi Access Point (softAP) */
    ESP_LOGI(TAG, "Starting Wi-Fi softAP");
    const char *ap_ssid = "SS";
    const char *ap_pass = "superSecret";
    ESP_ERROR_CHECK(wifi_helpers_init_ap(ap_ssid, ap_pass));

    /* Initialize camera/recorder */
    {
        esp_err_t rc = recorder_init();
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "recorder_init failed (%s); continuing without camera", esp_err_to_name(rc));
        } else {
            ESP_LOGI(TAG, "recorder initialized");
        }
    }

    /* Initialize SD card in SPI mode (non-conflicting pins) */
    const char* base_path = "/data";
    esp_err_t err = sd_card_mount(base_path);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sd_card_mount failed (%s)", esp_err_to_name(err));
    }

    /* Start the file server to serve index.html from SD card */
    ESP_LOGI(TAG, "Starting file server");
    ESP_ERROR_CHECK(example_start_file_server("/data"));
    ESP_LOGI(TAG, "File server started. Access at http://192.168.4.1/");

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

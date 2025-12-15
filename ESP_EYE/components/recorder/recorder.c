
#include "recorder.h"
#include "esp_log.h"
#include "esp_camera.h"
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "recorder";

// Camera configuration for ESP32-CAM AI-Thinker
static camera_config_t camera_config = {
    .pin_pwdn = 32,
    .pin_reset = -1,
    .pin_xclk = 0,
    .pin_sccb_sda = 26,
    .pin_sccb_scl = 27,
    .pin_d7 = 35,
    .pin_d6 = 34,
    .pin_d5 = 39,
    .pin_d4 = 36,
    .pin_d3 = 21,
    .pin_d2 = 19,
    .pin_d1 = 18,
    .pin_d0 = 5,
    .pin_vsync = 25,
    .pin_href = 23,
    .pin_pclk = 22,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_SVGA,
    .jpeg_quality = 12,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY
};

esp_err_t recorder_init(void)
{
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Camera initialized successfully");
    return ESP_OK;
}

esp_err_t recorder_deinit(void)
{
    esp_err_t err = esp_camera_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera deinit failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Camera deinitialized");
    return ESP_OK;
}

esp_err_t recorder_capture_to_file(const char *filepath)
{
    /* Capture image */
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera captured %d bytes", fb->len);

    /* Open file for writing */
    FILE *f = fopen(filepath, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filepath);
        esp_camera_fb_return(fb);
        return ESP_FAIL;
    }

    /* Write JPEG data to file */
    size_t written = fwrite(fb->buf, 1, fb->len, f);
    fclose(f);

    /* Return frame buffer */
    esp_camera_fb_return(fb);

    if (written != fb->len) {
        ESP_LOGE(TAG, "Failed to write complete image (wrote %d of %d bytes)", written, fb->len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Image saved to %s (%d bytes)", filepath, written);
    return ESP_OK;
}

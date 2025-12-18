
#include "recorder.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

static const char *TAG = "recorder";

/* Optional LED/flash GPIO to toggle while capturing a photo.
 * Set to -1 to disable. Override by defining RECORDER_LED_GPIO
 * in your build (e.g., via CMake) if a different pin is required.
 */
#ifndef RECORDER_LED_GPIO
#define RECORDER_LED_GPIO 4
#endif

static bool recorder_led_configured = false;


// Camera configurations (primary = XGA quality, fallback = VGA)
static camera_config_t camera_config_primary = {
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
    .frame_size = FRAMESIZE_XGA,      // 1024x768 - XGA quality
    .jpeg_quality = 12,               // slightly higher quality value for stability
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST
};

static camera_config_t camera_config_fallback = {
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
    .frame_size = FRAMESIZE_VGA,      // 640x480 - VGA fallback
    .jpeg_quality = 12,
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST
};

esp_err_t recorder_init(void)
{
    esp_err_t err = esp_camera_init(&camera_config_primary);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Camera initialized with primary config (XGA 1024x768, quality 10)");
        
        // Apply sensor tuning for better image quality
        sensor_t * s = esp_camera_sensor_get();
        s->set_brightness(s, 0);     // -2 to 2
        s->set_contrast(s, 0);       // -2 to 2
        s->set_saturation(s, 0);     // -2 to 2
        s->set_whitebal(s, 1);       // enable white balance
        s->set_awb_gain(s, 1);       // enable auto white balance gain
        s->set_wb_mode(s, 0);        // auto white balance mode
        s->set_exposure_ctrl(s, 1);  // enable auto exposure
        s->set_aec2(s, 0);           // disable AEC2
        s->set_ae_level(s, 0);       // auto exposure level -2 to 2
        s->set_aec_value(s, 300);    // auto exposure value 0-1200
        s->set_gain_ctrl(s, 1);      // enable auto gain
        s->set_agc_gain(s, 0);       // auto gain value 0-30
        s->set_gainceiling(s, (gainceiling_t)0);  // gain ceiling
        s->set_bpc(s, 0);            // disable black pixel correction
        s->set_wpc(s, 1);            // enable white pixel correction
        s->set_raw_gma(s, 1);        // enable gamma correction
        s->set_lenc(s, 1);           // enable lens correction
        s->set_hmirror(s, 0);        // disable horizontal mirror
        s->set_vflip(s, 0);          // disable vertical flip
        s->set_dcw(s, 1);            // enable downsize

            /* Configure optional LED GPIO (primary init path) */
            if (RECORDER_LED_GPIO >= 0) {
                gpio_reset_pin(RECORDER_LED_GPIO);
                gpio_set_direction(RECORDER_LED_GPIO, GPIO_MODE_OUTPUT);
                gpio_set_level(RECORDER_LED_GPIO, 0);
                recorder_led_configured = true;
                ESP_LOGI(TAG, "Recorder LED configured on GPIO %d", RECORDER_LED_GPIO);
            }

            return ESP_OK;
    }

    ESP_LOGW(TAG, "Primary camera init failed (%s), trying VGA fallback", esp_err_to_name(err));

    err = esp_camera_init(&camera_config_fallback);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallback camera init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Camera initialized with fallback config (VGA 640x480, quality 15, DRAM)");
    
    // Apply basic sensor tuning
    sensor_t * s = esp_camera_sensor_get();
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_lenc(s, 1);

    /* Configure optional LED GPIO (fallback init path) */
    if (RECORDER_LED_GPIO >= 0) {
        gpio_reset_pin(RECORDER_LED_GPIO);
        gpio_set_direction(RECORDER_LED_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_level(RECORDER_LED_GPIO, 0);
        recorder_led_configured = true;
        ESP_LOGI(TAG, "Recorder LED configured on GPIO %d", RECORDER_LED_GPIO);
    }

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

esp_err_t recorder_capture_to_file(const char *filepath, framesize_t frame_size, int jpeg_quality)
{
    /* Optionally adjust sensor settings for faster capture */
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        if (frame_size != (framesize_t)-1) {
            s->set_framesize(s, frame_size);
        }
        if (jpeg_quality >= 0) {
            s->set_quality(s, jpeg_quality);
        }
    }
    /* Turn on optional LED/flash to indicate capture */
    if (recorder_led_configured) {
        gpio_set_level(RECORDER_LED_GPIO, 1);
        /* give LED a short moment to power up */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    /* Capture image with retries */
    camera_fb_t *fb = NULL;
    for (int i = 0; i < 3; i++) {
        fb = esp_camera_fb_get();
        if (fb) {
            break;
        }
        ESP_LOGW(TAG, "Camera capture failed, retry %d", i + 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed after retries");
        if (recorder_led_configured) {
            gpio_set_level(RECORDER_LED_GPIO, 0);
        }
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera captured %d bytes", fb->len);

    /* Open file for writing */
    FILE *f = fopen(filepath, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filepath);
        esp_camera_fb_return(fb);
        if (recorder_led_configured) {
            gpio_set_level(RECORDER_LED_GPIO, 0);
        }
        return ESP_FAIL;
    }

    /* Write JPEG data to file */
    size_t written = fwrite(fb->buf, 1, fb->len, f);
    /* Ensure data is flushed to the SD card for durability and caching behavior */
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    /* Return frame buffer */
    esp_camera_fb_return(fb);

    /* Turn off optional LED/flash now that capture is done */
    if (recorder_led_configured) {
        gpio_set_level(RECORDER_LED_GPIO, 0);
    }


    if (written != fb->len) {
        ESP_LOGE(TAG, "Failed to write complete image (wrote %d of %d bytes)", written, fb->len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Image saved to %s (%d bytes)", filepath, written);
    return ESP_OK;
}

esp_err_t recorder_set_grayscale(bool enable)
{
    /* Not implemented: changing to PIXFORMAT_GRAYSCALE yields raw frames
       (non-JPEG). Proper grayscale JPEG would require additional encoding
       which is outside the scope here. Return not supported. */
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
}

/* Background task wrapper to call recorder_capture_to_file asynchronously.
   Expects `param` to be a `char *` path (heap-allocated) which will be freed
   by this task when done. */
void recorder_capture_to_file_async_task(void *param)
{
    char *path = (char *)param;
    if (!path) {
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Async capture started: %s", path);
    esp_err_t res = recorder_capture_to_file(path, FRAMESIZE_VGA, 30);
    if (res == ESP_OK) {
        ESP_LOGI(TAG, "Async capture succeeded: %s", path);
    } else {
        ESP_LOGE(TAG, "Async capture failed (%d): %s", res, path);
    }

    free(path);
    vTaskDelay(pdMS_TO_TICKS(10));
    vTaskDelete(NULL);
}

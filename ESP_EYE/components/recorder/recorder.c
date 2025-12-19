
#include "recorder.h"


static const char *TAG = "recorder"; // Tag for logging

// Capture queue and worker task handle
static QueueHandle_t s_capture_queue = NULL;
static TaskHandle_t s_capture_worker = NULL;

#ifndef RECORDER_LED_GPIO
#define RECORDER_LED_GPIO 4
#endif

#ifndef RECORDER_WORKER_PRIORITY
#define RECORDER_WORKER_PRIORITY (tskIDLE_PRIORITY + 5)
#endif

static bool recorder_led_configured = false;

/**
 * @brief Capture worker task
 * 
 * @param arg 
 */
static void capture_worker_task(void *arg){
    (void)arg;
    for (;;) {
        char *path = NULL;
        if (xQueueReceive(s_capture_queue, &path, portMAX_DELAY) == pdTRUE) {
            if (path) {
                esp_err_t res = recorder_capture_to_file(path, FRAMESIZE_VGA, 30);
                if (res != ESP_OK) {
                    ESP_LOGE(TAG, "Capture failed: %s", path);
                }
                free(path);
            }
        }
    }
}


/**
 * @brief Primary camera configuration (XGA)
 * 
 */
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
    .frame_size = FRAMESIZE_XGA,
    .jpeg_quality = 12,
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST
};

/**
 * @brief Fallback camera configuration (VGA)
 * 
 */
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
    .frame_size = FRAMESIZE_VGA,
    .jpeg_quality = 12,
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST
};

/**
 * @brief Start the recorder worker task
 * 
 */
static void recorder_start_worker(void){
    if (!s_capture_queue) {
        s_capture_queue = xQueueCreate(8, sizeof(char *));
        if (!s_capture_queue) {
            ESP_LOGE(TAG, "Failed to create capture queue");
            return;
        }
        xTaskCreate(capture_worker_task, "rec_cap_worker", 12288, NULL, RECORDER_WORKER_PRIORITY, &s_capture_worker);
    }
}

/**
 * @brief Stop the recorder worker task
 * 
 */
static void recorder_stop_worker(void){
    if (s_capture_worker) {
        vTaskDelete(s_capture_worker);
        s_capture_worker = NULL;
    }
    if (s_capture_queue) {
        vQueueDelete(s_capture_queue);
        s_capture_queue = NULL;
    }
}

/**
 * @brief Initialize the recorder
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t recorder_init(void){
    // Try primary camera configuration first
    esp_err_t err = esp_camera_init(&camera_config_primary);
    // Apply sensor tuning
    if (err == ESP_OK) {
        
        sensor_t * s = esp_camera_sensor_get();
        s->set_brightness(s, 0);
        s->set_contrast(s, 0);
        s->set_saturation(s, 0);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_wb_mode(s, 0);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 0);
        s->set_ae_level(s, 0);
        s->set_aec_value(s, 300);
        s->set_gain_ctrl(s, 1);
        s->set_agc_gain(s, 0);
        s->set_gainceiling(s, (gainceiling_t)0);
        s->set_bpc(s, 0);
        s->set_wpc(s, 1);
        s->set_raw_gma(s, 1);
        s->set_lenc(s, 1);
        s->set_hmirror(s, 0);
        s->set_vflip(s, 0);
        s->set_dcw(s, 1);

        /* Configure optional LED GPIO (primary init path) */
        if (RECORDER_LED_GPIO >= 0) {
            gpio_reset_pin(RECORDER_LED_GPIO);
            gpio_set_direction(RECORDER_LED_GPIO, GPIO_MODE_OUTPUT);
            gpio_set_level(RECORDER_LED_GPIO, 0);
            recorder_led_configured = true;
        }

        recorder_start_worker();
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Primary camera init failed, trying VGA fallback");

    err = esp_camera_init(&camera_config_fallback);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallback camera init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Camera initialized with fallback config (VGA)");
    
    // Apply sensor tuning
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
    }

    recorder_start_worker();
    return ESP_OK;
}

/**
 * @brief Deinitialize the recorder
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t recorder_deinit(void){
    esp_err_t err = esp_camera_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera deinit failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

/**
 * @brief Enqueue a capture request
 * 
 * @param filepath Path to save the captured image
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t recorder_enqueue_capture(char *filepath){
    if (!filepath) return ESP_ERR_INVALID_ARG;
    if (!s_capture_queue) return ESP_ERR_INVALID_STATE;
    if (xQueueSend(s_capture_queue, &filepath, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Capture an image to a file
 * 
 * @param filepath Path to save the captured image
 * @param frame_size Frame size to set for the capture
 * @param jpeg_quality JPEG quality to set for the capture
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t recorder_capture_to_file(const char *filepath, framesize_t frame_size, int jpeg_quality){
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        if (frame_size != (framesize_t)-1) {
            s->set_framesize(s, frame_size);
        }
        if (jpeg_quality >= 0) {
            s->set_quality(s, jpeg_quality);
        }
    }
    if (recorder_led_configured) {
        gpio_set_level(RECORDER_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    camera_fb_t *fb = NULL;
    for (int i = 0; i < 3; i++) {
        fb = esp_camera_fb_get();
        if (fb) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed after retries");
        if (recorder_led_configured) {
            gpio_set_level(RECORDER_LED_GPIO, 0);
        }
        return ESP_FAIL;
    }

    size_t img_len = fb->len;

    uint8_t *heap_buf = malloc(img_len);
    if (!heap_buf) {
        ESP_LOGE(TAG, "OOM allocating heap buffer for image copy");
        esp_camera_fb_return(fb);
        if (recorder_led_configured) gpio_set_level(RECORDER_LED_GPIO, 0);
        return ESP_FAIL;
    }
    memcpy(heap_buf, fb->buf, img_len);

    esp_camera_fb_return(fb);

    FILE *f = NULL;
    const int max_open_attempts = 3;
    for (int attempt = 0; attempt < max_open_attempts; ++attempt) {
        f = fopen(filepath, "wb");
        if (f) break;
        vTaskDelay(pdMS_TO_TICKS(100 * (attempt + 1)));
    }
    if (!f) {
        ESP_LOGE(TAG, "fopen failed");
        free(heap_buf);
        if (recorder_led_configured) gpio_set_level(RECORDER_LED_GPIO, 0);
        return ESP_FAIL;
    }

    size_t written = fwrite(heap_buf, 1, img_len, f);
    fflush(f);
    fclose(f);

    if (recorder_led_configured) {
        gpio_set_level(RECORDER_LED_GPIO, 0);
    }

    free(heap_buf);

    if (written != img_len) {
        ESP_LOGE(TAG, "Failed to write complete image");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Asynchronous task to capture an image to a file
 * 
 * @param param File path (heap-allocated string)
 */
void recorder_capture_to_file_async_task(void *param){
    char *path = (char *)param;
    if (!path) {
        vTaskDelete(NULL);
        return;
    }

    esp_err_t res = recorder_capture_to_file(path, FRAMESIZE_VGA, 30);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Async capture failed: %s", path);
    }

    free(path);
    vTaskDelete(NULL);
}

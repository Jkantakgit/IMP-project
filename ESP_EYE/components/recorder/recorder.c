
#include "recorder.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "recorder";

// Video recording state
static struct {
    FILE *file;
    bool recording;
    uint32_t duration_ms;
    int64_t start_time;
    TaskHandle_t record_task_handle;
} video_state = {
    .file = NULL,
    .recording = false,
    .duration_ms = 0,
    .start_time = 0,
    .record_task_handle = NULL
};

// Camera configurations (primary = best quality, medium = good, fallback = minimal)
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
    .jpeg_quality = 10,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST
};

static camera_config_t camera_config_medium = {
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
    .frame_size = FRAMESIZE_QVGA,     // 320x240 - good quality
    .jpeg_quality = 15,
    .fb_count = 1,
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
    .frame_size = FRAMESIZE_QQVGA,    // Minimal for guaranteed allocation
    .jpeg_quality = 30,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_DRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY
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
        
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Primary camera init failed (%s), trying medium", esp_err_to_name(err));

    err = esp_camera_init(&camera_config_medium);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Camera initialized with medium config (QVGA 320x240, quality 15)");
        
        // Apply basic sensor tuning
        sensor_t * s = esp_camera_sensor_get();
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_gain_ctrl(s, 1);
        s->set_lenc(s, 1);
        
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Medium camera init failed (%s), trying fallback", esp_err_to_name(err));

    err = esp_camera_init(&camera_config_fallback);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallback camera init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Camera initialized with fallback config (QQVGA 160x120, quality 30)");
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

static void video_record_task(void *pvParameters)
{
    const char *filepath = (const char *)pvParameters;
    
    ESP_LOGI(TAG, "Video recording task started");
    
    while (video_state.recording) {
        /* Check if duration has elapsed */
        if (video_state.duration_ms > 0) {
            int64_t elapsed = (esp_timer_get_time() - video_state.start_time) / 1000;
            if (elapsed >= video_state.duration_ms) {
                ESP_LOGI(TAG, "Recording duration reached, stopping");
                break;
            }
        }
        
        /* Capture frame */
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed during recording");
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        
        /* Write frame to file */
        if (video_state.file) {
            size_t written = fwrite(fb->buf, 1, fb->len, video_state.file);
            if (written != fb->len) {
                ESP_LOGE(TAG, "Failed to write frame");
            }
        }
        
        /* Return frame buffer */
        esp_camera_fb_return(fb);
        
        /* Small delay between frames (~10 FPS) */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    /* Close file */
    if (video_state.file) {
        fclose(video_state.file);
        video_state.file = NULL;
    }
    
    video_state.recording = false;
    video_state.record_task_handle = NULL;
    
    ESP_LOGI(TAG, "Video recording task finished");
    
    /* Free the filepath string */
    free((void *)filepath);
    
    vTaskDelete(NULL);
}

esp_err_t recorder_start_video(const char *filepath, uint32_t duration_ms)
{
    if (video_state.recording) {
        ESP_LOGE(TAG, "Video recording already in progress");
        return ESP_FAIL;
    }
    
    /* Open file for writing */
    video_state.file = fopen(filepath, "wb");
    if (!video_state.file) {
        ESP_LOGE(TAG, "Failed to open file for video: %s", filepath);
        return ESP_FAIL;
    }
    
    /* Set recording state */
    video_state.recording = true;
    video_state.duration_ms = duration_ms;
    video_state.start_time = esp_timer_get_time();
    
    /* Create a copy of filepath for the task */
    char *filepath_copy = strdup(filepath);
    if (!filepath_copy) {
        ESP_LOGE(TAG, "Failed to allocate memory for filepath");
        fclose(video_state.file);
        video_state.file = NULL;
        video_state.recording = false;
        return ESP_FAIL;
    }
    
    /* Create recording task */
    BaseType_t ret = xTaskCreate(
        video_record_task,
        "video_record",
        4096,
        filepath_copy,
        5,
        &video_state.record_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create video recording task");
        fclose(video_state.file);
        video_state.file = NULL;
        video_state.recording = false;
        free(filepath_copy);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Video recording started: %s (duration: %lu ms)", filepath, duration_ms);
    return ESP_OK;
}

esp_err_t recorder_stop_video(void)
{
    if (!video_state.recording) {
        ESP_LOGW(TAG, "No video recording in progress");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Stopping video recording");
    video_state.recording = false;
    
    /* Wait for task to finish (max 5 seconds) */
    int timeout = 50;
    while (video_state.record_task_handle != NULL && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout--;
    }
    
    if (timeout == 0) {
        ESP_LOGW(TAG, "Video recording task did not finish in time");
    }
    
    return ESP_OK;
}

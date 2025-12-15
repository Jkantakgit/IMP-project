
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

/* Forward declarations for file write helpers used in header writer */
static void write_u32(FILE *f, uint32_t val);
static void write_u16(FILE *f, uint16_t val);
static void write_fourcc(FILE *f, const char *fourcc);

// AVI/MJPEG structures
typedef struct {
    uint32_t fourcc;
    uint32_t size;
} __attribute__((packed)) avi_chunk_t;

typedef struct {
    uint32_t offset;
    uint32_t size;
} __attribute__((packed)) avi_index_entry_t;

// Camera operation lock - ensures only one operation (photo or video) at a time
static bool camera_busy = false;

// Video recording state
static struct {
    uint8_t *psram_buffer;      // PSRAM buffer for recording
    uint32_t buffer_pos;        // Current position in buffer
    uint32_t buffer_size;       // Total buffer size
    uint32_t header_size;       // Size of AVI header (to preserve during flush)
    FILE *file;
    bool recording;
    bool file_created;          // Track if file has been created
    uint32_t duration_ms;
    int64_t start_time;
    TaskHandle_t record_task_handle;
    uint32_t frame_count;
    uint32_t movi_size;
    uint32_t idx_count;
    avi_index_entry_t *index;
    uint32_t width;
    uint32_t height;
} video_state = {
    .psram_buffer = NULL,
    .buffer_pos = 0,
    .buffer_size = 3584*1024,  // 3.5MB PSRAM buffer (fits in 4MB mapped space, leaves room for camera FBs)
    .header_size = 0,
    .file = NULL,
    .recording = false,
    .file_created = false,
    .duration_ms = 0,
    .start_time = 0,
    .record_task_handle = NULL,
    .frame_count = 0,
    .movi_size = 0,
    .idx_count = 0,
    .index = NULL,
    .width = 0,
    .height = 0
};

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
    
    return ESP_OK;
}

bool is_recording(void)
{
    return video_state.recording;
}

bool is_camera_busy(void)
{
    return camera_busy;
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
    /* Check if camera is busy */
    if (camera_busy || video_state.recording) {
        ESP_LOGE(TAG, "Camera busy - cannot capture photo while recording or during another operation");
        return ESP_FAIL;
    }
    
    /* Mark camera as busy */
    camera_busy = true;
    
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
        camera_busy = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera captured %d bytes", fb->len);

    /* Open file for writing */
    FILE *f = fopen(filepath, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filepath);
        esp_camera_fb_return(fb);
        camera_busy = false;
        return ESP_FAIL;
    }

    /* Write JPEG data to file */
    size_t written = fwrite(fb->buf, 1, fb->len, f);
    fclose(f);

    /* Return frame buffer */
    esp_camera_fb_return(fb);
    
    /* Clear busy flag */
    camera_busy = false;

    if (written != fb->len) {
        ESP_LOGE(TAG, "Failed to write complete image (wrote %d of %d bytes)", written, fb->len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Image saved to %s (%d bytes)", filepath, written);
    return ESP_OK;
}

/* Helper function to write data to PSRAM buffer */
static bool write_to_buffer(const uint8_t *data, uint32_t len)
{
    if (video_state.buffer_pos + len > video_state.buffer_size) {
        ESP_LOGE(TAG, "PSRAM buffer full! Cannot write %d bytes (pos=%d, size=%d)", 
                 len, video_state.buffer_pos, video_state.buffer_size);
        return false;
    }
    memcpy(video_state.psram_buffer + video_state.buffer_pos, data, len);
    video_state.buffer_pos += len;
    return true;
}

static void static_write_u32(uint8_t *buf, uint32_t *pos, uint32_t val) {
    buf[(*pos)++] = (val) & 0xff;
    buf[(*pos)++] = (val >> 8) & 0xff;
    buf[(*pos)++] = (val >> 16) & 0xff;
    buf[(*pos)++] = (val >> 24) & 0xff;
}

static void static_write_u16(uint8_t *buf, uint32_t *pos, uint16_t val) {
    buf[(*pos)++] = (val) & 0xff;
    buf[(*pos)++] = (val >> 8) & 0xff;
}

static void static_write_fourcc(uint8_t *buf, uint32_t *pos, const char *fourcc) {
    memcpy(buf + *pos, fourcc, 4);
    *pos += 4;
}

static uint32_t static_write_avi_header_to_buffer(uint32_t width, uint32_t height, uint32_t fps) {
    uint32_t pos = 0;
    uint8_t *buf = video_state.psram_buffer;
    
    // RIFF header
    static_write_fourcc(buf, &pos, "RIFF");
    static_write_u32(buf, &pos, 0);  // placeholder for file size
    static_write_fourcc(buf, &pos, "AVI ");
    
    // hdrl LIST
    static_write_fourcc(buf, &pos, "LIST");
    static_write_u32(buf, &pos, 192);  // hdrl size
    static_write_fourcc(buf, &pos, "hdrl");
    
    // avih chunk
    static_write_fourcc(buf, &pos, "avih");
    static_write_u32(buf, &pos, 56);
    static_write_u32(buf, &pos, 1000000 / fps);  // microsec per frame
    static_write_u32(buf, &pos, 0);  // max bytes per sec
    static_write_u32(buf, &pos, 0);  // padding
    static_write_u32(buf, &pos, 0x10);  // flags
    static_write_u32(buf, &pos, 0);  // total frames (placeholder)
    static_write_u32(buf, &pos, 0);  // initial frames
    static_write_u32(buf, &pos, 1);  // streams
    static_write_u32(buf, &pos, 0);  // suggested buffer size
    static_write_u32(buf, &pos, width);
    static_write_u32(buf, &pos, height);
    static_write_u32(buf, &pos, 0);  // reserved
    static_write_u32(buf, &pos, 0);
    static_write_u32(buf, &pos, 0);
    static_write_u32(buf, &pos, 0);
    
    // strl LIST
    static_write_fourcc(buf, &pos, "LIST");
    static_write_u32(buf, &pos, 116);
    static_write_fourcc(buf, &pos, "strl");
    
    // strh chunk
    static_write_fourcc(buf, &pos, "strh");
    static_write_u32(buf, &pos, 56);
    static_write_fourcc(buf, &pos, "vids");
    static_write_fourcc(buf, &pos, "MJPG");
    static_write_u32(buf, &pos, 0);  // flags
    static_write_u16(buf, &pos, 0);  // priority
    static_write_u16(buf, &pos, 0);  // language
    static_write_u32(buf, &pos, 0);  // initial frames
    static_write_u32(buf, &pos, 1);  // scale
    static_write_u32(buf, &pos, fps);  // rate
    static_write_u32(buf, &pos, 0);  // start
    static_write_u32(buf, &pos, 0);  // length (placeholder)
    static_write_u32(buf, &pos, 0);  // suggested buffer size
    static_write_u32(buf, &pos, 0);  // quality
    static_write_u32(buf, &pos, 0);  // sample size
    static_write_u16(buf, &pos, 0);  // frame left
    static_write_u16(buf, &pos, 0);  // frame top
    static_write_u16(buf, &pos, width);  // frame right
    static_write_u16(buf, &pos, height);  // frame bottom
    
    // strf chunk
    static_write_fourcc(buf, &pos, "strf");
    static_write_u32(buf, &pos, 40);
    static_write_u32(buf, &pos, 40);  // size
    static_write_u32(buf, &pos, width);
    static_write_u32(buf, &pos, height);
    static_write_u16(buf, &pos, 1);  // planes
    static_write_u16(buf, &pos, 24);  // bit count
    static_write_fourcc(buf, &pos, "MJPG");
    static_write_u32(buf, &pos, width * height * 3);  // size image
    static_write_u32(buf, &pos, 0);  // x pixels per meter
    static_write_u32(buf, &pos, 0);  // y pixels per meter
    static_write_u32(buf, &pos, 0);  // clr used
    static_write_u32(buf, &pos, 0);  // clr important
    
    // movi LIST
    static_write_fourcc(buf, &pos, "LIST");
    static_write_u32(buf, &pos, 0);  // placeholder
    static_write_fourcc(buf, &pos, "movi");
    
    video_state.buffer_pos = pos;
    return pos;  // Return header size
}

static void write_u32(FILE *f, uint32_t val) {
    fwrite(&val, 4, 1, f);
}

static void write_u16(FILE *f, uint16_t val) {
    fwrite(&val, 2, 1, f);
}

static void write_fourcc(FILE *f, const char *fourcc) {
    fwrite(fourcc, 4, 1, f);
}


/* Write the initial AVI header to a file and leave position at start of movi data */
static esp_err_t write_avi_header_to_file(FILE *f, uint32_t width, uint32_t height, uint32_t fps) {
    // RIFF header
    write_fourcc(f, "RIFF");
    write_u32(f, 0); // placeholder for file size, updated in finalize
    write_fourcc(f, "AVI ");

    // hdrl LIST
    write_fourcc(f, "LIST");
    write_u32(f, 192); // hdrl size
    write_fourcc(f, "hdrl");

    // avih chunk
    write_fourcc(f, "avih");
    write_u32(f, 56); // avih size
    write_u32(f, 1000000 / fps); // microseconds per frame
    write_u32(f, 0); // max bytes per second
    write_u32(f, 0); // padding granularity
    write_u32(f, 0x10); // flags
    write_u32(f, 0); // total frames (placeholder)
    write_u32(f, 0); // initial frames
    write_u32(f, 1); // streams
    write_u32(f, 0); // suggested buffer size
    write_u32(f, width);
    write_u32(f, height);
    write_u32(f, 0); // reserved
    write_u32(f, 0); // reserved
    write_u32(f, 0); // reserved
    write_u32(f, 0); // reserved

    // strl LIST
    write_fourcc(f, "LIST");
    write_u32(f, 116); // strl size
    write_fourcc(f, "strl");

    // strh chunk
    write_fourcc(f, "strh");
    write_u32(f, 56); // strh size
    write_fourcc(f, "vids");
    write_fourcc(f, "MJPG");
    write_u32(f, 0); // flags
    write_u16(f, 0); // priority
    write_u16(f, 0); // language
    write_u32(f, 0); // initial frames
    write_u32(f, 1); // scale
    write_u32(f, fps); // rate
    write_u32(f, 0); // start
    write_u32(f, 0); // length (placeholder)
    write_u32(f, 0); // suggested buffer size
    write_u32(f, 0xFFFFFFFF); // quality
    write_u32(f, 0); // sample size
    write_u16(f, 0); // frame left
    write_u16(f, 0); // frame top
    write_u16(f, width); // frame right
    write_u16(f, height); // frame bottom

    // strf chunk
    write_fourcc(f, "strf");
    write_u32(f, 40); // strf size
    write_u32(f, 40); // BITMAPINFO size
    write_u32(f, width);
    write_u32(f, height);
    write_u16(f, 1); // planes
    write_u16(f, 24); // bit count
    write_fourcc(f, "MJPG");
    write_u32(f, width * height * 3); // image size
    write_u32(f, 0); // X pixels per meter
    write_u32(f, 0); // Y pixels per meter
    write_u32(f, 0); // colors used
    write_u32(f, 0); // colors important

    // movi LIST
    write_fourcc(f, "LIST");
    write_u32(f, 0); // placeholder for movi size, updated in finalize
    write_fourcc(f, "movi");

    return ESP_OK;
}


static esp_err_t finalize_avi(FILE *f, uint32_t frame_count, uint32_t movi_size, avi_index_entry_t *index, uint32_t idx_count) {
    // Write index
    write_fourcc(f, "idx1");
    write_u32(f, idx_count * 16);
    for (uint32_t i = 0; i < idx_count; i++) {
        write_fourcc(f, "00dc");
        write_u32(f, 0x10); // keyframe
        write_u32(f, index[i].offset);
        write_u32(f, index[i].size);
    }
    
    long file_end = ftell(f);
    
    // Update RIFF size
    fseek(f, 4, SEEK_SET);
    write_u32(f, file_end - 8);
    
    // Update total frames in avih
    fseek(f, 48, SEEK_SET);
    write_u32(f, frame_count);
    
    // Update stream length in strh
    fseek(f, 140, SEEK_SET);
    write_u32(f, frame_count);
    
    // Update movi size
    fseek(f, 220, SEEK_SET);
    write_u32(f, movi_size);
    
    fseek(f, file_end, SEEK_SET);
    return ESP_OK;
}

/* Periodic buffer flush function to write buffer contents to SD */
/* Direct-to-SD mode: no periodic flush needed */

static void video_record_task(void *pvParameters)
{
    const char *filepath = (const char *)pvParameters;
    
    ESP_LOGI(TAG, "Video recording task started");
    
    uint32_t frame_offset = 0;
    int64_t last_frame_time = esp_timer_get_time();
    
    while (video_state.recording) {
        /* Check if duration has elapsed */
        if (video_state.duration_ms > 0) {
            int64_t elapsed = (esp_timer_get_time() - video_state.start_time) / 1000;
            if (elapsed >= video_state.duration_ms) {
                ESP_LOGI(TAG, "Recording duration reached, stopping");
                break;
            }
        }
        
        int64_t loop_start = esp_timer_get_time();
        
        /* Capture frame */
        int64_t capture_start = esp_timer_get_time();
        camera_fb_t *fb = esp_camera_fb_get();
        int64_t capture_time = (esp_timer_get_time() - capture_start) / 1000;
        
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed during recording (after %lld ms)", capture_time);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        
        // Reduced per-frame logging to avoid overhead

        /* Write MJPEG frame chunk directly to SD file */
        int64_t write_start = esp_timer_get_time();

        // Write chunk header
        write_fourcc(video_state.file, "00dc");
        write_u32(video_state.file, fb->len);

        // Write JPEG data
        size_t written = fwrite(fb->buf, 1, fb->len, video_state.file);
        if (written != fb->len) {
            ESP_LOGE(TAG, "Failed to write frame data to file (%u/%u)", written, fb->len);
            esp_camera_fb_return(fb);
            break;
        }

        // Pad to even boundary
        if (fb->len & 1) {
            uint8_t pad = 0;
            fwrite(&pad, 1, 1, video_state.file);
        }

        // int64_t write_time = (esp_timer_get_time() - write_start) / 1000;
        
        // Store index entry
        if (video_state.idx_count < 1800) { // max 1 minute at 30fps
            video_state.index[video_state.idx_count].offset = frame_offset;
            video_state.index[video_state.idx_count].size = fb->len;
            video_state.idx_count++;
        }
        
        uint32_t chunk_size = 8 + fb->len + (fb->len & 1);
        video_state.movi_size += chunk_size;
        frame_offset += chunk_size;
        video_state.frame_count++;
        
        /* Return frame buffer */
        esp_camera_fb_return(fb);
        
        int64_t loop_time = (esp_timer_get_time() - loop_start) / 1000;
        int64_t interval = (esp_timer_get_time() - last_frame_time) / 1000;
        last_frame_time = esp_timer_get_time();
        
        // ESP_LOGD(TAG, "Frame %d complete: loop=%lld ms, interval=%lld ms", video_state.frame_count - 1, loop_time, interval);
        
        /* Small delay between frames (~10 FPS) */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    /* Finalize AVI headers in the open file */
    if (video_state.file) {
        finalize_avi(video_state.file, video_state.frame_count, video_state.movi_size, 
                     video_state.index, video_state.idx_count);
        fclose(video_state.file);
        video_state.file = NULL;
    }
    
    /* Free buffers */
    if (video_state.index) {
        free(video_state.index);
        video_state.index = NULL;
    }
    
    video_state.recording = false;
    camera_busy = false; // release busy flag when task ends (duration-based stop)
    video_state.record_task_handle = NULL;
    
    ESP_LOGI(TAG, "Video recording finished: %d frames", video_state.frame_count);
    
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
    
    if (camera_busy) {
        ESP_LOGE(TAG, "Camera busy - cannot start recording while capturing photo");
        return ESP_FAIL;
    }
    
    /* Mark camera as busy */
    camera_busy = true;
    /* Get camera sensor info for dimensions */
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_FAIL;
    }
    
    camera_status_t status = s->status;
    video_state.width = status.framesize <= FRAMESIZE_QVGA ? 320 : 
                        status.framesize == FRAMESIZE_VGA ? 640 : 1024;
    video_state.height = status.framesize <= FRAMESIZE_QVGA ? 240 :
                         status.framesize == FRAMESIZE_VGA ? 480 : 768;
    
    /* Allocate index buffer (max 1800 frames = 3 minutes at 10fps) */
    video_state.index = (avi_index_entry_t *)malloc(1800 * sizeof(avi_index_entry_t));
    if (!video_state.index) {
        ESP_LOGE(TAG, "Failed to allocate index buffer");
        camera_busy = false;
        return ESP_FAIL;
    }

    /* Open file and write AVI header immediately (direct-to-SD mode) */
    video_state.file = fopen(filepath, "wb");
    if (!video_state.file) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filepath);
        free(video_state.index);
        video_state.index = NULL;
        camera_busy = false;
        return ESP_FAIL;
    }
    setvbuf(video_state.file, NULL, _IOFBF, 32768); // 32KB file buffer
    if (write_avi_header_to_file(video_state.file, video_state.width, video_state.height, 10) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write AVI header to file");
        fclose(video_state.file);
        video_state.file = NULL;
        free(video_state.index);
        video_state.index = NULL;
        camera_busy = false;
        return ESP_FAIL;
    }
    
    /* Set recording state */
    video_state.recording = true;
    video_state.duration_ms = duration_ms;
    video_state.start_time = esp_timer_get_time();
    video_state.frame_count = 0;
    video_state.movi_size = 0;
    video_state.idx_count = 0;
    
    /* Store filepath for later use in recording task */
    char *filepath_copy = strdup(filepath);
    if (!filepath_copy) {
        ESP_LOGE(TAG, "Failed to allocate memory for filepath");
        free(video_state.index);
        video_state.index = NULL;
        video_state.recording = false;
        fclose(video_state.file);
        video_state.file = NULL;
        camera_busy = false;
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
        free(video_state.index);
        video_state.index = NULL;
        video_state.recording = false;
        fclose(video_state.file);
        video_state.file = NULL;
        camera_busy = false;
        free(filepath_copy);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "AVI recording started: %s (%dx%d at 10fps, duration: %lu ms)", 
             filepath, video_state.width, video_state.height, duration_ms);
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
    
    /* Clear busy flag */
    camera_busy = false;
    
    return ESP_OK;
}

#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Check if video is currently recording
bool is_recording(void);

// Check if camera is busy (photo capture or video recording in progress)
bool is_camera_busy(void);

// Initialize the camera. Returns ESP_OK on success.
esp_err_t recorder_init(void);

// Deinitialize camera
esp_err_t recorder_deinit(void);

// Capture image and save to file (camera must be initialized first)
esp_err_t recorder_capture_to_file(const char *filepath);

// Start video recording (duration_ms = 0 means indefinite, must call recorder_stop_video to stop)
esp_err_t recorder_start_video(const char *filepath, uint32_t duration_ms);

// Stop video recording
esp_err_t recorder_stop_video(void);
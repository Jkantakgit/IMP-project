#pragma once

#include "esp_err.h"
#include "esp_camera.h"
#include <stdbool.h>


// Initialize the camera. Returns ESP_OK on success.
esp_err_t recorder_init(void);

// Deinitialize camera
esp_err_t recorder_deinit(void);

// Capture image and save to file (camera must be initialized first)
// Pass `frame_size` as a `framesize_t` or -1 to leave unchanged.
// Pass `jpeg_quality` >= 0 to change quality (higher number = lower quality),
// or -1 to leave unchanged.
esp_err_t recorder_capture_to_file(const char *filepath, framesize_t frame_size, int jpeg_quality);

// Request grayscale captures. Note: PIXFORMAT_GRAYSCALE produces raw frames
// which are not JPEG-compressed by the camera; full support would require
// encoding raw frames to JPEG. This function returns ESP_ERR_NOT_SUPPORTED
// on this implementation to avoid producing unusable files.
esp_err_t recorder_set_grayscale(bool enable);




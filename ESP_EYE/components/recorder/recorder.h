#pragma once

#include "esp_err.h"


// Initialize the camera. Returns ESP_OK on success.
esp_err_t recorder_init(void);

// Deinitialize camera
esp_err_t recorder_deinit(void);

// Capture image and save to file (camera must be initialized first)
esp_err_t recorder_capture_to_file(const char *filepath);
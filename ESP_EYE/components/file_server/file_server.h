#pragma once

#include "esp_err.h"


/**
 * @brief Start the file server
 *
 * @param static_base_path Base path for static frontend files (e.g., "/spiffs")
 * @param videos_base_path Base path for recorded videos (e.g., "/data")
 * @return esp_err_t ESP_OK on success
 */
esp_err_t example_start_file_server(const char *static_base_path, const char *videos_base_path);

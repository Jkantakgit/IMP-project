#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the file server
 *
 * @param base_path Base path for file serving (e.g., "/data")
 * @return esp_err_t ESP_OK on success
 */
esp_err_t example_start_file_server(const char *base_path);

#ifdef __cplusplus
}
#endif

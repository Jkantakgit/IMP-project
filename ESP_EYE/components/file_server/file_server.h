#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "recorder.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"


#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <dirent.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/param.h>

/**
 * @brief Start the file server
 *
 * @param static_base_path Base path for static frontend files (e.g., "/spiffs")
 * @param videos_base_path Base path for recorded videos (e.g., "/data")
 * @return esp_err_t ESP_OK on success
 */
esp_err_t example_start_file_server(const char *static_base_path, const char *videos_base_path);

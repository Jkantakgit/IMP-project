#pragma once

#include "esp_err.h"
#include "esp_camera.h"
#include <stdbool.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include "ff.h"


// Initialize the camera. Returns ESP_OK on success.
esp_err_t recorder_init(void);

// Deinitialize camera
esp_err_t recorder_deinit(void);

esp_err_t recorder_capture_to_file(const char *filepath, framesize_t frame_size, int jpeg_quality);

esp_err_t recorder_enqueue_capture(char *filepath);




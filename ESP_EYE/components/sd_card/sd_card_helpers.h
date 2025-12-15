#pragma once


#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"


// Mount the SD card to the given base path (e.g. "/sdcard").
// Returns ESP_OK on success.
esp_err_t sd_card_mount(const char *base_path);

// Unmount the SD card previously mounted at base_path.
esp_err_t sd_card_unmount(const char *base_path);

// List directory entries under `path`. The callback will be called for each
// filename found. Returns ESP_OK if directory was opened, otherwise an error.
esp_err_t sd_card_list_dir(const char *path, void (*entry_cb)(const char *name, void *user), void *user);

#pragma once


#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>


#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "driver/spi_common.h"

esp_err_t sd_card_mount(const char *base_path);


esp_err_t sd_card_list_dir(const char *path, void (*entry_cb)(const char *name, void *user), void *user);
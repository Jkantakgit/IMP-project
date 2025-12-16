#pragma once

#include "esp_err.h"

/* Mount SD card via SDSPI (SPI-based SD). Provide MOSI/MISO/SCLK/CS GPIO numbers. */
esp_err_t sd_card_mount_sdspi(const char *base_path, int mosi_gpio, int miso_gpio, int sclk_gpio, int cs_gpio);

/* Unmount the SD card previously mounted at base_path. */
esp_err_t sd_card_unmount(const char *base_path);

// Mount SDMMC in 1-bit mode (if supported by the SDK/host).
// This mirrors `SD_MMC.begin()` with 1-bit operation.
esp_err_t sd_card_mount_1bit(const char *base_path);

// Probe and attempt multiple SD mount methods (deinit recorder, try multiple
// SDMMC pin maps in full/1-bit modes and finally SDSPI). Returns first
// successful `esp_err_t` or last error.
esp_err_t sd_card_mount_probe(const char *base_path);
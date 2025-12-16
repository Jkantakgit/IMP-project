/* SD card helper component - SDSPI mounting */

#include "sd_card_helpers.h"
#include "esp_vfs_fat.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "sd_card";
static sdmmc_card_t *s_card = NULL;

esp_err_t sd_card_mount_sdspi(const char *base_path, int mosi_gpio, int miso_gpio, int sclk_gpio, int cs_gpio)
{
    if (!base_path) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Mounting SD card via SDSPI at '%s'", base_path);

    spi_bus_config_t buscfg = {
        .mosi_io_num = mosi_gpio,
        .miso_io_num = miso_gpio,
        .sclk_io_num = sclk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4 * 1024 * 1024  /* 4MB for faster transfers */
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SPI_MASTER_FREQ_40M;  /* 40MHz SPI clock */
    
    esp_err_t err = spi_bus_initialize(host.slot, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = cs_gpio;
    slot_config.host_id = host.slot;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 4 * 1024  /* 4KB clusters for faster I/O */
    };

    err = esp_vfs_fat_sdspi_mount(base_path, &host, &slot_config, &mount_config, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SDSPI mount failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "SD card mounted successfully");
    return ESP_OK;
}

esp_err_t sd_card_unmount(const char *base_path)
{
    if (!base_path) return ESP_ERR_INVALID_ARG;
    if (!s_card) return ESP_ERR_INVALID_STATE;

    esp_err_t err = esp_vfs_fat_sdcard_unmount(base_path, s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount SD card: %s", esp_err_to_name(err));
        return err;
    }
    s_card = NULL;
    ESP_LOGI(TAG, "SD card unmounted");
    return ESP_OK;
}

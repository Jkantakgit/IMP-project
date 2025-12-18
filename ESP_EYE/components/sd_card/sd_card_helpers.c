/* SD card helper component - SDSPI mounting */

#include "sd_card_helpers.h"


static const char *TAG = "sd_card";

esp_err_t sd_card_mount(const char *base_path)
{
    if (!base_path) return ESP_ERR_INVALID_ARG;

    /* Use SPI mode to avoid pin conflicts with camera on ESP32-CAM */
    ESP_LOGI(TAG, "Attempting SDSPI mount at '%s'", base_path);
    
    /* Initialize SPI bus first */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = GPIO_NUM_15,
        .miso_io_num = GPIO_NUM_2,
        .sclk_io_num = GPIO_NUM_14,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(err));
        return err;
    }

    sdmmc_card_t *card = NULL;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = GPIO_NUM_13;
    slot_config.host_id = SPI2_HOST;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    err = esp_vfs_fat_sdspi_mount(base_path, &host, &slot_config, &mount_config, &card);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SDSPI mounted successfully (card=%p)", card);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "SDSPI mount failed (%s)", esp_err_to_name(err));
    ESP_LOGE(TAG, "SD mount failures: check wiring, power, card format");
    return err;
}


esp_err_t sd_card_list_dir(const char *path, void (*entry_cb)(const char *name, void *user), void *user)
{
    if (!path || !entry_cb) return ESP_ERR_INVALID_ARG;

    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open dir: %s", path);
        return ESP_FAIL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        entry_cb(entry->d_name, user);
    }
    closedir(dir);
    return ESP_OK;
}
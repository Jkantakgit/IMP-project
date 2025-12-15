/* SD card helper component
 * Provides mounting/unmounting and simple directory listing helpers.
 *
 * This uses the ESP-IDF FAT VFS helpers and supports SDMMC (native) or
 * SDSPI depending on SOC support and available drivers.
 */



#include "sd_card_helpers.h"
#include "driver/sdmmc_host.h"




static const char *TAG = "sd_card";

esp_err_t sd_card_mount(const char *base_path)
{
    if (!base_path) return ESP_ERR_INVALID_ARG;

    /* Prefer SDMMC 4-bit mode for throughput on ESP32-CAM (GPIOs: CLK=14, CMD=15, D0=2, D1=4, D2=12, D3=13). */
    ESP_LOGI(TAG, "Attempting SDMMC 4-bit mount at '%s'", base_path);
    
    sdmmc_card_t *card = NULL;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // Use default frequency for more robust init; can negotiate HS later
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    
    /* SDMMC slot config for 4-bit mode with internal pull-ups to ensure reliable init. */
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;  // 4-bit mode for max speed
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    
    /* GPIO configuration:
     * CMD:  GPIO15
     * CLK:  GPIO14
     * D0:   GPIO2
     * D1:   GPIO4
     * D2:   GPIO12
     * D3:   GPIO13
     */

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 64 * 1024  // 64KB clusters for large sequential writes
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(base_path, &host, &slot_config, &mount_config, &card);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SDMMC mounted successfully (4-bit mode)");
        sdmmc_card_print_info(stdout, card);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "SDMMC 4-bit mount failed (%s). Retrying 1-bit as fallback...", esp_err_to_name(err));

    /* Retry in 1-bit mode with pull-ups on CMD/D0 for compatibility */
    slot_config.width = 1;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    err = esp_vfs_fat_sdmmc_mount(base_path, &host, &slot_config, &mount_config, &card);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SDMMC mounted successfully (1-bit fallback)");
        sdmmc_card_print_info(stdout, card);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "SDMMC mount failed in both 4-bit and 1-bit (%s)", esp_err_to_name(err));
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

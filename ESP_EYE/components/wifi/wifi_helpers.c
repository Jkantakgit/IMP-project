#include "wifi_helpers.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <string.h>

static const char *TAG = "wifi_helpers";

static bool s_wifi_initialized = false;

esp_err_t wifi_helpers_init(void)
{
    if (s_wifi_initialized) return ESP_OK;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_wifi_initialized = true;
    return ESP_OK;
}

static esp_err_t wifi_helpers_ensure_init_and_stop(void)
{
    if (!s_wifi_initialized) {
        esp_err_t err = wifi_helpers_init();
        if (err != ESP_OK) return err;
    }
    /* Stop wifi if running to change mode safely */
    esp_wifi_stop();
    return ESP_OK;
}

esp_err_t wifi_helpers_init_ap(const char *ssid, const char *password)
{
    if (!ssid) return ESP_ERR_INVALID_ARG;

    esp_err_t err = wifi_helpers_ensure_init_and_stop();
    if (err != ESP_OK) return err;

    esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(ssid);
    if (password) {
        strncpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);
    }
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.channel = 1;

    if (password && strlen(password) >= 8) {
        wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) return err;
    /* Throughput tuning: disable power save, enable 11n, set bandwidth/tx power */
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
    /* Max TX power (~20-21 dBm); units are 0.25 dBm. 78=19.5 dBm, 84=21 dBm (clamped). */
    esp_wifi_set_max_tx_power(78);
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "AP started. SSID:%s password:%s", ssid, password ? password : "(open)");
    return ESP_OK;
}

esp_err_t wifi_helpers_init_sta(const char *ssid, const char *password)
{
    if (!ssid) return ESP_ERR_INVALID_ARG;

    esp_err_t err = wifi_helpers_ensure_init_and_stop();
    if (err != ESP_OK) return err;

    esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) return err;
    /* Throughput tuning: disable power save, enable 11n, prefer HT40 bandwidth */
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
    esp_wifi_set_max_tx_power(78);
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    /* Attempt to connect */
    if (password) {
        esp_wifi_connect();
    } else {
        /* open network may still require connect() */
        esp_wifi_connect();
    }

    ESP_LOGI(TAG, "STA started. Connecting to SSID:%s", ssid);
    return ESP_OK;
}

esp_err_t wifi_helpers_deinit(void)
{
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(err));
    }
    err = esp_wifi_deinit();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_deinit failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}



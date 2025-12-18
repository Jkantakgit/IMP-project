#include "wifi_helpers.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "freertos/task.h"
#include <string.h>
#include "lwip/inet.h"

static const char *TAG = "wifi_helpers";

static bool s_wifi_initialized = false;
static char s_ap_ssid[33] = {0};
static char s_ap_pass[65] = {0};

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t* evt = (wifi_event_ap_staconnected_t*)event_data;
            ESP_LOGI(TAG, "Station connected: mac=%02x:%02x:%02x:%02x:%02x:%02x, aid=%d",
                     evt->mac[0], evt->mac[1], evt->mac[2], evt->mac[3], evt->mac[4], evt->mac[5], evt->aid);
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t* evt = (wifi_event_ap_stadisconnected_t*)event_data;
            ESP_LOGI(TAG, "Station disconnected: mac=%02x:%02x:%02x:%02x:%02x:%02x, aid=%d",
                     evt->mac[0], evt->mac[1], evt->mac[2], evt->mac[3], evt->mac[4], evt->mac[5], evt->aid);
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_ASSIGNED_IP_TO_CLIENT) {
            /* Newer ESP-IDF uses IP_EVENT_ASSIGNED_IP_TO_CLIENT; avoid depending on
               deprecated event struct fields here to remain compatible. */
            ESP_LOGI(TAG, "DHCP assigned IP to station (event_id=%d)", event_id);
        }
    }
}

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

    /* register event handler for logging and DHCP events */
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ASSIGNED_IP_TO_CLIENT, &wifi_event_handler, NULL, &instance_any_id);

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(ssid);
    strlcpy(s_ap_ssid, ssid, sizeof(s_ap_ssid));
    if (password) {
        strncpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);
        strlcpy(s_ap_pass, password, sizeof(s_ap_pass));
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

esp_err_t wifi_helpers_restart_ap(void)
{
    ESP_LOGI(TAG, "Restarting AP to refresh DHCP state");
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(err));
    }
    /* small delay to ensure stop completes */
    vTaskDelay(pdMS_TO_TICKS(200));
    /* Recreate default AP netif if needed */
    esp_netif_create_default_wifi_ap();
    /* Re-init mode/config using stored SSID/password */
    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.ap.ssid, s_ap_ssid, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(s_ap_ssid);
    if (s_ap_pass[0]) {
        strlcpy((char *)wifi_config.ap.password, s_ap_pass, sizeof(wifi_config.ap.password));
    }
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();
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



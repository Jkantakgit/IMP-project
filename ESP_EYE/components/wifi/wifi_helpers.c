#include "wifi_helpers.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "lwip/inet.h"


static const char *TAG = "wifi_helpers";

static bool s_wifi_initialized = false;
static char s_ap_ssid[33] = {0};
static char s_ap_pass[65] = {0};


/**
 * @brief Initialize Wi-Fi driver. Call before other APIs.
 * 
 * @return esp_err_t ESP_OK on success, ESP_ERR_... on failure
 */
esp_err_t wifi_helpers_init(void){
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

/**
 * @brief Ensure Wi-Fi is initialized and stopped
 * 
 * @return esp_err_t ESP_OK on success, ESP_ERR_... on failure
 */
static esp_err_t wifi_helpers_ensure_init_and_stop(void){
    if (!s_wifi_initialized) {
        esp_err_t err = wifi_helpers_init();
        if (err != ESP_OK) return err;
    }
    esp_wifi_stop();
    return ESP_OK;
}

/**
 * @brief Initialize Wi-Fi in AP mode. SSID and password are copied.
 * 
 * @param ssid SSID
 * @param password Password
 * @return esp_err_t ESP_OK on success, ESP_ERR_... on failure
 */
esp_err_t wifi_helpers_init_ap(const char *ssid, const char *password){
    if (!ssid) return ESP_ERR_INVALID_ARG;

    esp_err_t err = wifi_helpers_ensure_init_and_stop();
    if (err != ESP_OK) return err;

    esp_netif_create_default_wifi_ap();

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
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
    esp_wifi_set_max_tx_power(78);
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "AP started");
    return ESP_OK;
}

/**
 * @brief Restart AP (stop/start) to refresh DHCP server and network state
 * 
 * @return esp_err_t ESP_OK on success, ESP_ERR_... on failure
 */
esp_err_t wifi_helpers_restart_ap(void){
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_stop failed");
    }
    vTaskDelay(pdMS_TO_TICKS(200));
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

/**
 * @brief Initialize Wi-Fi in STA mode and attempt to connect to SSID/password.
 * 
 * @param ssid SSID
 * @param password Password
 * @return esp_err_t ESP_OK on success, ESP_ERR_... on failure
 */
esp_err_t wifi_helpers_init_sta(const char *ssid, const char *password){
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
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
    esp_wifi_set_max_tx_power(78);
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    esp_wifi_connect();

    ESP_LOGI(TAG, "STA connecting");
    return ESP_OK;
}

/**
 * @brief Deinitialize Wi-Fi
 * 
 * @return esp_err_t ESP_OK on success, ESP_ERR_... on failure
 */
esp_err_t wifi_helpers_deinit(void){
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_stop failed");
    }
    err = esp_wifi_deinit();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_deinit failed");
    }
    return ESP_OK;
}


#pragma once

#include "esp_err.h"


// Initialize Wi-Fi driver. Call before other APIs.
esp_err_t wifi_helpers_init(void);

// Initialize Wi-Fi in AP mode. SSID and password are copied.
esp_err_t wifi_helpers_init_ap(const char *ssid, const char *password);

// Initialize Wi-Fi in STA mode and attempt to connect to SSID/password.
esp_err_t wifi_helpers_init_sta(const char *ssid, const char *password);

// Stop and deinitialize Wi-Fi
esp_err_t wifi_helpers_deinit(void);

// Restart AP (stop/start) to refresh DHCP server and network state
esp_err_t wifi_helpers_restart_ap(void);

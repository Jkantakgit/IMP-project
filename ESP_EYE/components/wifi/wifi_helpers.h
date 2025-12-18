#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize Wi-Fi driver (idempotent). Call before other APIs.
esp_err_t wifi_helpers_init(void);

// Initialize Wi-Fi in AP mode. SSID and password are copied.
// If password is NULL or shorter than 8 chars, AP will be open.
esp_err_t wifi_helpers_init_ap(const char *ssid, const char *password);

// Initialize Wi-Fi in STA (client) mode and attempt to connect to SSID/password.
esp_err_t wifi_helpers_init_sta(const char *ssid, const char *password);

// Stop and deinitialize Wi-Fi
esp_err_t wifi_helpers_deinit(void);

// Restart AP (stop/start) to refresh DHCP server and network state
esp_err_t wifi_helpers_restart_ap(void);

#ifdef __cplusplus
}
#endif

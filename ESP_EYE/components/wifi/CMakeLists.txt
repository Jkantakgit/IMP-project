idf_component_register(SRCS "wifi_helpers.c" "wifi_helpers.h"
                       INCLUDE_DIRS "./"
                       PRIV_REQUIRES esp_wifi esp_event esp_netif)

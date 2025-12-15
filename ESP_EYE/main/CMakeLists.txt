idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_event esp_netif recorder wifi sd_card fatfs spiffs nvs_flash sdmmc vfs esp_psram
                    PRIV_REQUIRES esp_event esp_netif recorder wifi file_server sd_card sdmmc fatfs spiffs nvs_flash vfs esp_psram)

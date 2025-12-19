idf_component_register(SRCS "recorder.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio
                       REQUIRES esp_timer esp32-camera fatfs)


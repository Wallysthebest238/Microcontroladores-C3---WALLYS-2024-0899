idf_component_register(SRCS "app_main.c"
                    PRIV_REQUIRES mqtt esp_wifi nvs_flash esp_driver_gpio
                    INCLUDE_DIRS ".")

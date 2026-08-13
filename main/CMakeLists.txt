idf_component_register(SRCS "main.c" "wifi.c" "appointments.c" "time_utils.c" INCLUDE_DIRS "." REQUIRES esp_driver_uart esp_driver_gpio esp_wifi esp_event esp_netif nvs_flash)

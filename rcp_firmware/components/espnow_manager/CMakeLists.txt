# CMakeLists.txt for ESPNOW Manager component
# This component provides espnow manager utilities.
idf_component_register(SRCS "espnow_manager.c" INCLUDE_DIRS "." PRIV_REQUIRES esp_netif esp_wifi esp_event nvs_flash common uart_protocol driver) 
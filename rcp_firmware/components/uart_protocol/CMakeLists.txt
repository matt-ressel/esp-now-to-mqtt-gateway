# CMakeLists.txt for UART Protocol component
# This component provides functionality UART protocol handling.
idf_component_register(SRCS "uart_protocol.c" INCLUDE_DIRS "." PRIV_REQUIRES driver esp_adc)
# CMakeLists.txt for UART Protocol component
# This component provides functionality UART protocol handling.
idf_component_register(SRCS "uart_protocol.c" "uart_framer.c" INCLUDE_DIRS "include" PRIV_REQUIRES esp_common driver common)
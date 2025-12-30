# CMakeLists.txt for Power Monitor component
# This component provides functionality to monitor power and battery status.
idf_component_register(SRCS "power_monitor.c" INCLUDE_DIRS "include" PRIV_REQUIRES esp_adc)
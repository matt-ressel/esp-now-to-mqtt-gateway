#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <stdint.h>
#include "esp_err.h"

// Initialize power monitor
esp_err_t power_monitor_init(void);

// Get battery voltage in millivolts
esp_err_t power_monitor_get_battery_voltage(uint16_t *voltage_mv);

// Check if AP is connected to power (live)
bool power_monitor_is_ap_connected(void);

#endif // POWER_MONITOR_H

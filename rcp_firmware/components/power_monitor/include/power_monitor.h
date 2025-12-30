/**
 * @file    power_monitor.h
 * @author  Mateusz Ressel (https://github.com/matt-ressel)
 * @brief   Public API for the Power Monitor component.
 *
 * @details This component is responsible for monitoring the power status of the device.
 *          It utilizes the onboard ADC (Analog-to-Digital Converter) to:
 *          1. Measure the internal battery voltage via a voltage divider.
 *          2. Detect if the Host (S3) is powered and providing voltage.
 *
 *          It abstracts the underlying ADC calibration and hardware specifics.
 *
 * @version 0.2
 * @date    2025-12-30
 *
 * @copyright Copyright (c) 2025 Mateusz Ressel. Licensed under the MIT License.
 */

#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <stdbool.h>  // for bool type
#include <stdint.h>   // Standard integer types

#include "esp_err.h"  // For esp_err_t error codes (e.g., ESP_OK)

/**
 * @brief Initializes the Power Monitor component.
 *
 * Sets up the ADC unit, configures attenuation, and initializes the calibration scheme
 * (Curve Fitting) to ensure accurate millivolt readings.
 *
 * @return esp_err_t ESP_OK on success, or specific ADC error codes.
 */
esp_err_t power_monitor_init(void);

/**
 * @brief Performs a single measurement of the battery voltage.
 *
 * Takes a sample from the battery ADC channel, converts it to voltage using
 * calibration data, and applies the voltage divider multiplier.
 *
 * @param[out] voltage_mv Pointer to store the result (in millivolts).
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t power_monitor_get_battery_voltage(uint16_t* voltage_mv);

/**
 * @brief Checks if the Host (S3) is currently powered and reachable.
 *
 * Measures the voltage on the Host detection pin. If the voltage exceeds
 * a defined threshold (indicating logic HIGH/Power Good), returns true.
 *
 * @return true If Host is powered and reachable.
 * @return false If Host is OFF or disconnected.
 */
bool power_monitor_is_host_connected(void);

#endif  // POWER_MONITOR_H

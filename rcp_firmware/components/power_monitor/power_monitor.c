/**
 * @file    power_monitor.c
 * @author  Mateusz Ressel (https://github.com/matt-ressel)
 * @brief   Implementation of ADC-based power monitoring for ESP32-C6.
 *
 * @details Uses the 'esp_adc/adc_oneshot' driver introduced in ESP-IDF v5.0.
 *          Includes Curve Fitting calibration for higher accuracy.
 *
 * @version 0.2
 * @date    2025-12-30
 *
 * @copyright Copyright (c) 2025 Mateusz Ressel. Licensed under the MIT License.
 */

#include "power_monitor.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

// #define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

// Logging tag for this module
static const char* POWER_MONITOR = "POWER_MONITOR";

// ESP32-C6 ADC1 Channels:
#define ADC_UNIT ADC_UNIT_1
#define ADC_CHAN_HOST_DET ADC_CHANNEL_1  // GPIO 1 (Host Detection)
#define ADC_CHAN_BATT ADC_CHANNEL_2    // GPIO 2 (Battery Measure)

// Attenuation 12dB allows measuring up to ~3.1V (depending on calibration)
// Since we use dividers, max voltage at pin should be < 3.0V
#define ADC_ATTEN ADC_ATTEN_DB_12
#define ADC_BITWIDTH ADC_BITWIDTH_DEFAULT

/**
 * @brief    Voltage divider ratio for the Battery measurement.
 * @details  Ratio = (R_HIGH + R_LOW) / R_LOW.
 */
#define BATT_DIVIDER_RATIO 2.0f

/**
 * @brief Voltage divider ratio for Host detection.
 */
#define HOST_DIVIDER_RATIO 2.0f

/**
 * @brief Voltage threshold (mV) to consider Host (S3) as "connected".
 */
#define HOST_DETECT_THRESH_MV 1500  //

// Module Context
static adc_oneshot_unit_handle_t adc_handle = NULL;  // ADC One-Shot handle
static adc_cali_handle_t cali_handle = NULL;         // ADC Calibration handle
static bool do_calibration = false;                  // Flag indicating if calibration is active

/**
 * @brief Initializes the calibration scheme (Curve Fitting) for ESP32-C6.
 */
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t* out_handle) {
  adc_cali_handle_t handle = NULL;
  esp_err_t ret = ESP_FAIL;
  bool calibrated = false;

  ESP_LOGI(POWER_MONITOR, "Calibration Init: Unit %d, Atten %d", unit, atten);

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  if (!calibrated) {
    ESP_LOGI(POWER_MONITOR, "calibration scheme version is %s", "Curve Fitting");
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
      calibrated = true;
    }
  }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  if (!calibrated) {
    ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
      calibrated = true;
    }
  }
#endif

  *out_handle = handle;
  if (ret == ESP_OK) {
    ESP_LOGI(POWER_MONITOR, "Calibration Success");
  } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
    ESP_LOGW(POWER_MONITOR, "eFuse not burnt, skip software calibration");
  } else {
    ESP_LOGE(POWER_MONITOR, "Invalid arg or no memory");
  }
  return calibrated;
}

// Initialize the Power Monitor component
esp_err_t power_monitor_init(void) {
  // esp_log_level_set(TAG, ESP_LOG_DEBUG);  // Set log level for this module to DEBUG

  esp_err_t ret;

  // 1. Initialize ADC One-Shot Driver
  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = ADC_UNIT,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };

  ret = adc_oneshot_new_unit(&init_config, &adc_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(POWER_MONITOR, "ADC Unit Init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  // 2. Configure Channels
  adc_oneshot_chan_cfg_t config = {
      .bitwidth = ADC_BITWIDTH,
      .atten = ADC_ATTEN,
  };

  // Config Host Detection Channel
  ret = adc_oneshot_config_channel(adc_handle, ADC_CHAN_HOST_DET, &config);
  if (ret != ESP_OK) return ret;

  // Config Battery Channel
  ret = adc_oneshot_config_channel(adc_handle, ADC_CHAN_BATT, &config);
  if (ret != ESP_OK) return ret;

  // 3. Initialize Calibration
  // We init calibration for the Battery channel primarily,
  // but the scheme usually works for the whole unit/attenuation pair.
  do_calibration = adc_calibration_init(ADC_UNIT, ADC_CHAN_BATT, ADC_ATTEN, &cali_handle);

  ESP_LOGI(POWER_MONITOR, "Power Monitor Initialized. Calibration: %s", do_calibration ? "YES" : "NO");
  return ESP_OK;
}

// Get the battery voltage in millivolts
esp_err_t power_monitor_get_battery_voltage(uint16_t* voltage_mv) {
  if (adc_handle == NULL) return ESP_ERR_INVALID_STATE;

  int adc_raw = 0;
  int voltage_at_pin = 0;

  // 1. Read Raw ADC
  // We take a single sample here. For better stability, you could average 10 samples.
  ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHAN_BATT, &adc_raw));

  // 2. Convert to Voltage (Calibration)
  if (do_calibration) {
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, adc_raw, &voltage_at_pin));
  } else {
    // Fallback: Crude estimation (Not recommended for prod)
    // Max ADC (4095) ~= 3100mV roughly at 12dB
    voltage_at_pin = (adc_raw * 3100) / 4095;
  }

  // 3. Apply Divider Ratio
  // V_batt = V_pin * Ratio
  *voltage_mv = (uint16_t)(voltage_at_pin * BATT_DIVIDER_RATIO);

  ESP_LOGD(POWER_MONITOR, "Batt Raw: %d, Pin: %d mV, Calc: %d mV", adc_raw, voltage_at_pin, *voltage_mv);
  return ESP_OK;
}

// Check if the Host (S3) is powered and reachable
bool power_monitor_is_host_connected(void) {
  if (adc_handle == NULL) return false;

  int adc_raw = 0;
  int voltage_at_pin = 0;

  // 1. Read Raw ADC from Host Detect Pin
  if (adc_oneshot_read(adc_handle, ADC_CHAN_HOST_DET, &adc_raw) != ESP_OK) {
    return false;
  }

  // 2. Convert to mV
  if (do_calibration) {
    adc_cali_raw_to_voltage(cali_handle, adc_raw, &voltage_at_pin);
  } else {
    voltage_at_pin = (adc_raw * 3100) / 4095;
  }

  // 3. Check Logic Level
  // We don't need exact voltage here, just "Is it high enough?"
  bool is_connected = (voltage_at_pin > HOST_DETECT_THRESH_MV);

  ESP_LOGD(POWER_MONITOR, "Host Check: %d mV (Raw %d) -> %s", voltage_at_pin, adc_raw, is_connected ? "ONLINE" : "OFFLINE");

  return is_connected;
}
/**
 * @file    gateway_payloads.h
 * @author  Mateusz Ressel (https://github.com/matt-ressel)
 * @brief   Shared data structures for Sensor Nodes, RCP, and AP.
 *
 * @details This header file acts as the central repository for all data
 *          structures exchanged within the IoT Gateway ecosystem.
 *          It defines:
 *          1. Over-the-Air (OTA) Payloads: The packed binary formats sent
 *             by end-devices (e.g., Env, Door) via ESP-NOW.
 *          2. Gateway Envelope: The `uart_gateway_packet_t` structure used
 *             to encapsulate raw radio frames with metadata (RSSI, MAC)
 *             for transport between RCP and AP.
 *          3. Device Types: Enums required for the AP's device registry
 *             to correctly parse the incoming flexible payloads.
 *
 * @version 0.4
 * @date    2025-12-28
 *
 * @copyright Copyright (c) 2025 Mateusz Ressel. Licensed under the MIT License.
 */

#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#include <stdbool.h>  // Required for bool type
#include <stdint.h>   // Required for standard integer types (e.g., int16_t, uint16_t, uint8_t)

/* ========================================================================
 * 1. DEVICE TYPES (Used by AP Registry to identify sensors)
 * ======================================================================== */

/**
 * @brief Enum to classify devices in the AP's registry.
 * The AP uses this to decide which structure to cast the payload to.
 */
typedef enum {
  DEVICE_TYPE_UNKNOWN = 0,
  DEVICE_TYPE_ENV_SENSOR,   /**< Sends payload_env_t (Temp/Hum) */
  DEVICE_TYPE_DOOR_SENSOR,  /**< Sends payload_door_t (Open/Close) */
  DEVICE_TYPE_MOTION_SENSOR /**< Sends payload_motion_t (PIR) */
} device_type_t;

/* ========================================================================
 * 2. SENSOR PAYLOADS (Data sent over ESP-NOW by sensors)
 * ======================================================================== */

/**
 * @brief Payload for Environmental Sensor (Type: DEVICE_TYPE_ENV_SENSOR)
 */
typedef struct __attribute__((packed)) {
  int16_t temperature; /**< Temperature in 0.1°C units */
  uint8_t humidity;    /**< Relative Humidity in % */
  uint16_t pressure;   /**< Atmospheric Pressure in 0.1 hPa units */
  int16_t air_quality; /**< Air Quality Index (AQI) or -1 if not available */
  uint16_t battery_mv; /**< Battery voltage in mV */
  uint8_t crc;         /**< CRC-8 checksum for data integrity */
} payload_env_t;       // Total structure size: 2 (temp) + 1 (hum) + 2 (press) + 2 (aq) + 2 (batt) + 1 (crc) = 10 bytes.

/**
 * @brief Payload for Door/Window Sensor (Type: DEVICE_TYPE_DOOR_SENSOR)
 */
typedef struct __attribute__((packed)) {
  uint8_t is_open;         /**< 1 = Open, 0 = Closed */
  uint16_t battery_mv;     /**< Battery voltage in mV */
  uint8_t alarm_triggered; /**< 1 = Forced entry / Tamper */
} payload_door_t;

/**
 * @brief Payload for PIR Motion Sensor (Type: DEVICE_TYPE_MOTION_SENSOR)
 */
typedef struct __attribute__((packed)) {
  uint8_t motion_detected; /**< 1 = Motion detected */
  uint32_t duration_sec;   /**< How long motion lasted */
  uint16_t battery_mv;     /**< Battery voltage in mV */
} payload_motion_t;

/* ========================================================================
 * 3. GATEWAY ENVELOPE (UART Protocol: RCP -> AP)
 * ======================================================================== */

/**
 * @brief The Main Envelope for sending Sensor Data from RCP to AP.
 *
 * RCP does NOT parse the payload. It fills the header (MAC, RSSI, TS)
 * and copies raw ESP-NOW bytes into the flexible `payload` array.
 */
typedef struct __attribute__((packed)) {
  // --- Header (Metadata) ---
  uint8_t mac_addr[6];    /**< Source MAC Address of the sensor */
  int8_t rssi;            /**< Received Signal Strength Indicator */
  uint32_t rcp_timestamp; /**< RCP Uptime in ms (for debugging order) */

  // --- Flexible Payload ---
  // Contains raw bytes of payload_env_t, payload_door_t, etc.
  // RCP uses: packet_size = sizeof(uart_gateway_packet_t) + data_len
  uint8_t payload[];
} uart_gateway_packet_t;

/* ========================================================================
 * 4. CONFIGURATION COMMANDS (UART Protocol: AP -> RCP)
 * ======================================================================== */

/**
 * @brief Command payload for CMD_ADD_PEER (AP configures RCP).
 * Used when AP tells RCP: "Start listening to this MAC with this Key".
 */
typedef struct __attribute__((packed)) {
  uint8_t mac_addr[6]; /**< Device MAC Address */
  uint8_t lmk[16];     /**< Local Master Key (AES-128) for ESP-NOW encryption */
} rcp_config_peer_t;

#endif  // SENSOR_DATA_H

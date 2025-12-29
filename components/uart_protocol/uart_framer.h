/**
 * @file    uart_framer.h
 * @author  Mateusz Ressel (https://github.com/matt-ressel)
 * @brief   Internal interface for the UART Framer logic.
 *
 * @details This file exposes the internal logic functions used by the
 *          hardware driver. It strictly separates the "Pure Logic" (Framer)
 *          from the "Hardware Driver" (Protocol), enabling easier testing
 *          and better modularity.
 *
 * @version 0.1
 * @date    2025-12-28
 *
 * @copyright Copyright (c) 2025 Mateusz Ressel. Licensed under the MIT License.
 */

#ifndef UART_FRAMER_H
#define UART_FRAMER_H

#include <stddef.h>  // for size_t
#include <stdint.h>  // for uint8_t, uint16_t

#include "esp_err.h"  // for esp_err_t

#define MAX_FRAME_SIZE 1024

// Parser States
typedef enum {
  STATE_WAIT_START,
  STATE_READ_HEADER,
  STATE_READ_PAYLOAD,
  STATE_READ_CRC,
  STATE_WAIT_END
} parser_state_t;

// Callback type for valid frames
typedef void (*framer_on_valid_frame_cb_t)(uint8_t cmd, const uint8_t* data, uint16_t len);

/**
 * @brief Initializes the framer state machine.
 * @param callback Function to call when a valid frame is fully parsed.
 */
void uart_framer_init(framer_on_valid_frame_cb_t callback);

/**
 * @brief Feeds a single byte into the parser state machine.
 * @param byte Raw byte from UART.
 */
void uart_framer_process_byte(uint8_t byte);

/**
 * @brief Constructs a raw frame buffer (Header + Data + CRC).
 *
 * @param[in]  cmd      Command ID.
 * @param[in]  payload  Data pointer.
 * @param[in]  len      Data length.
 * @param[out] out_buf  Allocated buffer (must be freed by caller).
 * @param[out] out_len  Length of the allocated buffer.
 *
 * @return esp_err_t ESP_OK or ESP_ERR_NO_MEM.
 */
esp_err_t uart_framer_build(uint8_t cmd, const uint8_t* payload, uint16_t len, uint8_t** out_buf, size_t* out_len);

#endif  // UART_FRAMER_H
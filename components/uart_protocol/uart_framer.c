/**
 * @file    uart_framer.c
 * @author  Mateusz Ressel (https://github.com/matt-ressel)
 * @brief   Platform-independent implementation of the framing logic.
 *
 * @details Implements a Finite State Machine (FSM) to parse incoming byte streams
 *          and reconstruct binary frames. It handles:
 *          - Frame synchronization (Magic Bytes 0xAA / 0x55)
 *          - CRC-16 Checksum verification
 *          - Dynamic payload length handling
 *
 *          Frame Format: [START][CMD][LEN_L][LEN_H][PAYLOAD...][CRC_L][CRC_H][END]
 *
 * @version 0.1
 * @date    2025-12-28
 *
 * @copyright Copyright (c) 2025 Mateusz Ressel. Licensed under the MIT License.
 */

#include "uart_framer.h"

#include <stdlib.h>  // for malloc, free
#include <string.h>  // for memcpy, memset

// ESP-IDF includes
#include "esp_crc.h"
#include "esp_log.h"

// Project includes
#include "inter_chip_protocol.h"

// Logging tag for this module
static const char* TAG = "UART_FRAMER";

// Struct to hold parser context
static struct {
  parser_state_t state;
  uint8_t cmd;
  uint16_t payload_len;
  uint16_t idx;
  uint8_t buffer[MAX_FRAME_SIZE];
  uint16_t received_crc;
  framer_on_valid_frame_cb_t callback;
} ctx;

// --- Helper: CRC ---
static uint16_t calculate_crc(uint8_t cmd, uint16_t len, const uint8_t* data) {
  uint16_t crc = 0;
  // 1. Command
  crc = esp_crc16_le(crc, &cmd, 1);
  // 2. Length (Little Endian)
  uint8_t len_buf[2] = {(uint8_t)(len & 0xFF), (uint8_t)((len >> 8) & 0xFF)};
  crc = esp_crc16_le(crc, len_buf, 2);
  // 3. Payload
  if (len > 0 && data) {
    crc = esp_crc16_le(crc, data, len);
  }
  return crc;
}

// --- API Implementation ---
// Initializes the framer state machine.
void uart_framer_init(framer_on_valid_frame_cb_t callback) {
  ctx.callback = callback;
  ctx.state = STATE_WAIT_START;
  ctx.idx = 0;
}

// Feeds a single byte into the parser state machine.
void uart_framer_process_byte(uint8_t byte) {
  switch (ctx.state) {
    case STATE_WAIT_START:
      if (byte == ICP_START_BYTE) {
        ctx.state = STATE_READ_HEADER;
        ctx.idx = 0;
      }
      break;

    case STATE_READ_HEADER:
      // Header: [CMD] [LEN_LSB] [LEN_MSB]
      if (ctx.idx == 0) {
        ctx.cmd = byte;
      } else if (ctx.idx == 1) {
        ctx.payload_len = byte;
      } else if (ctx.idx == 2) {
        ctx.payload_len |= (byte << 8);  // Little Endian

        // Sanity check
        if (ctx.payload_len > MAX_FRAME_SIZE) {
          ESP_LOGE(TAG, "Frame size %d exceeds limit. Resetting.", ctx.payload_len);
          ctx.state = STATE_WAIT_START;
        } else {
          ctx.state = (ctx.payload_len > 0) ? STATE_READ_PAYLOAD : STATE_READ_CRC;
          ctx.idx = 0;  // Reset index for the next state
        }
        break;
      }
      ctx.idx++;
      break;

    case STATE_READ_PAYLOAD:
      ctx.buffer[ctx.idx++] = byte;
      if (ctx.idx >= ctx.payload_len) {
        ctx.state = STATE_READ_CRC;
        ctx.idx = 0;
      }
      break;

    case STATE_READ_CRC:
      if (ctx.idx == 0)
        ctx.received_crc = byte;
      else {
        ctx.received_crc |= (byte << 8);
        ctx.state = STATE_WAIT_END;
      }
      ctx.idx++;
      break;

    case STATE_WAIT_END:
      if (byte == ICP_END_BYTE) {
        // Validation
        uint16_t calc = calculate_crc(ctx.cmd, ctx.payload_len, ctx.buffer);
        if (calc == ctx.received_crc) {
          // Success -> Notify Driver
          if (ctx.callback) {
            ctx.callback(ctx.cmd, ctx.buffer, ctx.payload_len);
          }
        } else {
          ESP_LOGW(TAG, "CRC Mismatch: Recv 0x%04X != Calc 0x%04X", ctx.received_crc, calc);
        }
        ctx.state = STATE_WAIT_START;

      } else if (byte == ICP_START_BYTE) {
        // 2. Fast Resync
        ESP_LOGW(TAG, "Missing END byte, but found START. Resyncing immediately.");
        ctx.state = STATE_READ_HEADER;
        ctx.idx = 0;
      } else {
        ESP_LOGW(TAG, "Frame Error: Missing END byte");
        ctx.state = STATE_WAIT_START;
      }
      break;

    default:
      ctx.state = STATE_WAIT_START;
      break;
  }
}

// --- Frame Builder ---
// Constructs a raw frame buffer (Header + Data + CRC).
esp_err_t uart_framer_build(uint8_t cmd, const uint8_t* payload, uint16_t len, uint8_t** out_buf, size_t* out_len) {
  // Frame: START(1) + CMD(1) + LEN(2) + PAYLOAD(len) + CRC(2) + END(1)
  size_t total_len = 7 + len;

  uint8_t* buf = malloc(total_len);
  if (!buf) return ESP_ERR_NO_MEM;

  size_t idx = 0;
  buf[idx++] = ICP_START_BYTE;
  buf[idx++] = cmd;
  buf[idx++] = (uint8_t)(len & 0xFF);
  buf[idx++] = (uint8_t)((len >> 8) & 0xFF);

  if (len > 0 && payload) {
    memcpy(&buf[idx], payload, len);
    idx += len;
  }

  uint16_t crc = calculate_crc(cmd, len, payload);
  buf[idx++] = (uint8_t)(crc & 0xFF);
  buf[idx++] = (uint8_t)((crc >> 8) & 0xFF);
  buf[idx++] = ICP_END_BYTE;

  *out_buf = buf;
  *out_len = total_len;

  return ESP_OK;
}
/**
 * @file    uart_protocol.h
 * @author  Mateusz Ressel (https://github.com/matt-ressel)
 * @brief   Public API for the reliable, event-driven UART communication protocol.
 *
 * @details This header defines the configuration structures and functions required
 *          to initialize the inter-chip communication between the Application
 *          Processor (AP) and the Radio Co-Processor (RCP).
 *          It provides high-level `send` functions and registers callbacks for
 *          received packets.
 *
 * @version 0.1
 * @date    2025-12-28
 *
 * @copyright Copyright (c) 2025 Mateusz Ressel. Licensed under the MIT License.
 */

#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"

// --- Internal Configuration ---
/**
 * @brief Configuration parameters for the UART Protocol.
 */
#define RX_BUF_SIZE 2048     /**< Size of the UART RX buffer */
#define TX_BUF_SIZE 2048     /**< Size of the UART TX buffer */
#define EVENT_QUEUE_SIZE 20  /**< Size of the UART event queue */
#define MAX_FRAME_SIZE 1024  /**< Maximum allowed payload size */
#define UART_TASK_STACK 4096 /**< Stack size for the UART event task */
#define UART_TASK_PRIO 12    /**< High priority to prevent RX buffer overruns */

/**
 * @brief Callback function prototype for received frames.
 *
 * This function is called by the UART task whenever a valid frame (correct CRC
 * and framing) is received.
 *
 * @param cmd   The command ID extracted from the frame header.
 * @param data  Pointer to the payload data (valid only during the callback).
 * @param len   Length of the payload data.
 */
typedef void (*uart_frame_callback_t)(uint8_t cmd, const uint8_t* data, uint16_t len);

/**
 * @brief Configuration structure for the UART Protocol component.
 */
typedef struct {
  uart_port_t uart_num;           /**< UART port number (e.g., UART_NUM_1) */
  int tx_pin;                     /**< GPIO number for TX pin */
  int rx_pin;                     /**< GPIO number for RX pin */
  int baud_rate;                  /**< Communication speed (e.g., 115200 or 921600) */
  uart_frame_callback_t callback; /**< Handler for valid received packets */
} uart_protocol_config_t;

/**
 * @brief Helper macro to send a structure or variable without manually casting and calculating size.
 *
 * Usage example:
 * @code
 *   sensor_data_t my_data = { ... };
 *   UART_SEND_STRUCT(CMD_SENSOR_DATA, my_data);
 * @endcode
 *
 * @param cmd_id  The command ID (e.g., CMD_SENSOR_DATA).
 * @param object  The actual variable/structure instance (NOT a pointer).
 */
#define UART_SEND_STRUCT(cmd_id, object) \
  uart_protocol_send(cmd_id, (const uint8_t*)&(object), sizeof(object));

/**
 * @brief Helper macro to send an empty command (e.g., PING).
 *
 * Usage example:
 * @code
 *   UART_SEND_CMD(CMD_PING);
 * @endcode
 *
 * @param cmd_id The command ID.
 */
#define UART_SEND_CMD(cmd_id) \
  uart_protocol_send(cmd_id, NULL, 0);

/**
 * @brief Initializes the UART driver and the Event processing task.
 *
 * This function installs the UART driver with an event queue, configures pins,
 * and spawns a FreeRTOS task that listens for hardware events and parses
 * incoming data streams.
 *
 * @param[in] config Pointer to the configuration structure.
 *
 * @return esp_err_t
 *   - ESP_OK: Success.
 *   - ESP_ERR_INVALID_ARG: If config is NULL.
 *   - ESP_FAIL: If driver installation fails.
 */
esp_err_t uart_protocol_init(const uart_protocol_config_t* config);

/**
 * @brief Constructs a frame and sends it over UART.
 *
 * This function wraps the data in the protocol structure:
 * [START] [CMD] [LEN] [PAYLOAD] [CRC] [END]
 * It calculates the CRC16 automatically.
 *
 * @param[in] cmd   Command ID to send.
 * @param[in] data  Pointer to the data payload (can be NULL if len is 0).
 * @param[in] len   Length of the data payload.
 *
 * @return esp_err_t
 *   - ESP_OK: Data written to UART TX buffer.
 *   - ESP_ERR_NO_MEM: Failed to allocate temporary buffer for framing.
 */
esp_err_t uart_protocol_send(uint8_t cmd, const uint8_t* data, uint16_t len);

/**
 * @brief Deinitializes the UART driver and stops the task.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t uart_protocol_deinit(void);

#endif  // UART_PROTOCOL_H
/**
 * @file    uart_protocol.c
 * @author  Mateusz Ressel (https://github.com/matt-ressel)
 * @brief   Hardware abstraction implementation for the UART Protocol.
 *
 * @details This component manages the physical UART driver using the ESP-IDF
 *          Event Queue model. It spawns a dedicated FreeRTOS task to handle
 *          hardware interrupts (RX data, FIFO overflow, errors) and feeds
 *          the raw byte stream into the logical Framer component.
 *
 * @version 0.1
 * @date    2025-12-28
 *
 * @copyright Copyright (c) 2025 Mateusz Ressel. Licensed under the MIT License.
 */

#include "uart_protocol.h"

#include <string.h>  // for memset

// ESP-IDF includes
#include "driver/uart.h"
#include "esp_crc.h"  // for CRC calculations
#include "esp_log.h"

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// Project includes
#include "inter_chip_protocol.h"
#include "uart_framer.h"

static const char* TAG = "UART_PROTOCOL";

// --- Module Context ---
static uart_port_t g_uart_num;
static QueueHandle_t g_uart_event_queue;
static TaskHandle_t g_task_handle = NULL;

// --- Main Event Task ---

/**
 * @brief FreeRTOS task handling UART events.
 *
 * Waits for interrupt events from the UART driver and feeds data to the parser.
 */
static void uart_event_task(void* pvParameters) {
  uart_event_t event;
  uint8_t* dtmp = (uint8_t*)malloc(RX_BUF_SIZE);

  // Check allocation
  if (dtmp == NULL) {
    ESP_LOGE(TAG, "Failed to allocate RX buffer");
    vTaskDelete(NULL);
    return;
  }

  while (1) {
    // Wait for UART event (Block indefinitely)
    if (xQueueReceive(g_uart_event_queue, (void*)&event, portMAX_DELAY)) {
      // Clear temp buffer for safety
      memset(dtmp, 0, RX_BUF_SIZE);

      switch (event.type) {
        case UART_DATA:
          // Read raw data from hardware FIFO
          uart_read_bytes(g_uart_num, dtmp, event.size, portMAX_DELAY);

          // Feed the State Machine byte-by-byte
          for (int i = 0; i < event.size; i++) {
            uart_framer_process_byte(dtmp[i]);
          }
          break;

        // --- Hardware Error Handling ---
        case UART_FIFO_OVF:
          ESP_LOGE(TAG, "Hardware FIFO Overflow");
          uart_flush_input(g_uart_num);
          xQueueReset(g_uart_event_queue);
          break;

        case UART_BUFFER_FULL:
          ESP_LOGE(TAG, "Ring Buffer Full");
          uart_flush_input(g_uart_num);
          xQueueReset(g_uart_event_queue);
          break;

        case UART_BREAK:
          ESP_LOGI(TAG, "UART Break detected");
          break;

        case UART_PARITY_ERR:
        case UART_FRAME_ERR:
          // Usually caused by noise or baud rate mismatch
          ESP_LOGW(TAG, "UART Signal Error (Noise/Frame Err)");
          break;

        default:
          ESP_LOGD(TAG, "Unhandled UART Event: %d", event.type);
          break;
      }
    }
  }
  // Free allocated buffer and delete task
  free(dtmp);
  dtmp = NULL;
  vTaskDelete(NULL);
}

// --- Initialize the UART Protocol module ---
// Sets up UART driver, event queue, and processing task.
esp_err_t uart_protocol_init(const uart_protocol_config_t* config) {
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  g_uart_num = config->uart_num;

  uart_framer_init(config->callback);

  uart_config_t uart_conf = {
      .baud_rate = config->baud_rate,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_LOGI(TAG, "Initializing UART%d on TX:%d RX:%d at %d baud", config->uart_num, config->tx_pin, config->rx_pin, config->baud_rate);

  // Install driver WITH event queue
  ESP_ERROR_CHECK(uart_driver_install(g_uart_num, RX_BUF_SIZE, TX_BUF_SIZE, EVENT_QUEUE_SIZE, &g_uart_event_queue, 0));

  ESP_ERROR_CHECK(uart_param_config(g_uart_num, &uart_conf));

  ESP_ERROR_CHECK(uart_set_pin(g_uart_num, config->tx_pin, config->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

  // Create the event handling task
  BaseType_t ret = xTaskCreate(uart_event_task, "uart_proto_task", UART_TASK_STACK, NULL, UART_TASK_PRIO, &g_task_handle);

  return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

// Sends a framed packet over UART
// Constructs the frame and writes to UART hardware.
esp_err_t uart_protocol_send(uint8_t cmd, const uint8_t* data, uint16_t len) {
  uint8_t* raw_frame = NULL;
  size_t raw_len = 0;

  // 1. Ask Engine to build the frame
  esp_err_t err = uart_framer_build(cmd, data, len, &raw_frame, &raw_len);
  if (err != ESP_OK) return err;

  // 2. Hardware Send
  int written = uart_write_bytes(g_uart_num, raw_frame, raw_len);

  // 3. Cleanup
  free(raw_frame);

  return (written == raw_len) ? ESP_OK : ESP_FAIL;
}

// Deinitialize the UART Protocol module
esp_err_t uart_protocol_deinit(void) {
  if (g_task_handle != NULL) {
    vTaskDelete(g_task_handle);
    g_task_handle = NULL;
  }
  return uart_driver_delete(g_uart_num);
}
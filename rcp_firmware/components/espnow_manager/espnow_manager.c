/**
 * @file espnow_manager.c
 * @author Mateusz Ressel (https://github.com/matt-ressel)
 *
 * @brief Implementation of the ESP-NOW manager module for the RCP firmware.
 *
 * This file contains the logic to initialize and manage ESP-NOW communication,
 * including peer management and event handling. It sets up the necessary WiFi
 * services, ESP-NOW stack, and FreeRTOS tasks/queues for processing ESP-NOW events.
 *
 * @version 0.3
 * @date    2025-12-28
 *
 * @copyright Copyright (c) 2025 Mateusz Ressel. Licensed under the MIT License.
 *
 */

#include "espnow_manager.h"

// ESP-IDF includes
#include "esp_err.h"    // Error handling definitions (ESP_OK, ESP_FAIL, etc.)
#include "esp_event.h"  // For esp_event_loop_create_default
#include "esp_log.h"    // For ESP-IDF logging utilities
#include "esp_mac.h"    // MAC address utilities (MACSTR, MAC2STR, ESP_NOW_ETH_ALEN)
#include "esp_netif.h"  // For esp_netif_init
#include "esp_now.h"    // ESP-NOW API functions
#include "esp_wifi.h"   // For core WiFi functionality and definitions

// FreeRTOS includes
#include "freertos/FreeRTOS.h"  // For FreeRTOS types and functions
#include "freertos/queue.h"     // For FreeRTOS queues
#include "freertos/task.h"      // For FreeRTOS tasks

// Project includes
#include "inter_chip_protocol.h"  // FOR CMD_SENSOR_DATA definition
#include "sensor_data.h"          // Definition of sensor_data_t structure
#include "uart_protocol.h"        // UART protocol functions

// Logging tag for this module
static const char* ESPNOW_MANAGER = "ESP-NOW Manager";

/** @brief Handle for the queue that stores incoming ESP-NOW events from the callback. */
static QueueHandle_t s_espnow_event_queue = NULL;

// Function to add a new ESP-NOW peer with encryption
esp_err_t add_peer_with_encryption(const uint8_t* peer_addr) {
  if (peer_addr == NULL) {
    ESP_LOGE(ESPNOW_MANAGER, "add_peer_with_encryption: peer_addr argument is NULL.");
    return ESP_ERR_INVALID_ARG;
  }

  // Check if the peer already exists in the ESP-NOW peer list
  if (esp_now_is_peer_exist(peer_addr)) {
    ESP_LOGD(ESPNOW_MANAGER, "Peer " MACSTR " already exists in ESP-NOW list. No action needed.", MAC2STR(peer_addr));
    return ESP_OK;
  }

  // Prepare the peer information structure for ESP-NOW
  esp_now_peer_info_t peer_info = {0};
  memcpy(peer_info.peer_addr, peer_addr, ESP_NOW_ETH_ALEN);
  peer_info.channel = CONFIG_ESPNOW_CHANNEL;
  peer_info.ifidx = ESP_IF_WIFI_STA;
  peer_info.encrypt = true;
  memcpy(peer_info.lmk, CONFIG_ESPNOW_LMK, ESP_NOW_KEY_LEN);

  // Attempt to add the new peer with encryption
  ESP_LOGI(ESPNOW_MANAGER, "Attempting to add new ESP-NOW peer: " MACSTR " with encryption.", MAC2STR(peer_addr));
  esp_err_t result = esp_now_add_peer(&peer_info);

  // Check the result of adding the peer
  if (result == ESP_OK) {
    // Successfully added the peer
    ESP_LOGI(ESPNOW_MANAGER, "Successfully added new encrypted peer: " MACSTR, MAC2STR(peer_addr));
  } else if (result == ESP_ERR_ESPNOW_EXIST) {
    // Peer already exists, consider it a success
    ESP_LOGW(ESPNOW_MANAGER, "Peer " MACSTR " already existed (esp_now_add_peer returned ESP_ERR_ESPNOW_EXIST). Considered as success.", MAC2STR(peer_addr));
    result = ESP_OK;
  } else {
    // Failed to add the peer for some reason
    ESP_LOGE(ESPNOW_MANAGER, "Failed to add ESP-NOW peer: " MACSTR ". Error: %s (0x%X)", MAC2STR(peer_addr), esp_err_to_name(result), result);
  }
  return result;
}

// WiFi service initialization for ESP-NOW
static void wifi_service_init(void) {
  ESP_LOGI(ESPNOW_MANAGER, "Initializing network interface (netif)...");
  ESP_ERROR_CHECK(esp_netif_init());

  ESP_LOGI(ESPNOW_MANAGER, "Creating default event loop...");
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Use default WiFi initialization configuration
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_LOGI(ESPNOW_MANAGER, "Initializing WiFi driver with default configuration...");
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_LOGI(ESPNOW_MANAGER, "Setting WiFi storage to RAM...");
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

  ESP_LOGI(ESPNOW_MANAGER, "Setting WiFi mode to STA...");
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

  ESP_LOGI(ESPNOW_MANAGER, "Starting WiFi stack...");
  ESP_ERROR_CHECK(esp_wifi_start());

  // Ensure bandwidth is set to HT20 for ESP-NOW compatibility
  ESP_LOGI(ESPNOW_MANAGER, "Setting WiFi bandwidth to HT20...");
  ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));

  // Set WiFi channel for ESP-NOW operation
  ESP_LOGI(ESPNOW_MANAGER, "Setting WiFi channel to %d...", CONFIG_ESPNOW_CHANNEL);
  ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

  // Set the WiFi protocol. For ESP-NOW, 802.11b/g/n and LR (Long Range) are common.
  // Adjust as needed for compatibility with peer devices.
  ESP_LOGI(ESPNOW_MANAGER, "Setting WiFi protocol (B/G/N + LR)...");
  ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));

  // Disable WiFi power save mode to improve reliability of ESP-NOW communication.
  ESP_LOGI(ESPNOW_MANAGER, "Disabling WiFi power save mode for ESP-NOW");
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

  // Retrieve and log the MAC address of the configured WiFi interface (STA).
  uint8_t my_mac_addr[ESP_NOW_ETH_ALEN] = {0};  // Local variable to store MAC address
  ESP_LOGI(ESPNOW_MANAGER, "Retrieving MAC address for STA interface...");
  ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, my_mac_addr));

  ESP_LOGI(ESPNOW_MANAGER, "RCP WiFi Ready. MAC: " MACSTR " | Channel: %d", MAC2STR(my_mac_addr), CONFIG_ESPNOW_CHANNEL);
}

/**
 * @brief ESP-NOW receive callback function (executed in ISR context).
 *
 * This callback is invoked by the ESP-NOW stack when data is received. It copies
 * the sender's MAC address and the payload, then posts an `espnow_event_t` to a
 * FreeRTOS queue (`s_espnow_event_queue`) for processing by a dedicated task.
 *
 * @note This function runs in a high-priority Wi-Fi task context. Processing here
 *       should be minimal and non-blocking to avoid disrupting Wi-Fi operations.
 *       Memory allocation (`malloc`) is used, and the corresponding `free` must be
 *       called by the consumer task (`espnow_task`).
 *
 * @param recv_info Pointer to a structure with receive metadata (e.g., MAC address, RSSI).
 * @param data Pointer to the buffer containing the received data.
 * @param len Length of the received data in bytes.
 */
static void espnow_recv_cb(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len) {
  espnow_event_t evt;
  espnow_event_recv_cb_t* recv_cb_data = &evt.info.recv_cb;

  if (recv_info == NULL || recv_info->src_addr == NULL || data == NULL || len <= 0) {
    ESP_LOGE(ESPNOW_MANAGER, "Receive callback error: Invalid arguments received.");
    return;
  }

  // Log received packet information
  uint8_t* mac_addr = recv_info->src_addr;
  int8_t rssi = 0;  // Default RSSI value

  if (recv_info->rx_ctrl) {
    rssi = (int8_t)recv_info->rx_ctrl->rssi;
    ESP_LOGI(ESPNOW_MANAGER, "Received data from: " MACSTR ", Len: %d, RSSI: %d dBm", MAC2STR(mac_addr), len, rssi);
  } else {
    ESP_LOGI(ESPNOW_MANAGER, "Received data from: " MACSTR ", Len: %d (No RSSI)", MAC2STR(mac_addr), len);
  }

  // Prepare event data to be sent to the queue
  evt.id = ESPNOW_RECV_CB;
  memcpy(recv_cb_data->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);

  recv_cb_data->rssi = rssi;

  // Allocate memory for the data payload. This memory will be freed by the consumer task.
  recv_cb_data->data = malloc(len);
  if (recv_cb_data->data == NULL) {
    ESP_LOGE(ESPNOW_MANAGER, "Receive callback error: Failed to allocate memory for data buffer (size: %d bytes)", len);
    return;
  }
  memcpy(recv_cb_data->data, data, len);
  recv_cb_data->data_len = len;

  // Send the event to the queue.
  if (xQueueSend(s_espnow_event_queue, &evt, pdMS_TO_TICKS(ESPNOW_QUEUE_TIMEOUT_TICKS)) != pdTRUE) {
    ESP_LOGW(ESPNOW_MANAGER, "Receive callback warning: Failed to send event to espnow_task queue. Discarding data from " MACSTR ".", MAC2STR(mac_addr));
    free(recv_cb_data->data);
    recv_cb_data->data = NULL;
  } else {
    ESP_LOGD(ESPNOW_MANAGER, "Event from " MACSTR " successfully sent to espnow_task queue.", MAC2STR(mac_addr));
  }
}

// Dedicated FreeRTOS task to process ESP-NOW events from the queue.
static void espnow_task(void* pvParameter) {
  espnow_event_t evt;  // Variable to hold events received from the queue

  // uint32_t current_timestamp;  // To store the timestamp of received data
  ESP_LOGI(ESPNOW_MANAGER, "ESP-NOW processing task started.");

  // Ensure the event queue is valid before entering the loop
  while (xQueueReceive(s_espnow_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
    switch (evt.id) {
      case ESPNOW_RECV_CB: {
        // Handle received data event
        espnow_event_recv_cb_t* recv_cb = &evt.info.recv_cb;

        // Log the received data length and MAC address
        ESP_LOGD(ESPNOW_MANAGER, "Data from " MACSTR ", Len: %d", MAC2STR(recv_cb->mac_addr), recv_cb->data_len);
        // Get current timestamp
        // Ensure system time is set for this to be meaningful (e.g., via I2C command from RPi)
        // current_timestamp = (uint32_t)time(NULL);
        // if (current_timestamp == 0) {
        //   ESP_LOGW(ESPNOW_MANAGER, "System time not set (timestamp is 0). Timestamps may not be meaningful yet.");
        // }

        // 1. Calculate total size required for the Gateway Packet
        // Size = Header (MAC, RSSI, TS) + Payload (ESP-NOW data)
        size_t packet_size = sizeof(uart_gateway_packet_t) + recv_cb->data_len;

        // 2. Allocate temporary buffer
        uint8_t* tx_buffer = malloc(packet_size);

        if (tx_buffer != NULL) {
          uart_gateway_packet_t* pkt = (uart_gateway_packet_t*)tx_buffer;

          // 3. Fill the Gateway Header
          memcpy(pkt->mac_addr, recv_cb->mac_addr, 6);

          pkt->rssi = recv_cb->rssi;

          // Timestamp (RCP uptime in ms)
          // pkt->rcp_timestamp = (uint32_t)(esp_timer_get_time() / 1000);
          pkt->rcp_timestamp = 5748340;

          // 4. Copy the RAW ESP-NOW payload (Opaque Data)
          memcpy(pkt->payload, recv_cb->data, recv_cb->data_len);

          // 5. Send via UART Protocol
          uart_protocol_send(CMD_SENSOR_DATA, tx_buffer, packet_size);

          free(tx_buffer);  // Clean up UART buffer
        } else {
          ESP_LOGE(ESPNOW_MANAGER, "Failed to allocate memory for UART forwarding");
        }

        // Scenario A: Check if we are connected to the AP
        // if (power_monitor_is_ap_connected()) {
        //   // AP is reachable, attempt to send data via UART
        //   sensor_data_packet_t packet;
        //   memcpy(packet.mac_addr, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
        //   packet.len = recv_cb->data_len;
        //   memcpy(packet.data, recv_cb->data, recv_cb->data_len);
        //   packet.rssi = power_monitor_get_last_rssi();  // Get last known RSSI
        //   esp_err_t ret = uart_transport_send_sensor_data(
        //       packet.mac_addr,
        //       packet.data,
        //       packet.len,
        //       packet.rssi);

        //   if (ret != ESP_OK) {
        //     // UART send failed despite AP being reachable -> Save to NVS
        //     // nvs_manager_save_event(EVENT_TYPE_SENSOR_DATA, &packet, sizeof(packet));
        //   }

        // } else {
        //   // Scenario B: AP is not reachable (Battery mode) -> Save directly to NVS
        //   // Save the entire structure (including MAC and RSSI) so we know who sent it upon retrieval and timestamp
        //   // nvs_manager_save_event(EVENT_TYPE_SENSOR_DATA, &packet, sizeof(packet));
        // }

        // Free the dynamically allocated data buffer after processing
        free(recv_cb->data);
        recv_cb->data = NULL;
        break;
      }
      default:
        ESP_LOGW(ESPNOW_MANAGER, "Received unknown event ID: %d", evt.id);
        break;
    }
  }
}

static esp_err_t espnow_init(void) {
  esp_log_level_set(ESPNOW_MANAGER, ESP_LOG_DEBUG);  // Set log level for this module to DEBUG

  esp_err_t ret;

  ESP_LOGI(ESPNOW_MANAGER, "Initializing ESP-NOW service...");

  // Create a queue to buffer events from the ESP-NOW callback, decoupling the ISR from the processing task.
  s_espnow_event_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnow_event_t));
  if (s_espnow_event_queue == NULL) {
    ESP_LOGE(ESPNOW_MANAGER, "Failed to create ESP-NOW event queue (s_espnow_event_queue)!");
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(ESPNOW_MANAGER, "ESP-NOW event queue created (size: %d).", ESPNOW_QUEUE_SIZE);

  // Initialize the ESP-NOW stack
  ret = esp_now_init();
  if (ret != ESP_OK) {
    ESP_LOGE(ESPNOW_MANAGER, "Failed to initialize ESP-NOW library: %s (0x%X)", esp_err_to_name(ret), ret);
    vQueueDelete(s_espnow_event_queue);
    s_espnow_event_queue = NULL;
    return ret;
  }

  // Register the receive callback function.
  // This function will be called automatically whenever a packet is received.
  ret = esp_now_register_recv_cb(espnow_recv_cb);
  if (ret != ESP_OK) {
    ESP_LOGE(ESPNOW_MANAGER, "Failed to register ESP-NOW receive callback: %s (0x%X)", esp_err_to_name(ret), ret);
    esp_now_deinit();
    vQueueDelete(s_espnow_event_queue);
    s_espnow_event_queue = NULL;
    return ret;
  }

  // Set the Primary Master Key(PMK).
  // This key is used to establish encrypted communication sessions with peers.
  ret = esp_now_set_pmk((uint8_t*)CONFIG_ESPNOW_PMK);
  if (ret != ESP_OK) {
    ESP_LOGE(ESPNOW_MANAGER, "Failed to set ESP-NOW PMK: %s (0x%X)", esp_err_to_name(ret), ret);
    esp_now_deinit();
    vQueueDelete(s_espnow_event_queue);
    s_espnow_event_queue = NULL;
    return ret;
  }

  // Load the list of allowed MAC addresses from NVS and add them as peers.
  // This ensures that the gateway can communicate with known sensors after a restart.
  // ESP_LOGI(ESPNOW_MANAGER, "Loading allowed MACs from NVS and adding as ESP-NOW peers...");
  // allowed_mac_list_t allowed_list_from_nvs;
  // esp_err_t nvs_read_err = read_mac_list_from_nvs(&allowed_list_from_nvs);

  // if (nvs_read_err != ESP_OK) {
  //   ESP_LOGE(ESPNOW_MANAGER, "Failed to read allowed MAC list from NVS during init (%s). No peers will be pre-added from NVS.", esp_err_to_name(nvs_read_err));
  // } else {
  //   if (allowed_list_from_nvs.count == 0) {
  //     ESP_LOGI(ESPNOW_MANAGER, "NVS allowed MAC list is empty. No peers to pre-add at init.");
  //   } else {
  //     ESP_LOGI(ESPNOW_MANAGER, "Found %d allowed MAC(s) in NVS. Attempting to add them as ESP-NOW peers...", allowed_list_from_nvs.count);
  //     for (size_t i = 0; i < allowed_list_from_nvs.count; ++i) {
  //       esp_err_t peer_add_ret = add_peer_with_encryption(allowed_list_from_nvs.macs[i]);
  //       if (peer_add_ret != ESP_OK) {
  //         ESP_LOGW(ESPNOW_MANAGER, "Failed to add peer " MACSTR " from NVS list (Error: %s). Continuing with next.", MAC2STR(allowed_list_from_nvs.macs[i]), esp_err_to_name(peer_add_ret));
  //       }
  //     }
  //   }
  //   ESP_LOGI(ESPNOW_MANAGER, "Finished processing NVS allowed MAC list for ESP-NOW peers.");
  // }
  // For initial testing, add a hardcoded peer (replace with actual MAC as needed)
  uint8_t mac_addr[1][ESP_NOW_ETH_ALEN] = {
      {0xb4, 0x3a, 0x45, 0x6c, 0xdb, 0xe8},  // ESP32C3-BME280-board (TESTING)
  };
  esp_err_t peer_add_ret = add_peer_with_encryption(mac_addr[0]);
  if (peer_add_ret != ESP_OK) {
    ESP_LOGW(ESPNOW_MANAGER, "Failed to add peer " MACSTR " from NVS list (Error: %s). Continuing with next.", MAC2STR(mac_addr[0]), esp_err_to_name(peer_add_ret));
  }

  // Create and start the dedicated FreeRTOS task for processing the events.
  BaseType_t task_created = xTaskCreate(espnow_task, "espnow_task", ESPNOW_STACK_SIZE, NULL, ESPNOW_TASK_PRIORITY, NULL);
  if (task_created != pdPASS) {
    ESP_LOGE(ESPNOW_MANAGER, "Failed to create ESP-NOW processing task (espnow_task)!");
    esp_now_set_pmk(NULL);
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    vQueueDelete(s_espnow_event_queue);
    s_espnow_event_queue = NULL;
    return ESP_FAIL;
  }

  ESP_LOGI(ESPNOW_MANAGER, "ESP-NOW Initialized successfully, and processing task started.");
  return ESP_OK;
}

// Sets up WiFi and ESP-NOW services.
esp_err_t espnow_manager_init(void) {
  ESP_LOGI(ESPNOW_MANAGER, "Starting ESP-NOW Manager initialization...");

  // Initialize WiFi service for ESP-NOW
  ESP_LOGI(ESPNOW_MANAGER, "Initializing WiFi service for ESP-NOW...");
  wifi_service_init();

  // Initialize ESP-NOW
  ESP_ERROR_CHECK(espnow_init());

  ESP_LOGI(ESPNOW_MANAGER, "ESP-NOW Manager initialized successfully. Waiting for messages on channel %d...", CONFIG_ESPNOW_CHANNEL);
  return ESP_OK;
}
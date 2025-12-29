/**
 * @file    inter_chip_protocol.h
 * @author  Mateusz Ressel (https://github.com/matt-ressel)
 * @brief   Shared definitions for the UART Inter-Chip Protocol (ICP).
 *
 * @details This header acts as the "Shared Contract" between the Application
 *          Controller (AP) and the Radio Co-Processor (RCP).
 *          It defines the wire format constants (Magic Bytes), Command IDs,
 *          and protocol constraints required to ensure binary compatibility
 *          between the two independent firmware projects.
 *
 *          @warning This file must remain identical in both AP and RCP projects!
 *
 * @version 0.1
 * @date    2025-12-28
 *
 * @copyright Copyright (c) 2025 Mateusz Ressel. Licensed under the MIT License.
 */

#ifndef INTER_CHIP_PROTOCOL_H
#define INTER_CHIP_PROTOCOL_H

/** @brief Magic byte indicating the start of a frame */
#define ICP_START_BYTE 0xAA

/** @brief Magic byte indicating the end of a frame */
#define ICP_END_BYTE 0x55

// --- Command Definitions ---
#define CMD_SENSOR_DATA 0x10  /**< Payload contains espnow_packet_t */
#define CMD_GET_BATTERY 0x20  /**< Request battery voltage */
#define CMD_RESP_BATTERY 0x21 /**< Response with battery voltage */
#define CMD_ADD_PEER 0x30     /**< Add a new ESP-NOW peer */
#define CMD_PING 0xA0         /**< Connectivity check */
#define CMD_PONG 0xA1         /**< Response to PING */

#endif  // INTER_CHIP_PROTOCOL_H
# ESP-NOW to MQTT Gateway (Dual-Core Architecture)

A professional, fault-tolerant IoT gateway designed with a dual-processor architecture (Co-Processor) to reliably bridge **ESP-NOW** communication to **MQTT**.

This project prioritizes **reliability** and **data continuity**. It features physical separation of the radio layer (RCP) from the application layer (AP), non-volatile memory (NVS) buffering in case of failures, and advanced hardware power management.

## 🏗 System Architecture

The system consists of two independent microcontrollers connected via a UART interface with logic level translation.

### 1. Application Controller (AP) - **ESP32-S3**
The "Brain" of operations. Handles business logic, WiFi/MQTT connectivity, and controls the power supply of the co-processor.
*   **Power:** USB (5V).
*   **Role:** Receives data from RCP, publishes to MQTT, manages RCP lifecycle (Power Cycle).
*   **Fail-Safe:** Buffers data to internal NVS if WiFi connection is lost.

### 2. Radio Co-Processor (RCP) - **ESP32-C6**
Dedicated specifically to handling the ESP-NOW physical layer.
*   **Power:** Controlled by AP (via Load Switch) + Battery (Backup/RTC).
*   **Role:** Listens for ESP-NOW packets, monitors AP "liveness", buffers data when AP is unresponsive.
*   **Fail-Safe:** If AP is powered but frozen (no response) or unpowered, RCP stores data in its local NVS buffer.

## 🔌 Hardware Interface & Glue Logic

The project utilizes dedicated intermediary components to ensure stability and protection against *backfeeding* and *phantom powering*:

| Function | Component | Description |
| :--- | :--- | :--- |
| **Power Switch** | `Vishay SiP32431DR3` | Allows the AP (S3) to physically cut power to the RCP (C6) for hard resets or current measurements. |
| **Level Translator** | `TI TXB0102DCUR` | Bidirectional UART translator with an **OE** (Output Enable) pin controlled by the AP. Prevents current leakage through data lines when one side is powered down. |

### Pinout Map

**ESP32-S3 (AP):**
*   `GPIO 2` -> **PWR_EN** (SiP32431 ON/OFF)
*   `GPIO 3` -> **UART_OE** (TXB0102 Output Enable)
*   `UART TX/RX` -> TXB0102 A-side

**ESP32-C6 (RCP):**
*   `GPIO 2` (ADC) -> AP Power Detection (Voltage divider from AP 5V rail)
*   `GPIO 3` (ADC) -> Battery Voltage Measurement
*   `UART TX/RX` -> TXB0102 B-side

## ⚙️ Logic & Fail-Safe Scenarios

### Scenario A: Normal Operation
1. RCP receives an ESP-NOW packet.
2. RCP checks ADC (GPIO 2) -> Confirms AP voltage is present.
3. RCP sends the frame via UART to AP.
4. AP receives the frame and publishes it to MQTT.

### Scenario B: AP Failure (Powered but frozen) or Power Loss
1. RCP receives an ESP-NOW packet.
2. RCP detects no voltage on ADC **OR** encounters UART transmission errors.
3. RCP writes the packet to its local **NVS** ring buffer.
4. Once AP is back online, RCP flushes the buffer upon request.

### Scenario C: WiFi/MQTT Failure
1. AP receives data from RCP via UART.
2. MQTT client is disconnected.
3. AP writes data to its local **NVS** buffer.
4. Upon reconnection, AP publishes pending messages ("Batch upload").

## 🛠️ Tech Stack
*   **Framework:** ESP-IDF (v5.x)
*   **Language:** C
*   **Communication Protocol:** Custom UART Protocol (Binary frames with CRC16)
*   **Libraries:** `esp_now`, `esp_mqtt`, `nvs_flash`, `driver/uart`

## 📂 Project Structure (Monorepo)

```text
esp-now-to-mqtt-gateway/
├── common/             # Shared protocol definitions (Header-only)
├── ap_firmware/        # Firmware for ESP32-S3 (Application Controller)
└── rcp_firmware/       # Firmware for ESP32-C6 (Radio Co-Processor)

#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <stdint.h> // Required for standard integer types
#include "esp_err.h"


// Type of event stored in NVS
typedef enum {
    EVENT_TYPE_SENSOR_DATA = 0x01,  // Sensor data log
    EVENT_TYPE_BATTERY_LOG = 0x02,  // Battery voltage log
    EVENT_TYPE_SYSTEM_ERR  = 0xEE   // Error or system event
} event_type_t;


typedef struct __attribute__((packed)) {
    uint32_t timestamp;  // Unix timestamp of the event
    uint8_t type;        // event_type_t
    uint8_t len;         // Length of the payload data in bytes
} nvs_record_header_t;

// Initialize NVS manager
esp_err_t nvs_manager_init(void);


// Save an event to NVS
esp_err_t nvs_manager_save_event(event_type_t type, const void* data, size_t len);

// Pop (retrieve and remove) the oldest event from NVS
esp_err_t nvs_manager_pop_event(event_type_t *type, void* buffer, size_t *len);

// Get the count of stored events in NVS
size_t nvs_manager_get_count(void);

#endif  // NVS_MANAGER_H
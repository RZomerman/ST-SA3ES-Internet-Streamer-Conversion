/**
 * @file event_log.h
 * @brief Deferred serial logging system (ISR-safe)
 *
 * This module provides thread-safe, ISR-safe logging via FreeRTOS queues.
 * ISRs post log events to a queue; a background task serializes them.
 * This prevents blocking in timing-critical code.
 */

#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <stdint.h>
#include "esp_err.h"
#include "config.h"

/* ============================================================================
 * Event Types
 * ============================================================================ */

typedef enum {
    EVENT_FRAME_RECEIVED,      /**< Complete 3-byte frame captured */
    EVENT_FRAME_MALFORMED,     /**< Incomplete or invalid frame */
    EVENT_READ_REQUEST,        /**< Read/status transaction detected */
    EVENT_DIN_RESPONSE_SENT,   /**< D-IN response clocked out */
    EVENT_STATE_UPDATE,        /**< Emulated state change */
    EVENT_INFO,                /**< General info message */
    EVENT_ERROR,               /**< Error message */
    EVENT_DEBUG,               /**< Debug message */
} event_type_t;

/* ============================================================================
 * Event Structure
 * ============================================================================ */

typedef struct {
    uint8_t state_id;
    uint32_t old_value;
    uint32_t new_value;
} event_state_change_t;

typedef struct {
    uint64_t timestamp_us;      /**< Event timestamp (microseconds) */
    event_type_t type;
    union {
        struct {
            uint8_t byte0, byte1, byte2;
            uint32_t divider;
            float frequency_mhz;
            uint8_t control_byte;
        } frame_data;
        struct {
            uint8_t response_byte;
            uint32_t bit_pattern;
        } din_response;
        event_state_change_t state_change;
        struct {
            char message[64];
        } text;
    } data;
} event_log_entry_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Initialize the event logging system
 * @return ESP_OK on success
 */
esp_err_t event_log_init(void);

/**
 * Post a frame-received event to the log queue (ISR-safe)
 * @param byte0, byte1, byte2 The three bytes of the frame
 */
void event_log_frame_received(uint8_t byte0, uint8_t byte1, uint8_t byte2);

/**
 * Post a malformed frame event (ISR-safe)
 * @param reason Short description of the malformation
 */
void event_log_frame_malformed(const char *reason);

/**
 * Post a read request event (ISR-safe)
 */
void event_log_read_request(void);

/**
 * Post a D-IN response transmission event (ISR-safe)
 * @param response_byte The byte that was clocked out
 */
void event_log_din_response(uint8_t response_byte);

/**
 * Post a state update event (ISR-safe)
 * @param state_id Which field changed (e.g., FREQUENCY, CONTROL_BYTE)
 * @param old_value Previous value
 * @param new_value New value
 */
void event_log_state_update(uint8_t state_id, uint32_t old_value, uint32_t new_value);

/**
 * Post a text message at a given log level (can be called from tasks only)
 * @param level Log level
 * @param format printf-style format string
 * @param ... Arguments
 */
void event_log_printf(log_level_t level, const char *format, ...);

/**
 * Flush any pending events to serial (blocking; call from task, not ISR)
 */
void event_log_flush(void);

/**
 * Get queue fill level (for diagnostics)
 */
uint32_t event_log_get_queue_depth(void);

#endif // EVENT_LOG_H

/**
 * @file lc72130_bus.h
 * @brief Low-level bus capture and D-IN transmission for Sanyo CCB
 *
 * This module:
 * - Configures GPIO interrupts for CE, CLK, DATA
 * - Buffers bits and frames with timestamps
 * - Drives D-IN output via RMT or GPIO during read transactions
 * - Detects transaction boundaries and malformations
 */

#ifndef LC72130_BUS_H
#define LC72130_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "config.h"

typedef enum {
    DIN_OVERRIDE_AUTO = 0,
    DIN_OVERRIDE_FORCE_HIGH,
    DIN_OVERRIDE_FORCE_LOW,
} din_override_t;

/* ============================================================================
 * Constants
 * ============================================================================ */

#define MAX_FRAME_BITS 24

/* ============================================================================
 * Frame Capture Result
 * ============================================================================ */

typedef enum {
    FRAME_INCOMPLETE,
    FRAME_VALID,
    FRAME_SHORT,
    FRAME_OVERLONG,
    FRAME_CLK_ERROR,
    FRAME_TIMEOUT,
} frame_status_t;

typedef struct {
    uint8_t byte0, byte1, byte2;
    uint32_t bit_count;
    frame_status_t status;
    uint64_t capture_time_us;
    uint64_t ce_rise_time_us;
    uint64_t ce_fall_time_us;
} captured_frame_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Initialize GPIO interrupts and bus capture
 * @return ESP_OK on success
 */
esp_err_t lc72130_bus_init(void);

/**
 * Start bus monitoring (enable GPIO interrupts)
 * @return ESP_OK on success
 */
esp_err_t lc72130_bus_start(void);

/**
 * Stop bus monitoring (disable GPIO interrupts)
 * @return ESP_OK on success
 */
esp_err_t lc72130_bus_stop(void);

/**
 * Check if a complete frame has been captured
 * Must be called repeatedly from the application task
 * @param frame Pointer to frame buffer (filled on return)
 * @return true if a frame is available, false otherwise
 */
bool lc72130_bus_frame_ready(captured_frame_t *frame);

/**
 * Drive D-IN output with a byte value (LSB-first, FALLING edge timing)
 * Only effective if GPIO18 is configured as output and mode is EMULATOR
 * @param response_byte The byte to transmit
 * @param bit_count Number of bits to transmit (typically 8)
 * @return ESP_OK if transmission started, or error if not in correct mode
 */
esp_err_t lc72130_bus_drive_din_response(uint8_t response_byte, uint8_t bit_count);

/**
 * Get the current state of CE input
 * @return 1 if CE is active (HIGH), 0 if inactive (LOW)
 */
uint32_t lc72130_bus_get_ce_state(void);

/**
 * Get the current state of CLK input
 * @return 1 if CLK is HIGH, 0 if CLK is LOW
 */
uint32_t lc72130_bus_get_clk_state(void);

/**
 * Get the current state of DATA input
 * @return 1 if DATA is HIGH, 0 if DATA is LOW
 */
uint32_t lc72130_bus_get_data_state(void);

/**
 * Get the current state of D-IN output
 * @return 1 if D-IN is driven HIGH, 0 if driven LOW or inactive
 */
uint32_t lc72130_bus_get_din_state(void);

/**
 * Check whether the D-IN RMT transmit hardware initialized successfully
 * @return true if D-IN responses can be driven, false otherwise
 */
bool lc72130_bus_din_hardware_ready(void);

/**
 * Get the last D-IN RMT initialization error, or ESP_OK if none occurred
 */
esp_err_t lc72130_bus_get_din_init_error(void);

/**
 * Force D-IN HIGH or LOW, overriding the automatic 100 ms status response,
 * or return it to automatic (AUTO) operation.
 */
esp_err_t lc72130_bus_set_din_override(din_override_t mode);

din_override_t lc72130_bus_get_din_override(void);
const char *lc72130_bus_din_override_name(din_override_t mode);

/**
 * Force D-IN LOW for pulse_ms milliseconds, then automatically return it to
 * HIGH. Used by the "Normal HIGH -> Pulse LOW" manual test control.
 */
esp_err_t lc72130_bus_pulse_din_low(uint32_t pulse_ms);

/**
 * Get diagnostics: number of captured frames
 */
uint32_t lc72130_bus_get_frame_count(void);

/**
 * Get diagnostics: number of malformed frames
 */
uint32_t lc72130_bus_get_error_count(void);

/**
 * Get diagnostics: number of read requests detected
 */
uint32_t lc72130_bus_get_read_count(void);

/**
 * Hard-reset the capture state (clear buffers, restart)
 * Use when recovering from an error or transaction timeout
 */
void lc72130_bus_reset(void);

#endif // LC72130_BUS_H

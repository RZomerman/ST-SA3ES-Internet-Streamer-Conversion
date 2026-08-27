/**
 * @file lc72130_emulator.h
 * @brief Internal state machine and response generation for LC72130 emulation
 *
 * Maintains:
 * - Current requested frequency
 * - Previous frequency
 * - Current divider and control byte
 * - PLL lock state
 * - Tuner ready state
 * - Input port states (configurable)
 * - IF counter/status
 * - Transaction and error counters
 */

#ifndef LC72130_EMULATOR_H
#define LC72130_EMULATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "lc72130_protocol.h"

/* ============================================================================
 * Emulated LC72130 State
 * ============================================================================ */

typedef struct {
    /* Frequency state */
    float current_frequency_mhz;
    float previous_frequency_mhz;
    uint16_t current_divider;
    uint16_t previous_divider;
    uint8_t current_control_byte;
    uint8_t previous_control_byte;
    
    /* Status bits */
    bool pll_locked;            /**< PLL lock status (read/status response bit) */
    bool tuner_ready;           /**< Tuner ready status */
    bool error_flag;            /**< Error status */
    
    /* Input ports (Sony controller may query via read transactions) */
    uint8_t input_port_1;       /**< GPIO input state (if applicable) */
    uint8_t input_port_2;       /**< GPIO input state (if applicable) */
    
    /* IF counter / additional status */
    uint16_t if_counter_result; /**< IF counter value (if applicable) */
    
    /* Counters */
    uint32_t transaction_count;
    uint32_t malformed_frame_count;
    uint32_t read_request_count;
    
    /* Last known D-IN response sent */
    uint8_t last_din_response_sent;
} lc72130_state_t;

/* ============================================================================
 * D-IN Response Byte Format (Status)
 * ============================================================================
 * Per LC72130 CCB protocol:
 *   Bit 7: Test / Reserved (0)
 *   Bit 6: PLL Lock Status (1 = locked, 0 = not locked)
 *   Bit 5: Tuner Ready (1 = ready, 0 = not ready)
 *   Bit 4: Input Port 2 state
 *   Bit 3: Input Port 1 state
 *   Bit 2: IF-Counter / Status (configurable meaning)
 *   Bit 1: Reserved (0)
 *   Bit 0: Reserved (0)
 */

#define DIN_RESPONSE_PLL_LOCK_BIT   6
#define DIN_RESPONSE_TUNER_READY_BIT 5
#define DIN_RESPONSE_INPUT_PORT2_BIT 4
#define DIN_RESPONSE_INPUT_PORT1_BIT 3
#define DIN_RESPONSE_IF_COUNTER_BIT  2

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Initialize the emulator state machine
 * @return ESP_OK on success
 */
esp_err_t lc72130_emulator_init(void);

/**
 * Get the current emulator state
 * @return Pointer to the state structure (should not be modified directly)
 */
const lc72130_state_t *lc72130_emulator_get_state(void);

/**
 * Process a decoded transaction: update internal state
 * @param transaction The decoded transaction from lc72130_protocol
 * @return ESP_OK on success, error if validation fails
 */
esp_err_t lc72130_emulator_process_transaction(const lc72130_transaction_t *transaction);

/**
 * Generate a D-IN response byte for a read/status request
 * Encodes current PLL lock, tuner ready, and other status bits
 * @param response_out Pointer to output byte (filled on return)
 * @return ESP_OK on success
 */
esp_err_t lc72130_emulator_generate_din_response(uint8_t *response_out);

/**
 * Set PLL lock status (typically set true after a frequency write)
 * @param locked true for locked, false for unlocked
 */
void lc72130_emulator_set_pll_locked(bool locked);

/**
 * Set tuner ready status
 * @param ready true for ready, false for not ready
 */
void lc72130_emulator_set_tuner_ready(bool ready);

/**
 * Set error flag (typically false; set true on protocol error)
 * @param error true if error state, false for normal
 */
void lc72130_emulator_set_error_flag(bool error);

/**
 * Set input port states (typically 0; may be used by Sony for feature detection)
 * @param port1 Port 1 input bit
 * @param port2 Port 2 input bit
 */
void lc72130_emulator_set_input_ports(uint8_t port1, uint8_t port2);

/**
 * Set IF counter result value (for read response if applicable)
 * @param value Counter value
 */
void lc72130_emulator_set_if_counter(uint16_t value);

/**
 * Get transaction count
 */
uint32_t lc72130_emulator_get_transaction_count(void);

/**
 * Get malformed frame count
 */
uint32_t lc72130_emulator_get_malformed_count(void);

/**
 * Get read request count
 */
uint32_t lc72130_emulator_get_read_count(void);

/**
 * Reset the emulator state (clear counters and frequency)
 * Useful when starting a new tuning session
 */
void lc72130_emulator_reset(void);

#endif // LC72130_EMULATOR_H

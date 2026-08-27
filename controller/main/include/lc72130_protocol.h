/**
 * @file lc72130_protocol.h
 * @brief Sanyo CCB protocol framing and transaction classification
 *
 * Decodes Sanyo CCB frames and classifies them as:
 * - Frequency write (FM programming)
 * - Control/configuration write
 * - Read/status request
 * - Unknown
 */

#ifndef LC72130_PROTOCOL_H
#define LC72130_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ============================================================================
 * Transaction Types
 * ============================================================================ */

typedef enum {
    TRANS_TYPE_UNKNOWN,
    TRANS_TYPE_WRITE_FREQUENCY,     /**< FM frequency programming */
    TRANS_TYPE_WRITE_CONTROL,       /**< Control/configuration */
    TRANS_TYPE_WRITE_IF_COUNTER,    /**< IF counter setting */
    TRANS_TYPE_READ_STATUS,         /**< Read status/register */
    TRANS_TYPE_MALFORMED,           /**< Incomplete or invalid */
} transaction_type_t;

/* ============================================================================
 * Transaction Data Structure
 * ============================================================================ */

typedef struct {
    transaction_type_t type;
    uint8_t byte0, byte1, byte2;
    
    /* Decoded fields for frequency writes */
    uint32_t divider;           /**< Frequency divider N */
    float frequency_mhz;        /**< Decoded FM frequency */
    uint8_t control_byte;       /**< Byte 2 (control/mode) */
    
    /* Transaction metadata */
    uint32_t transaction_id;    /**< Sequential counter */
    uint64_t timestamp_us;      /**< Capture time */
    bool is_read;               /**< True if read/status, false if write */
} lc72130_transaction_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Initialize the protocol decoder
 * @return ESP_OK on success
 */
esp_err_t lc72130_protocol_init(void);

/**
 * Decode a 3-byte frame into a transaction
 * @param byte0, byte1, byte2 The three bytes received
 * @param timestamp_us When the frame was captured
 * @param is_read True if this was a read request, false if write
 * @param transaction Output transaction (filled on success)
 * @return ESP_OK on successful decode, error otherwise
 */
esp_err_t lc72130_protocol_decode(uint8_t byte0, uint8_t byte1, uint8_t byte2,
                                   uint64_t timestamp_us, bool is_read,
                                   lc72130_transaction_t *transaction);

/**
 * Check if a transaction contains a valid FM frequency
 * @param trans The transaction
 * @return true if frequency is valid and decodable
 */
bool lc72130_protocol_is_frequency_frame(const lc72130_transaction_t *trans);

/**
 * Check if a transaction is a read/status request
 * @param trans The transaction
 * @return true if a response on D-IN is expected
 */
bool lc72130_protocol_is_read_frame(const lc72130_transaction_t *trans);

/**
 * Get human-readable description of a transaction type
 * @param type The type enum
 * @return String description
 */
const char *lc72130_protocol_type_str(transaction_type_t type);

/**
 * Get the next sequential transaction ID
 */
uint32_t lc72130_protocol_get_next_transaction_id(void);

#endif // LC72130_PROTOCOL_H

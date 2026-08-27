/**
 * @file lc72130_protocol.c
 * @brief Sanyo CCB protocol framing and transaction classification
 */

#include "lc72130_protocol.h"
#include "lc72130_decoder.h"
#include "esp_log.h"

static const char *TAG = "LC72130_PROTOCOL";

static uint32_t g_transaction_counter = 0;

/* ============================================================================
 * Transaction Classification
 * ============================================================================ */

static transaction_type_t classify_transaction(uint8_t byte0, uint8_t byte1, uint8_t byte2)
{
    /* Check if it looks like a frequency frame (FM programming) */
    if (lc72130_decoder_is_plausible_frequency_frame(byte0, byte1, byte2)) {
        if (byte2 == 0x2A) {
            return TRANS_TYPE_WRITE_FREQUENCY;
        }
    }
    
    /* Check for known control bytes */
    if (lc72130_decoder_is_known_control_byte(byte2)) {
        if (byte2 == 0x2A) {
            return TRANS_TYPE_WRITE_FREQUENCY;
        } else if (byte2 == 0xE3 || byte2 == 0x4C || byte2 == 0x88) {
            return TRANS_TYPE_WRITE_CONTROL;
        }
    }
    
    /* If we can't classify it, mark as unknown */
    return TRANS_TYPE_UNKNOWN;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

esp_err_t lc72130_protocol_init(void)
{
    ESP_LOGI(TAG, "LC72130 protocol decoder initialized");
    return ESP_OK;
}

esp_err_t lc72130_protocol_decode(uint8_t byte0, uint8_t byte1, uint8_t byte2,
                                   uint64_t timestamp_us, bool is_read,
                                   lc72130_transaction_t *transaction)
{
    if (!transaction) return ESP_ERR_INVALID_ARG;
    
    transaction->type = classify_transaction(byte0, byte1, byte2);
    transaction->byte0 = byte0;
    transaction->byte1 = byte1;
    transaction->byte2 = byte2;
    transaction->timestamp_us = timestamp_us;
    transaction->is_read = is_read;
    transaction->transaction_id = g_transaction_counter++;
    
    /* Extract frequency if this is a frequency frame */
    if (transaction->type == TRANS_TYPE_WRITE_FREQUENCY) {
        uint16_t divider;
        uint8_t control;
        
        if (lc72130_decoder_frame_to_divider(byte0, byte1, byte2, &divider, &control)) {
            transaction->divider = divider;
            transaction->control_byte = control;
            
            float freq;
            if (lc72130_decoder_divider_to_frequency(divider, &freq)) {
                transaction->frequency_mhz = freq;
            } else {
                ESP_LOGW(TAG, "Failed to convert divider 0x%04X to frequency", divider);
                return ESP_ERR_INVALID_ARG;
            }
        } else {
            ESP_LOGW(TAG, "Failed to decode frequency frame: %02X %02X %02X", byte0, byte1, byte2);
            return ESP_ERR_INVALID_ARG;
        }
    } else {
        transaction->divider = 0;
        transaction->frequency_mhz = 0.0f;
        transaction->control_byte = byte2;
    }
    
    return ESP_OK;
}

bool lc72130_protocol_is_frequency_frame(const lc72130_transaction_t *trans)
{
    if (!trans) return false;
    return trans->type == TRANS_TYPE_WRITE_FREQUENCY;
}

bool lc72130_protocol_is_read_frame(const lc72130_transaction_t *trans)
{
    if (!trans) return false;
    return trans->is_read;
}

const char *lc72130_protocol_type_str(transaction_type_t type)
{
    switch (type) {
        case TRANS_TYPE_UNKNOWN:           return "Unknown";
        case TRANS_TYPE_WRITE_FREQUENCY:   return "Write Frequency";
        case TRANS_TYPE_WRITE_CONTROL:     return "Write Control";
        case TRANS_TYPE_WRITE_IF_COUNTER:  return "Write IF Counter";
        case TRANS_TYPE_READ_STATUS:       return "Read Status";
        case TRANS_TYPE_MALFORMED:         return "Malformed";
        default:                           return "???";
    }
}

uint32_t lc72130_protocol_get_next_transaction_id(void)
{
    return g_transaction_counter;
}

/**
 * @file lc72130_decoder.c
 * @brief Frequency decoding and bit-reversal implementation
 */

#include "lc72130_decoder.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "LC72130_DECODER";

/* ============================================================================
 * Bit Reversal
 * ============================================================================ */

uint8_t lc72130_decoder_reverse_bits_8(uint8_t x)
{
    x &= 0xFF;
    x = ((x & 0xF0) >> 4) | ((x & 0x0F) << 4);
    x = ((x & 0xCC) >> 2) | ((x & 0x33) << 2);
    x = ((x & 0xAA) >> 1) | ((x & 0x55) << 1);
    return x;
}

uint16_t lc72130_decoder_reverse_bits_16(uint16_t x)
{
    uint16_t lo = lc72130_decoder_reverse_bits_8(x & 0xFF);
    uint16_t hi = lc72130_decoder_reverse_bits_8((x >> 8) & 0xFF);
    return (lo << 8) | hi;
}

/* ============================================================================
 * Frequency Conversion
 * ============================================================================ */

bool lc72130_decoder_divider_to_frequency(uint16_t divider, float *frequency_out)
{
    if (!frequency_out) return false;
    
    if (divider < LC72130_MIN_DIVIDER || divider > LC72130_MAX_DIVIDER) {
        return false;
    }
    
    *frequency_out = (divider * LC72130_DIVIDER_SCALE) - LC72130_FREQUENCY_OFFSET;
    return true;
}

bool lc72130_decoder_frequency_to_divider(float frequency_mhz, uint16_t *divider_out)
{
    if (!divider_out) return false;
    
    if (frequency_mhz < LC72130_MIN_FREQUENCY_MHZ || frequency_mhz > LC72130_MAX_FREQUENCY_MHZ) {
        return false;
    }
    
    float n = (frequency_mhz + LC72130_FREQUENCY_OFFSET) / LC72130_DIVIDER_SCALE;
    *divider_out = (uint16_t)roundf(n);
    return true;
}

bool lc72130_decoder_frame_to_divider(uint8_t byte0, uint8_t byte1, uint8_t byte2,
                                       uint16_t *divider_out, uint8_t *control_out)
{
    if (!divider_out || !control_out) return false;
    
    /* LSB-first: byte0 is low, byte1 is high */
    *divider_out = byte0 | (byte1 << 8);
    *control_out = byte2;
    
    /* Validate divider is plausible for FM band */
    if (*divider_out < 0x0700 || *divider_out > 0x0B00) {
        ESP_LOGW(TAG, "Divider 0x%04X outside typical FM range", *divider_out);
        return false;
    }
    
    return true;
}

bool lc72130_decoder_divider_to_frame(uint16_t divider, uint8_t control,
                                       uint8_t *byte0_out, uint8_t *byte1_out, uint8_t *byte2_out)
{
    if (!byte0_out || !byte1_out || !byte2_out) return false;
    
    if (divider < LC72130_MIN_DIVIDER || divider > LC72130_MAX_DIVIDER) {
        return false;
    }
    
    *byte0_out = divider & 0xFF;
    *byte1_out = (divider >> 8) & 0xFF;
    *byte2_out = control;
    
    return true;
}

/* ============================================================================
 * Frame Validation
 * ============================================================================ */

bool lc72130_decoder_is_plausible_frequency_frame(uint8_t byte0, uint8_t byte1, uint8_t byte2)
{
    uint16_t divider = byte0 | (byte1 << 8);
    
    /* Typical FM frequency divider range */
    if (divider < 0x0700 || divider > 0x0B00) {
        return false;
    }
    
    /* Common control bytes for FM */
    if (byte2 != 0x2A && byte2 != 0x54) {
        /* 0x2A seen in Sony captures, 0x54 in user memory notes */
        /* Allow others but log as unusual */
    }
    
    return true;
}

bool lc72130_decoder_is_known_control_byte(uint8_t control)
{
    /* Known control bytes from protocol captures */
    switch (control) {
        case 0x2A:  /* FM frequency write (Sony) */
        case 0x54:  /* Alternative frequency write (sigrok captures) */
        case 0xE3:  /* Control frame (user memory) */
        case 0x4C:  /* Control frame (user memory) */
        case 0x88:  /* Control frame (user memory) */
            return true;
        default:
            return false;
    }
}

/* ============================================================================
 * Tuning Sequence Detection
 * ============================================================================ */

static struct {
    float last_frequency;
    float initial_frequency;
    bool sequence_active;
} g_tune_detector = {0};

bool lc72130_decoder_detect_tune_pattern(float frequency_mhz, tune_pattern_t *pattern_out)
{
    if (!pattern_out) return false;
    
    *pattern_out = TUNE_PATTERN_NONE;
    
    /* First frequency in a sequence */
    if (!g_tune_detector.sequence_active) {
        g_tune_detector.sequence_active = true;
        g_tune_detector.initial_frequency = frequency_mhz;
        g_tune_detector.last_frequency = frequency_mhz;
        *pattern_out = TUNE_PATTERN_INITIAL_SELECT;
        return true;
    }
    
    float delta = frequency_mhz - g_tune_detector.last_frequency;
    float from_initial = frequency_mhz - g_tune_detector.initial_frequency;
    
    /* Return to initial */
    if (fabsf(from_initial) < 0.01f && fabsf(delta) > 0.01f) {
        *pattern_out = TUNE_PATTERN_RETURN;
        g_tune_detector.sequence_active = false;
        return true;
    }
    
    /* Positive offsets */
    if (delta > 0) {
        if (delta < 0.08f) {
            *pattern_out = TUNE_PATTERN_SMALL_OFFSET_POS;
        } else {
            *pattern_out = TUNE_PATTERN_LARGE_OFFSET_POS;
        }
        g_tune_detector.last_frequency = frequency_mhz;
        return true;
    }
    
    /* Negative offsets */
    if (delta < -0.01f) {
        *pattern_out = TUNE_PATTERN_OFFSET_NEG;
        g_tune_detector.last_frequency = frequency_mhz;
        return true;
    }
    
    g_tune_detector.last_frequency = frequency_mhz;
    return false;
}

const char *lc72130_decoder_tune_pattern_str(tune_pattern_t pattern)
{
    switch (pattern) {
        case TUNE_PATTERN_NONE:             return "None";
        case TUNE_PATTERN_INITIAL_SELECT:   return "Initial Select";
        case TUNE_PATTERN_SMALL_OFFSET_POS: return "Small Positive Offset";
        case TUNE_PATTERN_LARGE_OFFSET_POS: return "Large Positive Offset";
        case TUNE_PATTERN_OFFSET_NEG:       return "Negative Offset";
        case TUNE_PATTERN_RETURN:           return "Return to Initial";
        default:                            return "Unknown";
    }
}

void lc72130_decoder_reset_tune_sequence(void)
{
    g_tune_detector.sequence_active = false;
    g_tune_detector.last_frequency = 0;
    g_tune_detector.initial_frequency = 0;
}

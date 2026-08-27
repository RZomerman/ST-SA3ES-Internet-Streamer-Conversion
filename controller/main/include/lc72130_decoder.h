/**
 * @file lc72130_decoder.h
 * @brief High-level frame decoding and frequency calculation
 *
 * Provides:
 * - Bit-reversal utilities
 * - Frequency divider to MHz conversion
 * - Frame validation and parsing
 * - Tuning sequence detection (startup patterns)
 */

#ifndef LC72130_DECODER_H
#define LC72130_DECODER_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Constants & Formulas
 * ============================================================================ */

/**
 * FM Frequency Calculation
 * 
 * divider = (byte1 << 8) | byte0  (LSB-first)
 * f_MHz = divider × 0.05 - 10.70
 * 
 * Inverse:
 * divider = round((f_MHz + 10.70) / 0.05)
 * 
 * Examples:
 *  - 105.90 MHz: divider=0x091C, bytes=1C 09 2A
 *  - 106.00 MHz: divider=0x091E, bytes=1E 09 2A
 *  - 106.05 MHz: divider=0x091F, bytes=1F 09 2A
 *  - 106.20 MHz: divider=0x0922, bytes=22 09 2A
 *  - 88.00 MHz:  divider=0x07B6, bytes=B6 07 2A
 */

#define LC72130_DIVIDER_SCALE       0.05f
#define LC72130_FREQUENCY_OFFSET    10.70f
#define LC72130_MIN_DIVIDER         0x0000
#define LC72130_MAX_DIVIDER         0xFFFF
#define LC72130_MIN_FREQUENCY_MHZ   10.70f   /**< @10000 */
#define LC72130_MAX_FREQUENCY_MHZ   (0xFFFF * LC72130_DIVIDER_SCALE - LC72130_FREQUENCY_OFFSET)

/* ============================================================================
 * Bit Reversal
 * ============================================================================ */

/**
 * Reverse the bits of an 8-bit value
 * LSB → MSB, MSB → LSB
 * @param x The input byte
 * @return Bit-reversed byte
 */
uint8_t lc72130_decoder_reverse_bits_8(uint8_t x);

/**
 * Reverse the bits of a 16-bit value
 * @param x The input word
 * @return Bit-reversed word
 */
uint16_t lc72130_decoder_reverse_bits_16(uint16_t x);

/* ============================================================================
 * Frequency Decoding
 * ============================================================================ */

/**
 * Decode divider + control byte to frequency in MHz
 * @param divider The 16-bit frequency divider (from bytes 0-1)
 * @param frequency_out Pointer to output frequency (filled on success)
 * @return true if divider is in valid range
 */
bool lc72130_decoder_divider_to_frequency(uint16_t divider, float *frequency_out);

/**
 * Encode frequency in MHz to divider
 * @param frequency_mhz The frequency to encode
 * @param divider_out Pointer to output divider (filled on success)
 * @return true if frequency is in valid range
 */
bool lc72130_decoder_frequency_to_divider(float frequency_mhz, uint16_t *divider_out);

/**
 * Decode a 3-byte frame to divider and control values
 * Assumes LSB-first bit order per byte
 * @param byte0, byte1, byte2 The frame bytes
 * @param divider_out Decoded divider (filled on success)
 * @param control_out Control byte (typically 0x2A for FM)
 * @return true if valid
 */
bool lc72130_decoder_frame_to_divider(uint8_t byte0, uint8_t byte1, uint8_t byte2,
                                       uint16_t *divider_out, uint8_t *control_out);

/**
 * Encode divider + control to a 3-byte frame
 * @param divider The frequency divider
 * @param control The control byte (e.g., 0x2A)
 * @param byte0_out, byte1_out, byte2_out Frame bytes (filled on success)
 * @return true if valid
 */
bool lc72130_decoder_divider_to_frame(uint16_t divider, uint8_t control,
                                       uint8_t *byte0_out, uint8_t *byte1_out, uint8_t *byte2_out);

/* ============================================================================
 * Frame Validation
 * ============================================================================ */

/**
 * Check if a frame looks like a valid FM frequency write
 * (Control byte typically 0x2A, divider in plausible range)
 * @param byte0, byte1, byte2 The frame bytes
 * @return true if plausible
 */
bool lc72130_decoder_is_plausible_frequency_frame(uint8_t byte0, uint8_t byte1, uint8_t byte2);

/**
 * Check if a frame control byte is known (e.g., 0x2A for FM)
 * @param control The control byte
 * @return true if recognized
 */
bool lc72130_decoder_is_known_control_byte(uint8_t control);

/* ============================================================================
 * Tuning Sequence Detection
 * ============================================================================ */

/**
 * Detect tuning sequence patterns:
 * - Initial frequency selection
 * - Small positive offset
 * - Larger positive offset
 * - Negative offset
 * - Return to selected
 *
 * This is used for non-invasive monitoring of tuning behavior
 * and is NOT labeled as AFC or fine-tuning in output.
 */

typedef enum {
    TUNE_PATTERN_NONE,
    TUNE_PATTERN_INITIAL_SELECT,    /**< First frequency in a sequence */
    TUNE_PATTERN_SMALL_OFFSET_POS,  /**< +0.05 MHz or similar */
    TUNE_PATTERN_LARGE_OFFSET_POS,  /**< +0.10 MHz or more */
    TUNE_PATTERN_OFFSET_NEG,        /**< Frequency decrease */
    TUNE_PATTERN_RETURN,            /**< Return to initial */
} tune_pattern_t;

/**
 * Analyze a new frequency for tuning patterns
 * Must be called with each new frequency write in sequence
 * @param frequency_mhz The frequency from the latest frame
 * @param pattern_out Pattern detected (filled on return)
 * @return true if a pattern was detected
 */
bool lc72130_decoder_detect_tune_pattern(float frequency_mhz, tune_pattern_t *pattern_out);

/**
 * Get human-readable name for a tuning pattern
 * @param pattern The pattern enum
 * @return String description
 */
const char *lc72130_decoder_tune_pattern_str(tune_pattern_t pattern);

/**
 * Reset tuning sequence detector (e.g., when a large gap is detected)
 */
void lc72130_decoder_reset_tune_sequence(void);

#endif // LC72130_DECODER_H

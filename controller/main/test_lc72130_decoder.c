/**
 * @file test_lc72130_decoder.c
 * @brief Unit tests for LC72130 decoder functions
 *
 * Compile as: gcc -o test_lc72130_decoder test_lc72130_decoder.c lc72130_decoder.c -lm
 * Run: ./test_lc72130_decoder
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

/* Minimal stubs for ESP-IDF functions (not needed for decoder tests) */
#define ESP_LOGI(tag, fmt, ...) printf("[INFO]  " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[WARN]  " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)

/* Include decoder header (will compile the .c file) */
#include "lc72130_decoder.h"

/* ============================================================================
 * Test Framework
 * ============================================================================ */

typedef struct {
    const char *name;
    bool passed;
} test_result_t;

static int g_test_count = 0;
static int g_test_passed = 0;
static test_result_t g_results[100];

static void assert_true(bool condition, const char *test_name, const char *message)
{
    g_test_count++;
    if (condition) {
        g_test_passed++;
        printf("  ✓ %s\n", message);
        g_results[g_test_count - 1].passed = true;
    } else {
        printf("  ✗ %s\n", message);
        g_results[g_test_count - 1].passed = false;
    }
    g_results[g_test_count - 1].name = test_name;
}

static void print_summary(void)
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("Test Summary: %d/%d passed\n", g_test_passed, g_test_count);
    printf("════════════════════════════════════════════════════════════════\n");
    
    if (g_test_passed == g_test_count) {
        printf("✓ All tests passed!\n");
    } else {
        printf("✗ Some tests failed:\n");
        for (int i = 0; i < g_test_count; i++) {
            if (!g_results[i].passed) {
                printf("  - %s\n", g_results[i].name);
            }
        }
    }
    printf("\n");
}

/* ============================================================================
 * Test Cases
 * ============================================================================ */

static void test_bit_reversal(void)
{
    printf("\nTest: Bit Reversal\n");
    
    assert_true(lc72130_decoder_reverse_bits_8(0x00) == 0x00, "bit_rev_8_00", "reverse(0x00) = 0x00");
    assert_true(lc72130_decoder_reverse_bits_8(0xFF) == 0xFF, "bit_rev_8_FF", "reverse(0xFF) = 0xFF");
    assert_true(lc72130_decoder_reverse_bits_8(0x01) == 0x80, "bit_rev_8_01", "reverse(0x01) = 0x80");
    assert_true(lc72130_decoder_reverse_bits_8(0x80) == 0x01, "bit_rev_8_80", "reverse(0x80) = 0x01");
    assert_true(lc72130_decoder_reverse_bits_8(0xAA) == 0x55, "bit_rev_8_AA", "reverse(0xAA) = 0x55");
}

static void test_frequency_decoding(void)
{
    printf("\nTest: Frequency Decoding\n");
    
    /* Test cases from Sony IC701 reverse engineering */
    struct {
        uint16_t divider;
        float expected_freq;
        const char *label;
    } test_cases[] = {
        {0x091C, 105.90f, "105.90 MHz"},
        {0x091E, 106.00f, "106.00 MHz"},
        {0x091F, 106.05f, "106.05 MHz"},
        {0x0922, 106.20f, "106.20 MHz"},
        {0x07B6, 88.00f,  "88.00 MHz"},
    };
    
    for (int i = 0; i < 5; i++) {
        float freq;
        bool result = lc72130_decoder_divider_to_frequency(test_cases[i].divider, &freq);
        
        char test_id[32];
        snprintf(test_id, sizeof(test_id), "freq_%04X", test_cases[i].divider);
        
        bool match = result && fabsf(freq - test_cases[i].expected_freq) < 0.01f;
        assert_true(match, test_id, test_cases[i].label);
    }
}

static void test_frame_to_divider(void)
{
    printf("\nTest: Frame to Divider Conversion\n");
    
    struct {
        uint8_t byte0, byte1, byte2;
        uint16_t expected_divider;
        float expected_freq;
        const char *label;
    } test_cases[] = {
        {0x1C, 0x09, 0x2A, 0x091C, 105.90f, "105.90 MHz (1C 09 2A)"},
        {0x1E, 0x09, 0x2A, 0x091E, 106.00f, "106.00 MHz (1E 09 2A)"},
        {0x1F, 0x09, 0x2A, 0x091F, 106.05f, "106.05 MHz (1F 09 2A)"},
        {0x22, 0x09, 0x2A, 0x0922, 106.20f, "106.20 MHz (22 09 2A)"},
        {0xB6, 0x07, 0x2A, 0x07B6, 88.00f,  "88.00 MHz (B6 07 2A)"},
    };
    
    for (int i = 0; i < 5; i++) {
        uint16_t divider;
        uint8_t control;
        bool result = lc72130_decoder_frame_to_divider(
            test_cases[i].byte0, test_cases[i].byte1, test_cases[i].byte2,
            &divider, &control
        );
        
        if (result) {
            float freq;
            lc72130_decoder_divider_to_frequency(divider, &freq);
            
            char test_id[32];
            snprintf(test_id, sizeof(test_id), "frame_%02X%02X%02X", 
                     test_cases[i].byte0, test_cases[i].byte1, test_cases[i].byte2);
            
            bool divider_match = (divider == test_cases[i].expected_divider);
            bool freq_match = fabsf(freq - test_cases[i].expected_freq) < 0.01f;
            bool control_match = (control == 0x2A);
            
            assert_true(divider_match && freq_match && control_match, 
                       test_id, test_cases[i].label);
        } else {
            printf("  ✗ %s (decode failed)\n", test_cases[i].label);
            g_test_count++;
        }
    }
}

static void test_frame_validation(void)
{
    printf("\nTest: Frame Validation\n");
    
    /* Valid FM frequency frames */
    assert_true(lc72130_decoder_is_plausible_frequency_frame(0x1C, 0x09, 0x2A),
               "valid_frame_1", "Valid FM frame (1C 09 2A)");
    
    /* Invalid: divider too low */
    assert_true(!lc72130_decoder_is_plausible_frequency_frame(0x00, 0x05, 0x2A),
               "invalid_frame_low", "Invalid FM frame (divider too low)");
    
    /* Invalid: divider too high */
    assert_true(!lc72130_decoder_is_plausible_frequency_frame(0x00, 0x0C, 0x2A),
               "invalid_frame_high", "Invalid FM frame (divider too high)");
    
    /* Known control bytes */
    assert_true(lc72130_decoder_is_known_control_byte(0x2A),
               "known_ctrl_2A", "Control byte 0x2A is known");
    assert_true(lc72130_decoder_is_known_control_byte(0x54),
               "known_ctrl_54", "Control byte 0x54 is known");
}

static void test_frequency_encoding(void)
{
    printf("\nTest: Frequency Encoding\n");
    
    struct {
        float freq;
        uint16_t expected_divider;
        const char *label;
    } test_cases[] = {
        {105.90f, 0x091C, "105.90 MHz"},
        {106.00f, 0x091E, "106.00 MHz"},
        {106.05f, 0x091F, "106.05 MHz"},
        {106.20f, 0x0922, "106.20 MHz"},
        {88.00f,  0x07B6, "88.00 MHz"},
    };
    
    for (int i = 0; i < 5; i++) {
        uint16_t divider;
        bool result = lc72130_decoder_frequency_to_divider(test_cases[i].freq, &divider);
        
        char test_id[32];
        snprintf(test_id, sizeof(test_id), "enc_%.2f", test_cases[i].freq);
        
        bool match = result && (divider == test_cases[i].expected_divider);
        assert_true(match, test_id, test_cases[i].label);
    }
}

static void test_roundtrip_frequency(void)
{
    printf("\nTest: Frequency Roundtrip (encode/decode)\n");
    
    float test_freqs[] = {88.00f, 95.90f, 106.00f, 107.90f};
    
    for (int i = 0; i < 4; i++) {
        uint16_t divider;
        float decoded_freq;
        
        bool encode_ok = lc72130_decoder_frequency_to_divider(test_freqs[i], &divider);
        bool decode_ok = lc72130_decoder_divider_to_frequency(divider, &decoded_freq);
        
        char test_id[32];
        snprintf(test_id, sizeof(test_id), "roundtrip_%.2f", test_freqs[i]);
        
        bool match = encode_ok && decode_ok && fabsf(decoded_freq - test_freqs[i]) < 0.01f;
        assert_true(match, test_id, "Roundtrip successful");
    }
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char *argv[])
{
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("LC72130 Decoder Unit Tests\n");
    printf("════════════════════════════════════════════════════════════════\n");
    
    test_bit_reversal();
    test_frequency_decoding();
    test_frame_to_divider();
    test_frame_validation();
    test_frequency_encoding();
    test_roundtrip_frequency();
    
    print_summary();
    
    return (g_test_passed == g_test_count) ? 0 : 1;
}

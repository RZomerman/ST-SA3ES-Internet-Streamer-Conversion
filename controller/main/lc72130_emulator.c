/**
 * @file lc72130_emulator.c
 * @brief LC72130 emulator state machine and response generation
 */

#include <inttypes.h>
#include "lc72130_emulator.h"
#include "lc72130_decoder.h"
#include "lc72130_bus.h"
#include "event_log.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "radio_link.h"

static const char *TAG = "LC72130_EMULATOR";

#define DIN_RESPONSE_DELAY_US 100000  /* 100 ms after each frequency write, per LC72130 spec intent */

static esp_timer_handle_t g_din_response_timer;

static void din_response_timer_callback(void *arg)
{
    uint8_t response;
    esp_err_t err = lc72130_emulator_generate_din_response(&response);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate D-IN response: %s", esp_err_to_name(err));
        return;
    }

    err = lc72130_bus_drive_din_response(response, 8);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not drive D-IN response: %s", esp_err_to_name(err));
    }
}

/* Restart the 100 ms delay on each new frequency write so only the most
 * recent write in a burst (e.g. an AFC sequence) produces a response. */
static void schedule_din_response(void)
{
    if (!g_din_response_timer) {
        return;
    }
    esp_timer_stop(g_din_response_timer);  /* no-op if not currently running */
    esp_err_t err = esp_timer_start_once(g_din_response_timer, DIN_RESPONSE_DELAY_US);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to schedule D-IN response: %s", esp_err_to_name(err));
    }
}

/* ============================================================================
 * Global Emulated State
 * ============================================================================ */

static lc72130_state_t g_state = {
    .current_frequency_mhz = 0.0f,
    .previous_frequency_mhz = 0.0f,
    .current_divider = 0,
    .previous_divider = 0,
    .current_control_byte = 0,
    .previous_control_byte = 0,
    .pll_locked = false,
    .tuner_ready = true,
    .error_flag = false,
    .input_port_1 = 0,
    .input_port_2 = 0,
    .if_counter_result = 0,
    .transaction_count = 0,
    .malformed_frame_count = 0,
    .read_request_count = 0,
    .last_din_response_sent = 0,
};

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

esp_err_t lc72130_emulator_init(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = &din_response_timer_callback,
        .name = "din_response",
    };
    esp_err_t err = esp_timer_create(&timer_args, &g_din_response_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create D-IN response timer: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "LC72130 emulator initialized");
    ESP_LOGI(TAG, "Initial state: PLL unlocked, Tuner ready");
    return ESP_OK;
}

const lc72130_state_t *lc72130_emulator_get_state(void)
{
    return &g_state;
}

esp_err_t lc72130_emulator_process_transaction(const lc72130_transaction_t *transaction)
{
    if (!transaction) return ESP_ERR_INVALID_ARG;
    
    g_state.transaction_count++;
    
    if (transaction->type == TRANS_TYPE_WRITE_FREQUENCY) {
        /* Update frequency state */
        g_state.previous_frequency_mhz = g_state.current_frequency_mhz;
        g_state.previous_divider = g_state.current_divider;
        g_state.previous_control_byte = g_state.current_control_byte;
        
        g_state.current_frequency_mhz = transaction->frequency_mhz;
        g_state.current_divider = transaction->divider;
        g_state.current_control_byte = transaction->control_byte;
        
        /* Lock PLL after a frequency write */
        g_state.pll_locked = true;
        
        /* Detect tuning patterns */
        tune_pattern_t pattern;
        if (lc72130_decoder_detect_tune_pattern(transaction->frequency_mhz, &pattern)) {
            ESP_LOGI(TAG, "Frequency pattern: %s (%.2f MHz)",
                     lc72130_decoder_tune_pattern_str(pattern), transaction->frequency_mhz);
        }
        
        /* Log the transaction */
        event_log_frame_received(transaction->byte0, transaction->byte1, transaction->byte2);
        
        ESP_LOGI(TAG, "Frequency set: %.2f MHz (divider=0x%04" PRIx32 " control=0x%02X)",
                 transaction->frequency_mhz, transaction->divider, transaction->control_byte);

        esp_err_t radio_err = radio_link_send_frequency(transaction->frequency_mhz);
        if (radio_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to forward frequency to Radio ESP: %s",
                     esp_err_to_name(radio_err));
        }

        schedule_din_response();
    } else if (transaction->is_read) {
        g_state.read_request_count++;
        event_log_read_request();
        ESP_LOGI(TAG, "Read/Status request #%" PRIu32, g_state.read_request_count);
    } else {
        ESP_LOGI(TAG, "Control frame: %02X %02X %02X",
                 transaction->byte0, transaction->byte1, transaction->byte2);
        event_log_frame_received(transaction->byte0, transaction->byte1, transaction->byte2);
    }
    
    return ESP_OK;
}

esp_err_t lc72130_emulator_generate_din_response(uint8_t *response_out)
{
    if (!response_out) return ESP_ERR_INVALID_ARG;
    
    uint8_t response = 0;
    
    /* Encode status bits */
    if (g_state.pll_locked) {
        response |= (1 << DIN_RESPONSE_PLL_LOCK_BIT);
    }
    
    if (g_state.tuner_ready) {
        response |= (1 << DIN_RESPONSE_TUNER_READY_BIT);
    }
    
    if (g_state.input_port_1) {
        response |= (1 << DIN_RESPONSE_INPUT_PORT1_BIT);
    }
    
    if (g_state.input_port_2) {
        response |= (1 << DIN_RESPONSE_INPUT_PORT2_BIT);
    }
    
    /* IF counter / status bit (configurable) */
    if (g_state.if_counter_result & 0x01) {
        response |= (1 << DIN_RESPONSE_IF_COUNTER_BIT);
    }
    
    g_state.last_din_response_sent = response;
    *response_out = response;
    
    event_log_din_response(response);
    ESP_LOGD(TAG, "Generated D-IN response: 0x%02X", response);
    
    return ESP_OK;
}

void lc72130_emulator_set_pll_locked(bool locked)
{
    if (g_state.pll_locked != locked) {
        event_log_state_update(0, g_state.pll_locked, locked);
        ESP_LOGI(TAG, "PLL lock: %s", locked ? "LOCKED" : "UNLOCKED");
    }
    g_state.pll_locked = locked;
}

void lc72130_emulator_set_tuner_ready(bool ready)
{
    if (g_state.tuner_ready != ready) {
        event_log_state_update(1, g_state.tuner_ready, ready);
        ESP_LOGI(TAG, "Tuner ready: %s", ready ? "READY" : "NOT READY");
    }
    g_state.tuner_ready = ready;
}

void lc72130_emulator_set_error_flag(bool error)
{
    if (g_state.error_flag != error) {
        event_log_state_update(2, g_state.error_flag, error);
        ESP_LOGI(TAG, "Error flag: %s", error ? "SET" : "CLEAR");
    }
    g_state.error_flag = error;
}

void lc72130_emulator_set_input_ports(uint8_t port1, uint8_t port2)
{
    if (g_state.input_port_1 != port1) {
        event_log_state_update(3, g_state.input_port_1, port1);
    }
    if (g_state.input_port_2 != port2) {
        event_log_state_update(4, g_state.input_port_2, port2);
    }
    g_state.input_port_1 = port1;
    g_state.input_port_2 = port2;
}

void lc72130_emulator_set_if_counter(uint16_t value)
{
    if (g_state.if_counter_result != value) {
        event_log_state_update(5, g_state.if_counter_result, value);
    }
    g_state.if_counter_result = value;
}

uint32_t lc72130_emulator_get_transaction_count(void)
{
    return g_state.transaction_count;
}

uint32_t lc72130_emulator_get_malformed_count(void)
{
    return g_state.malformed_frame_count;
}

uint32_t lc72130_emulator_get_read_count(void)
{
    return g_state.read_request_count;
}

void lc72130_emulator_reset(void)
{
    g_state.current_frequency_mhz = 0.0f;
    g_state.previous_frequency_mhz = 0.0f;
    g_state.current_divider = 0;
    g_state.previous_divider = 0;
    g_state.pll_locked = false;
    g_state.transaction_count = 0;
    g_state.malformed_frame_count = 0;
    g_state.read_request_count = 0;
    
    lc72130_decoder_reset_tune_sequence();
    
    ESP_LOGI(TAG, "Emulator state reset");
}

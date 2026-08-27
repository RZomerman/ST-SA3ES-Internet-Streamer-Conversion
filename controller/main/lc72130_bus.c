/**
 * @file lc72130_bus.c
 * @brief Low-level bus capture and D-IN transmission implementation
 */

#include "lc72130_bus.h"
#include "config.h"
#include "event_log.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "LC72130_BUS";

/* ============================================================================
 * Ring Buffer for Bit Capture
 * ============================================================================ */

typedef struct {
    uint32_t timestamp_us;
    uint8_t bit_value;
} bit_sample_t;

typedef struct {
    bit_sample_t samples[MAX_FRAME_BITS];
    uint32_t count;
    uint64_t ce_rise_time;
    uint64_t clk_rise_time;
    uint32_t clk_edge_count;
} frame_buffer_t;

/* Global state */
static frame_buffer_t g_frame_buffer = {0};
static QueueHandle_t g_frame_queue = NULL;
static volatile uint32_t g_frame_count = 0;
static volatile uint32_t g_error_count = 0;
static volatile uint32_t g_read_count = 0;
static volatile uint32_t g_ce_state = 0;
static volatile uint32_t g_clk_state = 0;
static volatile uint32_t g_data_state = 0;
static volatile uint32_t g_din_state = 0;
static volatile uint64_t g_ce_rising_edge_time = 0;
static volatile uint64_t g_ce_falling_edge_time = 0;

/* RMT handle for D-IN output */
static rmt_channel_handle_t g_rmt_chan_dout = NULL;
static rmt_encoder_handle_t g_rmt_encoder_dout = NULL;
static esp_err_t g_din_init_error = ESP_OK;
static din_override_t g_din_override = DIN_OVERRIDE_AUTO;

/* ============================================================================
 * ISR Handlers
 * ============================================================================ */

static void IRAM_ATTR gpio_ce_isr(void *arg)
{
    uint32_t level = gpio_get_level(GPIO_CE_INPUT);
    uint64_t now_us = esp_timer_get_time();
    
    if (level) {
        /* CE rising edge - start of transaction */
        g_ce_rising_edge_time = now_us;
        g_frame_buffer.ce_rise_time = now_us;
        g_frame_buffer.count = 0;
        g_frame_buffer.clk_edge_count = 0;
    } else {
        /* CE falling edge - end of transaction */
        g_ce_falling_edge_time = now_us;
        captured_frame_t frame = {0};  /* Initialize frame early */
        
        /* Only accept frames with expected bit count */
        if (g_frame_buffer.count == MAX_FRAME_BITS) {
            frame.byte0 = 0;
            frame.byte1 = 0;
            frame.byte2 = 0;
            frame.bit_count = g_frame_buffer.count;
            frame.status = FRAME_VALID;
            frame.capture_time_us = now_us;
            frame.ce_rise_time_us = g_frame_buffer.ce_rise_time;
            frame.ce_fall_time_us = now_us;
            
            /* Extract bytes from bit samples (LSB-first) */
            uint8_t bytes[3] = {0, 0, 0};
            for (uint32_t i = 0; i < MAX_FRAME_BITS && i < g_frame_buffer.count; i++) {
                uint8_t byte_idx = i / 8;
                uint8_t bit_idx = i % 8;
                if (g_frame_buffer.samples[i].bit_value) {
                    bytes[byte_idx] |= (1 << bit_idx);
                }
            }
            
            frame.byte0 = bytes[0];
            frame.byte1 = bytes[1];
            frame.byte2 = bytes[2];
            
            g_frame_count++;
            xQueueSendFromISR(g_frame_queue, &frame, NULL);
        } else if (g_frame_buffer.count < MAX_FRAME_BITS) {
            frame.status = FRAME_SHORT;
            frame.bit_count = g_frame_buffer.count;
            g_error_count++;
            xQueueSendFromISR(g_frame_queue, &frame, NULL);
        } else {
            frame.status = FRAME_OVERLONG;
            g_error_count++;
            xQueueSendFromISR(g_frame_queue, &frame, NULL);
        }
    }
    
    g_ce_state = level;
}

static void IRAM_ATTR gpio_clk_isr(void *arg)
{
    uint32_t level = gpio_get_level(GPIO_CLK_INPUT);
    
    if (!level) {
        /* CLK falling edge - capture DATA */
        if (g_ce_state && g_frame_buffer.count < MAX_FRAME_BITS) {
            uint8_t data = gpio_get_level(GPIO_DATA_INPUT);
            
            g_frame_buffer.samples[g_frame_buffer.count].timestamp_us = esp_timer_get_time();
            g_frame_buffer.samples[g_frame_buffer.count].bit_value = data;
            g_frame_buffer.count++;
            g_frame_buffer.clk_edge_count++;
        }
    }
    
    g_clk_state = level;
}

static void IRAM_ATTR gpio_data_isr(void *arg)
{
    uint32_t level = gpio_get_level(GPIO_DATA_INPUT);
    g_data_state = level;
}

/* ============================================================================
 * RMT Configuration for D-IN Output
 * ============================================================================ */

static esp_err_t configure_rmt_dout(void)
{
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,  /* 1 MHz = 1 µs per tick */
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .gpio_num = GPIO_DIN_OUTPUT,
    };
    
    esp_err_t err = rmt_new_tx_channel(&tx_chan_config, &g_rmt_chan_dout);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(err));
        return err;
    }
    
    err = rmt_enable(g_rmt_chan_dout);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(err));
        return err;
    }

    /* LSB-first NRZ: each bit holds its level for one CCB bit period
     * (no return-to-zero), matching the write side's bit timing. */
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {.level0 = 0, .duration0 = LC72130_BIT_PERIOD_US, .level1 = 0, .duration1 = 0},
        .bit1 = {.level0 = 1, .duration0 = LC72130_BIT_PERIOD_US, .level1 = 1, .duration1 = 0},
        .flags.msb_first = 0,
    };
    err = rmt_new_bytes_encoder(&bytes_encoder_config, &g_rmt_encoder_dout);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create D-IN bytes encoder: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "RMT D-IN output configured");
    return ESP_OK;
}

/* ============================================================================
 * GPIO Configuration
 * ============================================================================ */

static esp_err_t configure_gpio_inputs(void)
{
    /* Internal pulls hold CE/CLK/DATA at their protocol idle levels when the
     * Sony bus is disconnected, preventing floating-pin noise from being
     * decoded as spurious frames (GPIO input threshold itself is fixed in
     * hardware and is not software-adjustable). */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_CE_INPUT) | (1ULL << GPIO_CLK_INPUT) | (1ULL << GPIO_DATA_INPUT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO inputs: %s", esp_err_to_name(err));
        return err;
    }

    /* CE idles inactive LOW; CLK idles HIGH per CCB CPOL=1; DATA has no
     * defined idle level, pull it low for stability. */
    gpio_set_pull_mode(GPIO_CE_INPUT, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(GPIO_CLK_INPUT, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_DATA_INPUT, GPIO_PULLDOWN_ONLY);

    /* Install ISR service */
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Add ISR handlers */
    gpio_isr_handler_add(GPIO_CE_INPUT, gpio_ce_isr, NULL);
    gpio_isr_handler_add(GPIO_CLK_INPUT, gpio_clk_isr, NULL);
    gpio_isr_handler_add(GPIO_DATA_INPUT, gpio_data_isr, NULL);
    
    ESP_LOGI(TAG, "GPIO inputs configured (CE=%d CLK=%d DATA=%d)", 
             GPIO_CE_INPUT, GPIO_CLK_INPUT, GPIO_DATA_INPUT);
    
    return ESP_OK;
}

static esp_err_t configure_gpio_output(void)
{
#if !FEATURE_DIN_DISABLED_AT_COMPILE_TIME
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_DIN_OUTPUT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO D-IN output: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Set D-IN to idle state (LOW) initially */
    gpio_set_level(GPIO_DIN_OUTPUT, 0);
    
    ESP_LOGI(TAG, "GPIO D-IN output configured (GPIO%d)", GPIO_DIN_OUTPUT);
    
#else
    ESP_LOGI(TAG, "D-IN output disabled at compile time (safety mode)");
#endif
    
    return ESP_OK;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

esp_err_t lc72130_bus_init(void)
{
    ESP_LOGI(TAG, "Initializing LC72130 bus interface");
    
    /* Create frame queue */
    g_frame_queue = xQueueCreate(16, sizeof(captured_frame_t));
    if (!g_frame_queue) {
        ESP_LOGE(TAG, "Failed to create frame queue");
        return ESP_ERR_NO_MEM;
    }
    
    /* Configure GPIO */
    esp_err_t err = configure_gpio_inputs();
    if (err != ESP_OK) return err;
    
    err = configure_gpio_output();
    if (err != ESP_OK) return err;
    
    /* Configure RMT for D-IN output */
    if (config_get_operating_mode() == MODE_EMULATOR) {
        err = configure_rmt_dout();
        if (err != ESP_OK) {
            /* Non-fatal: frame capture must keep working even if D-IN transmit
             * setup fails, since D-IN is only needed for the optional read/status
             * response feature. */
            g_din_init_error = err;
            ESP_LOGE(TAG, "D-IN RMT setup failed, continuing without D-IN: %s",
                     esp_err_to_name(err));
        }
    }
    
    ESP_LOGI(TAG, "LC72130 bus interface initialized");
    return ESP_OK;
}

esp_err_t lc72130_bus_start(void)
{
    ESP_LOGI(TAG, "Starting bus monitoring");
    /* ISRs are already active from gpio_config with GPIO_INTR_ANYEDGE */
    return ESP_OK;
}

esp_err_t lc72130_bus_stop(void)
{
    ESP_LOGI(TAG, "Stopping bus monitoring");
    /* Could disable ISRs here if needed */
    return ESP_OK;
}

bool lc72130_bus_frame_ready(captured_frame_t *frame)
{
    if (!frame || !g_frame_queue) return false;
    
    return xQueueReceive(g_frame_queue, frame, 0) == pdPASS;
}

esp_err_t lc72130_bus_drive_din_response(uint8_t response_byte, uint8_t bit_count)
{
#if FEATURE_DIN_DISABLED_AT_COMPILE_TIME
    ESP_LOGW(TAG, "D-IN output disabled at compile time");
    return ESP_ERR_NOT_SUPPORTED;
#endif
    
    if (config_get_operating_mode() != MODE_EMULATOR) {
        ESP_LOGW(TAG, "Not in EMULATOR mode, cannot drive D-IN");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_din_override != DIN_OVERRIDE_AUTO) {
        ESP_LOGD(TAG, "D-IN override active (%s); skipping automatic response",
                 lc72130_bus_din_override_name(g_din_override));
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_rmt_chan_dout || !g_rmt_encoder_dout) {
        ESP_LOGE(TAG, "RMT channel not initialized");
        return ESP_ERR_NOT_FOUND;
    }

    if (bit_count != 8) {
        ESP_LOGE(TAG, "Only 8-bit D-IN responses are supported");
        return ESP_ERR_INVALID_ARG;
    }

    const rmt_transmit_config_t tx_config = {.loop_count = 0};
    esp_err_t err = rmt_transmit(g_rmt_chan_dout, g_rmt_encoder_dout, &response_byte, 1, &tx_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to transmit D-IN response: %s", esp_err_to_name(err));
        return err;
    }

    g_din_state = response_byte & 1;  /* LSB is first bit out */
    ESP_LOGI(TAG, "D-IN response driven: 0x%02X (%d bits)", response_byte, bit_count);

    return ESP_OK;
}

uint32_t lc72130_bus_get_ce_state(void)
{
    return g_ce_state;
}

uint32_t lc72130_bus_get_clk_state(void)
{
    return g_clk_state;
}

uint32_t lc72130_bus_get_data_state(void)
{
    return g_data_state;
}

uint32_t lc72130_bus_get_din_state(void)
{
    return g_din_state;
}

bool lc72130_bus_din_hardware_ready(void)
{
    return g_rmt_chan_dout != NULL && g_rmt_encoder_dout != NULL;
}

esp_err_t lc72130_bus_get_din_init_error(void)
{
    return g_din_init_error;
}

esp_err_t lc72130_bus_set_din_override(din_override_t mode)
{
    g_din_override = mode;

    if (mode == DIN_OVERRIDE_AUTO) {
        return ESP_OK;
    }
#if FEATURE_DIN_DISABLED_AT_COMPILE_TIME
    return ESP_ERR_NOT_SUPPORTED;
#endif
    if (config_get_operating_mode() != MODE_EMULATOR) {
        ESP_LOGW(TAG, "Not in EMULATOR mode, cannot force D-IN");
        return ESP_ERR_INVALID_STATE;
    }
    if (!g_rmt_chan_dout || !g_rmt_encoder_dout) {
        ESP_LOGE(TAG, "RMT channel not initialized");
        return ESP_ERR_NOT_FOUND;
    }

    /* A one-off transmission whose "end of transmission" level is the forced
     * level; since nothing else transmits while an override is active, the
     * pad simply holds at that level afterward. */
    uint8_t level = mode == DIN_OVERRIDE_FORCE_HIGH ? 1 : 0;
    uint8_t payload = level ? 0xFF : 0x00;
    const rmt_transmit_config_t tx_config = {.loop_count = 0, .flags.eot_level = level};
    esp_err_t err = rmt_transmit(g_rmt_chan_dout, g_rmt_encoder_dout, &payload, 1, &tx_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to force D-IN level: %s", esp_err_to_name(err));
        return err;
    }

    g_din_state = level;
    ESP_LOGI(TAG, "D-IN forced %s", level ? "HIGH" : "LOW");
    return ESP_OK;
}

din_override_t lc72130_bus_get_din_override(void)
{
    return g_din_override;
}

const char *lc72130_bus_din_override_name(din_override_t mode)
{
    switch (mode) {
        case DIN_OVERRIDE_FORCE_HIGH: return "high";
        case DIN_OVERRIDE_FORCE_LOW:  return "low";
        default:                      return "auto";
    }
}

static esp_timer_handle_t g_din_pulse_timer = NULL;

static void din_pulse_timer_callback(void *arg)
{
    lc72130_bus_set_din_override(DIN_OVERRIDE_FORCE_HIGH);
}

esp_err_t lc72130_bus_pulse_din_low(uint32_t pulse_ms)
{
    esp_err_t err = lc72130_bus_set_din_override(DIN_OVERRIDE_FORCE_LOW);
    if (err != ESP_OK) {
        return err;
    }

    if (!g_din_pulse_timer) {
        const esp_timer_create_args_t timer_args = {
            .callback = &din_pulse_timer_callback,
            .name = "din_pulse",
        };
        err = esp_timer_create(&timer_args, &g_din_pulse_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create D-IN pulse timer: %s", esp_err_to_name(err));
            return err;
        }
    } else {
        esp_timer_stop(g_din_pulse_timer);  /* no-op if not currently running */
    }

    return esp_timer_start_once(g_din_pulse_timer, (uint64_t)pulse_ms * 1000);
}

uint32_t lc72130_bus_get_frame_count(void)
{
    return g_frame_count;
}

uint32_t lc72130_bus_get_error_count(void)
{
    return g_error_count;
}

uint32_t lc72130_bus_get_read_count(void)
{
    return g_read_count;
}

void lc72130_bus_reset(void)
{
    ESP_LOGI(TAG, "Resetting bus capture state");
    memset(&g_frame_buffer, 0, sizeof(g_frame_buffer));
    g_frame_count = 0;
    g_error_count = 0;
    g_read_count = 0;
    
    /* Flush any pending frames */
    captured_frame_t dummy;
    while (xQueueReceive(g_frame_queue, &dummy, 0) == pdPASS) {
        /* Discard */
    }
}

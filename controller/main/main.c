/**
 * @file main.c
 * @brief Application entry point and main task loop
 *
 * Initializes all subsystems and runs the main event processing loop.
 * Handles frame capture, decoding, emulation state, and deferred logging.
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/uart.h"

#include "config.h"
#include "event_log.h"
#include "lc72130_bus.h"
#include "lc72130_protocol.h"
#include "lc72130_decoder.h"
#include "lc72130_emulator.h"
#include "frequency_test.h"
#include "radio_link.h"
#include "radio_metadata_poll.h"
#include "rds_output.h"
#include "wifi_monitor.h"

static const char *TAG = "MAIN";

/* ============================================================================
 * Application State
 * ============================================================================ */

typedef struct {
    uint32_t frames_processed;
    uint32_t last_frequency_mhz_int;  /* For comparison */
    uint64_t last_frame_time_us;
    bool is_running;
} app_state_t;

static app_state_t g_app_state = {0};

/* ============================================================================
 * Initialization
 * ============================================================================ */

static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "Erasing NVS flash...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static esp_err_t init_uart(void)
{
    /* UART is already initialized by ESP-IDF logging system */
    ESP_LOGI(TAG, "UART initialized for logging (115200 baud)");
    return ESP_OK;
}

static esp_err_t init_subsystems(void)
{
    esp_err_t err;
    
    ESP_LOGI(TAG, "Initializing subsystems...");
    
    err = config_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Config init failed");
        return err;
    }
    
    err = event_log_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Event log init failed");
        return err;
    }
    
    err = lc72130_bus_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LC72130 bus init failed");
        return err;
    }
    
    err = lc72130_protocol_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Protocol init failed");
        return err;
    }
    
    err = lc72130_emulator_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Emulator init failed");
        return err;
    }
    
    err = lc72130_bus_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bus start failed");
        return err;
    }
    
    ESP_LOGI(TAG, "All subsystems initialized successfully");
    return ESP_OK;
}

/* ============================================================================
 * Main Event Processing Loop
 * ============================================================================ */

static void print_banner(void)
{
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║         ESP32-S3 LC72130 Frequency Synthesizer Emulator       ║\n");
    printf("║                   Sony ST-SA3ES Tuner Control                 ║\n");
    printf("║                                                              ║\n");
    printf("║  Mode: PASSIVE_MONITOR (decode & log, D-IN inactive)        ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

static void print_status(void)
{
    const lc72130_state_t *state = lc72130_emulator_get_state();
    uint32_t queue_depth = event_log_get_queue_depth();
    uint32_t frame_cnt = lc72130_bus_get_frame_count();
    uint32_t err_cnt = lc72130_bus_get_error_count();
    
    printf("\n");
    printf("────────────────────────────────────────────────────────────────\n");
    printf("Status Report:\n");
    printf("  Frames received:      %" PRIu32 " (errors: %" PRIu32 ")\n", frame_cnt, err_cnt);
    printf("  Transactions:         %" PRIu32 "\n", lc72130_emulator_get_transaction_count());
    printf("  Read requests:        %" PRIu32 "\n", lc72130_emulator_get_read_count());
    printf("  Current frequency:    %.2f MHz\n", state->current_frequency_mhz);
    printf("  PLL state:            %s\n", state->pll_locked ? "LOCKED" : "UNLOCKED");
    printf("  Tuner ready:          %s\n", state->tuner_ready ? "YES" : "NO");
    printf("  Event queue depth:    %" PRIu32 "\n", queue_depth);
    printf("  Uptime:               %lld seconds\n", esp_timer_get_time() / 1000000);
    printf("────────────────────────────────────────────────────────────────\n");
    printf("\n");
}

static void main_task(void *arg)
{
    print_banner();
    
    lc72130_operating_mode_t mode = config_get_operating_mode();
    ESP_LOGI(TAG, "Starting main event loop (mode=%d)", mode);
    
    g_app_state.is_running = true;
    uint32_t iteration = 0;
    uint64_t last_status_time = esp_timer_get_time();
    
    while (g_app_state.is_running) {
        /* Check for captured frames */
        captured_frame_t frame;
        while (lc72130_bus_frame_ready(&frame)) {
            g_app_state.frames_processed++;
            g_app_state.last_frame_time_us = esp_timer_get_time();
            
            lc72130_transaction_t transaction;
            
            /* Determine if this is a read or write transaction
             * For now, assume writes unless we have explicit read detection
             * TODO: Implement read detection via CE/CLK timing analysis
             */
            bool is_read = false;
            
            esp_err_t err = lc72130_protocol_decode(frame.byte0, frame.byte1, frame.byte2,
                                                    frame.capture_time_us, is_read,
                                                    &transaction);
            if (err == ESP_OK) {
                /* Process the transaction */
                lc72130_emulator_process_transaction(&transaction);
                
                /* In EMULATOR mode, respond to read requests */
                if (mode == MODE_EMULATOR && is_read) {
                    uint8_t din_response;
                    if (lc72130_emulator_generate_din_response(&din_response) == ESP_OK) {
                        lc72130_bus_drive_din_response(din_response, 8);
                    }
                }
            } else {
                ESP_LOGW(TAG, "Failed to decode frame: %02X %02X %02X",
                         frame.byte0, frame.byte1, frame.byte2);
                event_log_frame_malformed("decode_error");
            }
        }
        
        /* Flush logging queue periodically */
        if (iteration % 100 == 0) {
            event_log_flush();
        }
        
        /* Print status every 30 seconds */
        uint64_t now = esp_timer_get_time();
        if (now - last_status_time > 30000000) {
            print_status();
            last_status_time = now;
        }
        
        iteration++;
        vTaskDelay(pdMS_TO_TICKS(10));  /* 10 ms task sleep */
    }
    
    ESP_LOGI(TAG, "Main task shutting down");
    vTaskDelete(NULL);
}

/* ============================================================================
 * Entry Point
 * ============================================================================ */

void app_main(void)
{
    printf("\n\n");
    printf("ESP32-S3 LC72130 Emulator - Initializing...\n");
    printf("Build time: " __DATE__ " " __TIME__ "\n\n");
    
    init_nvs();
    init_uart();

    esp_err_t err = rds_output_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RDS output initialization failed: %s", esp_err_to_name(err));
    }

    err = radio_link_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Radio ESP link initialization failed: %s", esp_err_to_name(err));
    }
    
    err = init_subsystems();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Subsystem initialization failed: %s", esp_err_to_name(err));
        /* Keep going: Wi-Fi/OTA must stay reachable even if a peripheral
         * subsystem fails, otherwise the device can only be recovered over USB. */
    }

    err = frequency_test_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Frequency test initialization failed: %s", esp_err_to_name(err));
    }

    err = wifi_monitor_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi monitor initialization failed: %s", esp_err_to_name(err));
    }

    err = radio_metadata_poll_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Radio ESP metadata poll initialization failed: %s", esp_err_to_name(err));
    }
    
    /* Create main event processing task */
    BaseType_t task_err = xTaskCreate(main_task, "lc72130_main", 4096, NULL, 5, NULL);
    if (task_err != pdPASS) {
        ESP_LOGE(TAG, "Failed to create main task");
    }
}

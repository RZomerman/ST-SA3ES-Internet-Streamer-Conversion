/**
 * @file event_log.c
 * @brief Event logging implementation (ISR-safe via FreeRTOS queues)
 */

#include <inttypes.h>
#include "event_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static const char *TAG = "EVENT_LOG";

/* Global event queue */
static QueueHandle_t g_event_queue = NULL;

/* ============================================================================
 * Helper: Format and print event
 * ============================================================================ */

static void log_event_to_serial(const event_log_entry_t *event)
{
    if (!event) return;
    
    char timestamp_str[32];
    snprintf(timestamp_str, sizeof(timestamp_str), "[%lld us] ", event->timestamp_us);
    
    switch (event->type) {
        case EVENT_FRAME_RECEIVED: {
            const struct { uint8_t byte0, byte1, byte2; uint32_t divider; float frequency_mhz; uint8_t control_byte; } *f = &event->data.frame_data;
            printf("%sLC72130 WRITE: raw=%02X %02X %02X divider=0x%04" PRIx32 " frequency=%.2f MHz control=0x%02X\n",
                   timestamp_str, f->byte0, f->byte1, f->byte2, f->divider, f->frequency_mhz, f->control_byte);
            break;
        }
        case EVENT_FRAME_MALFORMED:
            printf("%sMALFORMED FRAME: %s\n", timestamp_str, event->data.text.message);
            break;
        case EVENT_READ_REQUEST:
            printf("%sREAD REQUEST detected\n", timestamp_str);
            break;
        case EVENT_DIN_RESPONSE_SENT:
            printf("%sD-IN RESPONSE: 0x%02X (bits: 0x%08" PRIx32 ")\n", timestamp_str,
                   event->data.din_response.response_byte, event->data.din_response.bit_pattern);
            break;
        case EVENT_STATE_UPDATE: {
            const event_state_change_t *s = &event->data.state_change;
            printf("%sSTATE UPDATE: field=%d old=0x%08" PRIx32 " new=0x%08" PRIx32 "\n",
                   timestamp_str, s->state_id, s->old_value, s->new_value);
            break;
        }
        case EVENT_INFO:
        case EVENT_DEBUG:
            printf("%s%s\n", timestamp_str, event->data.text.message);
            break;
        case EVENT_ERROR:
            printf("%sERROR: %s\n", timestamp_str, event->data.text.message);
            break;
        default:
            printf("%sUNKNOWN EVENT TYPE: %d\n", timestamp_str, event->type);
            break;
    }
    fflush(stdout);
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

esp_err_t event_log_init(void)
{
    g_event_queue = xQueueCreate(EVENT_LOG_QUEUE_SIZE, sizeof(event_log_entry_t));
    if (!g_event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Event logging initialized (queue size: %d)", EVENT_LOG_QUEUE_SIZE);
    return ESP_OK;
}

void event_log_frame_received(uint8_t byte0, uint8_t byte1, uint8_t byte2)
{
    if (!g_event_queue) return;
    
    event_log_entry_t event = {
        .timestamp_us = esp_timer_get_time(),
        .type = EVENT_FRAME_RECEIVED,
        .data.frame_data = {
            .byte0 = byte0,
            .byte1 = byte1,
            .byte2 = byte2,
            .divider = byte0 | (byte1 << 8),
            .frequency_mhz = (byte0 | (byte1 << 8)) * 0.05f - 10.70f,
            .control_byte = byte2,
        }
    };
    
    xQueueSendFromISR(g_event_queue, &event, NULL);
}

void event_log_frame_malformed(const char *reason)
{
    if (!g_event_queue) return;
    
    event_log_entry_t event = {
        .timestamp_us = esp_timer_get_time(),
        .type = EVENT_FRAME_MALFORMED,
    };
    
    if (reason) {
        strncpy(event.data.text.message, reason, sizeof(event.data.text.message) - 1);
        event.data.text.message[sizeof(event.data.text.message) - 1] = '\0';
    }
    
    xQueueSendFromISR(g_event_queue, &event, NULL);
}

void event_log_read_request(void)
{
    if (!g_event_queue) return;
    
    event_log_entry_t event = {
        .timestamp_us = esp_timer_get_time(),
        .type = EVENT_READ_REQUEST,
    };
    
    xQueueSendFromISR(g_event_queue, &event, NULL);
}

void event_log_din_response(uint8_t response_byte)
{
    if (!g_event_queue) return;
    
    event_log_entry_t event = {
        .timestamp_us = esp_timer_get_time(),
        .type = EVENT_DIN_RESPONSE_SENT,
        .data.din_response = {
            .response_byte = response_byte,
            .bit_pattern = response_byte,
        }
    };
    
    xQueueSendFromISR(g_event_queue, &event, NULL);
}

void event_log_state_update(uint8_t state_id, uint32_t old_value, uint32_t new_value)
{
    if (!g_event_queue) return;
    
    event_log_entry_t event = {
        .timestamp_us = esp_timer_get_time(),
        .type = EVENT_STATE_UPDATE,
        .data.state_change = {
            .state_id = state_id,
            .old_value = old_value,
            .new_value = new_value,
        }
    };
    
    xQueueSendFromISR(g_event_queue, &event, NULL);
}

void event_log_printf(log_level_t level, const char *format, ...)
{
    if (!g_event_queue || !format) return;
    
    event_log_entry_t event = {
        .timestamp_us = esp_timer_get_time(),
        .type = (level == LOG_LEVEL_ERROR) ? EVENT_ERROR : 
                (level == LOG_LEVEL_DEBUG) ? EVENT_DEBUG : EVENT_INFO,
    };
    
    va_list args;
    va_start(args, format);
    vsnprintf(event.data.text.message, sizeof(event.data.text.message), format, args);
    va_end(args);
    
    event.data.text.message[sizeof(event.data.text.message) - 1] = '\0';
    
    BaseType_t err = xQueueSend(g_event_queue, &event, pdMS_TO_TICKS(10));
    if (err != pdPASS) {
        ESP_LOGW(TAG, "Event queue full, dropped message");
    }
}

void event_log_flush(void)
{
    if (!g_event_queue) return;
    
    event_log_entry_t event;
    while (xQueueReceive(g_event_queue, &event, 0) == pdPASS) {
        log_event_to_serial(&event);
    }
}

uint32_t event_log_get_queue_depth(void)
{
    if (!g_event_queue) return 0;
    return uxQueueMessagesWaiting(g_event_queue);
}

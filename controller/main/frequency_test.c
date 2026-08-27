#include "frequency_test.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "lc72130_decoder.h"
#include "lc72130_emulator.h"
#include "lc72130_protocol.h"

static const char *TAG = "FREQUENCY_TEST";
static QueueHandle_t frequency_queue;

static void frequency_test_task(void *arg)
{
    float frequency_mhz;
    while (true) {
        if (xQueueReceive(frequency_queue, &frequency_mhz, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        uint16_t divider;
        uint8_t byte0;
        uint8_t byte1;
        uint8_t byte2;
        lc72130_transaction_t transaction;

        if (!lc72130_decoder_frequency_to_divider(frequency_mhz, &divider) ||
            !lc72130_decoder_divider_to_frame(divider, 0x2A, &byte0, &byte1, &byte2) ||
            lc72130_protocol_decode(byte0, byte1, byte2, esp_timer_get_time(), false,
                                    &transaction) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to synthesize %.2f MHz", frequency_mhz);
            continue;
        }

        ESP_LOGI(TAG, "Simulating inbound frequency %.2f MHz as frame %02X %02X %02X",
                 frequency_mhz, byte0, byte1, byte2);
        esp_err_t err = lc72130_emulator_process_transaction(&transaction);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to process simulated frequency: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t frequency_test_init(void)
{
    frequency_queue = xQueueCreate(8, sizeof(float));
    if (frequency_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(frequency_test_task, "frequency_test", 4096, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(frequency_queue);
        frequency_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t frequency_test_simulate(float frequency_mhz)
{
    if (frequency_mhz < 76.0f || frequency_mhz > 108.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    if (frequency_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return xQueueSend(frequency_queue, &frequency_mhz, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
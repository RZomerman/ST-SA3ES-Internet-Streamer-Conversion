#include "radio_link.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "config.h"
#include "rds_output.h"

#define ST_AST_PULSE_DURATION_US 300000  /* 300 ms flash when a stream starts playing */

static const char *TAG = "RADIO_LINK";
static bool initialized;
static radio_metadata_t metadata;
static SemaphoreHandle_t metadata_mutex;
static char cached_radio_ip[46];
static gpio_override_t st_override = GPIO_OVERRIDE_AUTO;
static gpio_override_t ast_override = GPIO_OVERRIDE_AUTO;
static gpio_override_t si_override = GPIO_OVERRIDE_AUTO;
static esp_err_t g_init_error = ESP_OK;
static radio_playback_state_t previous_playback_state = RADIO_PLAYBACK_IDLE;
static esp_timer_handle_t st_ast_pulse_timer;

static void st_ast_pulse_timer_callback(void *arg)
{
    if (GPIO_IS_VALID_OUTPUT_GPIO(GPIO_ST) && st_override == GPIO_OVERRIDE_AUTO) {
        gpio_set_level(GPIO_ST, 0);
    }
    if (GPIO_IS_VALID_OUTPUT_GPIO(GPIO_AST) && ast_override == GPIO_OVERRIDE_AUTO) {
        gpio_set_level(GPIO_AST, 0);
    }
}

/* Recompute ST/AST from playback state, honoring any manual override.
 * Caller must hold metadata_mutex. */
static void apply_output_levels(void)
{
    bool entering_playing = metadata.playback_state == RADIO_PLAYBACK_PLAYING &&
                            previous_playback_state != RADIO_PLAYBACK_PLAYING;
    previous_playback_state = metadata.playback_state;

    /* AUTO mode: ST/AST rest LOW; a found+playing stream only flashes them
     * briefly rather than holding them on for the whole playback duration. */
    bool st_level = st_override == GPIO_OVERRIDE_FORCE_ON ? true :
                    st_override == GPIO_OVERRIDE_FORCE_OFF ? false :
                    entering_playing;
    bool ast_level = ast_override == GPIO_OVERRIDE_FORCE_ON ? true :
                     ast_override == GPIO_OVERRIDE_FORCE_OFF ? false :
                     (metadata.playback_state == RADIO_PLAYBACK_SEARCH || entering_playing);
    bool si_level = si_override == GPIO_OVERRIDE_FORCE_ON ? true :
                    si_override == GPIO_OVERRIDE_FORCE_OFF ? false :
                    (metadata.playback_state == RADIO_PLAYBACK_SEARCH ||
                     metadata.playback_state == RADIO_PLAYBACK_PLAYING);

    if (GPIO_IS_VALID_OUTPUT_GPIO(GPIO_ST)) {
        gpio_set_level(GPIO_ST, st_level);
    }
    if (GPIO_IS_VALID_OUTPUT_GPIO(GPIO_AST)) {
        gpio_set_level(GPIO_AST, ast_level);
    }
    if (GPIO_IS_VALID_OUTPUT_GPIO(GPIO_SI)) {
        gpio_set_level(GPIO_SI, si_level);
    }

    if (entering_playing && st_ast_pulse_timer != NULL) {
        esp_timer_stop(st_ast_pulse_timer);  /* no-op if not currently running */
        esp_timer_start_once(st_ast_pulse_timer, ST_AST_PULSE_DURATION_US);
    }
}

static char *trim_value(char *value)
{
    while (isspace((unsigned char)*value)) value++;
    size_t length = strlen(value);
    while (length > 0 && isspace((unsigned char)value[length - 1])) value[--length] = '\0';
    if (length >= 2 && value[0] == '[' && value[length - 1] == ']') {
        value[length - 1] = '\0';
        value++;
    }
    return value;
}

static esp_err_t update_field(const char *key, char *value)
{
    value = trim_value(value);
    xSemaphoreTake(metadata_mutex, portMAX_DELAY);
    if (strcmp(key, "STATION") == 0) {
        strlcpy(metadata.station, value, sizeof(metadata.station));
    } else if (strcmp(key, "NOW_PLAYING") == 0) {
        strlcpy(metadata.now_playing, value, sizeof(metadata.now_playing));
    } else if (strcmp(key, "GENRE") == 0) {
        strlcpy(metadata.genre, value, sizeof(metadata.genre));
        metadata.pty = rds_output_pty_for_genre(metadata.genre);
    } else if (strcmp(key, "BITRATE") == 0) {
        strlcpy(metadata.bitrate, value, sizeof(metadata.bitrate));
    } else if (strcmp(key, "STREAM_URL") == 0) {
        strlcpy(metadata.stream_url, value, sizeof(metadata.stream_url));
    } else if (strcmp(key, "IP") == 0) {
        strlcpy(cached_radio_ip, value, sizeof(cached_radio_ip));
        ESP_LOGI(TAG, "Cached Radio ESP API address: %s", cached_radio_ip);
    } else if (strcmp(key, "PLAYING") == 0) {
        if (strcasecmp(value, "SEARCH") == 0) {
            metadata.playback_state = RADIO_PLAYBACK_SEARCH;
        } else if (strcasecmp(value, "TRUE") == 0 || strcasecmp(value, "PLAYING") == 0) {
            metadata.playback_state = RADIO_PLAYBACK_PLAYING;
        } else if (strcasecmp(value, "ERROR") == 0) {
            metadata.playback_state = RADIO_PLAYBACK_ERROR;
        } else {
            metadata.playback_state = RADIO_PLAYBACK_IDLE;
        }
        apply_output_levels();
        esp_err_t err = rds_output_update(&metadata);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update RDS metadata: %s", esp_err_to_name(err));
        }
    } else {
        xSemaphoreGive(metadata_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    xSemaphoreGive(metadata_mutex);
    return ESP_OK;
}

esp_err_t radio_link_process_metadata_line(const char *line)
{
    if (line == NULL || metadata_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char copy[256];
    if (strlcpy(copy, line, sizeof(copy)) >= sizeof(copy)) {
        return ESP_ERR_INVALID_SIZE;
    }
    char *separator = strchr(copy, ':');
    if (separator == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *separator = '\0';
    return update_field(copy, separator + 1);
}

esp_err_t radio_link_get_metadata(radio_metadata_t *metadata_out)
{
    if (metadata_out == NULL || metadata_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(metadata_mutex, portMAX_DELAY);
    *metadata_out = metadata;
    xSemaphoreGive(metadata_mutex);
    return ESP_OK;
}

esp_err_t radio_link_get_radio_ip(char *ip_out, size_t ip_out_size)
{
    if (ip_out == NULL || ip_out_size == 0 || metadata_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(metadata_mutex, portMAX_DELAY);
    strlcpy(ip_out, cached_radio_ip, ip_out_size);
    xSemaphoreGive(metadata_mutex);
    return cached_radio_ip[0] != '\0' ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t radio_link_apply_now_playing(const radio_metadata_t *now_playing)
{
    if (now_playing == NULL || metadata_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(metadata_mutex, portMAX_DELAY);
    strlcpy(metadata.station, now_playing->station, sizeof(metadata.station));
    strlcpy(metadata.now_playing, now_playing->now_playing, sizeof(metadata.now_playing));
    strlcpy(metadata.genre, now_playing->genre, sizeof(metadata.genre));
    strlcpy(metadata.bitrate, now_playing->bitrate, sizeof(metadata.bitrate));
    strlcpy(metadata.stream_url, now_playing->stream_url, sizeof(metadata.stream_url));
    metadata.pty = rds_output_pty_for_genre(metadata.genre);
    metadata.playback_state = now_playing->playback_state;

    apply_output_levels();
    esp_err_t err = rds_output_update(&metadata);
    xSemaphoreGive(metadata_mutex);
    return err;
}

esp_err_t radio_link_set_st_override(gpio_override_t override)
{
    if (metadata_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(metadata_mutex, portMAX_DELAY);
    st_override = override;
    apply_output_levels();
    xSemaphoreGive(metadata_mutex);
    return ESP_OK;
}

esp_err_t radio_link_set_ast_override(gpio_override_t override)
{
    if (metadata_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(metadata_mutex, portMAX_DELAY);
    ast_override = override;
    apply_output_levels();
    xSemaphoreGive(metadata_mutex);
    return ESP_OK;
}

gpio_override_t radio_link_get_st_override(void)
{
    return st_override;
}

gpio_override_t radio_link_get_ast_override(void)
{
    return ast_override;
}

const char *radio_link_override_name(gpio_override_t override)
{
    switch (override) {
        case GPIO_OVERRIDE_FORCE_ON:  return "on";
        case GPIO_OVERRIDE_FORCE_OFF: return "off";
        default:                      return "auto";
    }
}

esp_err_t radio_link_set_si_override(gpio_override_t override)
{
    if (metadata_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(metadata_mutex, portMAX_DELAY);
    si_override = override;
    apply_output_levels();
    xSemaphoreGive(metadata_mutex);
    return ESP_OK;
}

gpio_override_t radio_link_get_si_override(void)
{
    return si_override;
}

esp_err_t radio_link_get_init_error(void)
{
    return g_init_error;
}

static void radio_receive_task(void *arg)
{
    char line[256];
    size_t length = 0;
    uint8_t received[64];

    while (true) {
        int count = uart_read_bytes(UART_NUM_1, received, sizeof(received), pdMS_TO_TICKS(100));
        for (int i = 0; i < count; i++) {
            char character = received[i];
            if (character == '\n') {
                line[length] = '\0';
                esp_err_t err = radio_link_process_metadata_line(line);
                if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
                    ESP_LOGW(TAG, "Rejected metadata line: %s", line);
                }
                length = 0;
            } else if (character != '\r') {
                if (length < sizeof(line) - 1) {
                    line[length++] = character;
                } else {
                    ESP_LOGW(TAG, "Discarding overlong metadata line");
                    length = 0;
                }
            }
        }
    }
}

esp_err_t radio_link_init(void)
{
    metadata_mutex = xSemaphoreCreateMutex();
    if (metadata_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t pulse_timer_args = {
        .callback = &st_ast_pulse_timer_callback,
        .name = "st_ast_pulse",
    };
    esp_err_t timer_err = esp_timer_create(&pulse_timer_args, &st_ast_pulse_timer);
    if (timer_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ST/AST pulse timer: %s", esp_err_to_name(timer_err));
    }

    const uart_config_t uart_config = {
        .baud_rate = CONFIG_LC72130_RADIO_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(UART_NUM_1, 256, 256, 0, NULL, 0), TAG,
                        "Failed to install Radio ESP UART");
    ESP_RETURN_ON_ERROR(uart_param_config(UART_NUM_1, &uart_config), TAG,
                        "Failed to configure Radio ESP UART");
    ESP_RETURN_ON_ERROR(uart_set_pin(UART_NUM_1, CONFIG_LC72130_RADIO_UART_TX_GPIO,
                                     CONFIG_LC72130_RADIO_UART_RX_GPIO,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        TAG, "Failed to assign Radio ESP UART pins");

    initialized = true;
    if (GPIO_IS_VALID_OUTPUT_GPIO(GPIO_ST)) {
        gpio_config_t st_config = {
            .pin_bit_mask = 1ULL << GPIO_ST,
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t st_err = gpio_config(&st_config);
        if (st_err != ESP_OK) {
            /* Non-fatal: one output pin failing must not disable the others
             * or the Radio ESP UART receive task. */
            g_init_error = st_err;
            ESP_LOGE(TAG, "Failed to configure ST output: %s", esp_err_to_name(st_err));
        } else {
            gpio_set_level(GPIO_ST, 0);
        }
    } else {
        ESP_LOGE(TAG, "GPIO%d is input-only on ESP32-S3; ST output requires another pin", GPIO_ST);
    }
    if (GPIO_IS_VALID_OUTPUT_GPIO(GPIO_AST)) {
        gpio_config_t ast_config = {
            .pin_bit_mask = 1ULL << GPIO_AST,
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t ast_err = gpio_config(&ast_config);
        if (ast_err != ESP_OK) {
            g_init_error = ast_err;
            ESP_LOGE(TAG, "Failed to configure AST output: %s", esp_err_to_name(ast_err));
        } else {
            gpio_set_level(GPIO_AST, 0);
        }
    } else {
        ESP_LOGE(TAG, "GPIO%d is input-only on ESP32-S3; AST output requires another pin", GPIO_AST);
    }
    if (GPIO_IS_VALID_OUTPUT_GPIO(GPIO_SI)) {
        gpio_config_t si_config = {
            .pin_bit_mask = 1ULL << GPIO_SI,
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t si_err = gpio_config(&si_config);
        if (si_err != ESP_OK) {
            /* Non-fatal: SI is an independent manual output; a failure here
             * must not prevent the Radio ESP UART link from starting. */
            ESP_LOGE(TAG, "Failed to configure SI output: %s", esp_err_to_name(si_err));
        } else {
            gpio_set_level(GPIO_SI, 0);
        }
    } else {
        ESP_LOGE(TAG, "GPIO%d is input-only on ESP32-S3; SI output requires another pin", GPIO_SI);
    }
    if (xTaskCreate(radio_receive_task, "radio_rx", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Radio ESP UART ready: TX=GPIO%d RX=GPIO%d baud=%d",
             CONFIG_LC72130_RADIO_UART_TX_GPIO, CONFIG_LC72130_RADIO_UART_RX_GPIO,
             CONFIG_LC72130_RADIO_UART_BAUD);
    return ESP_OK;
}

esp_err_t radio_link_send_frequency(float frequency_mhz)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    char command[32];
    int length = snprintf(command, sizeof(command), "FREQ_MHZ=%.2f\n", frequency_mhz);
    if (length < 0 || length >= sizeof(command)) {
        return ESP_ERR_INVALID_SIZE;
    }

    int written = uart_write_bytes(UART_NUM_1, command, length);
    if (written != length) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sent to Radio ESP: %.*s", length - 1, command);
    return ESP_OK;
}
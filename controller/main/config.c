/**
 * @file config.c
 * @brief Configuration system implementation
 */

#include "config.h"
#include "esp_log.h"

static const char *TAG = "CONFIG";

/* Global configuration state */
static lc72130_operating_mode_t g_operating_mode = DEFAULT_OPERATING_MODE;
static uint8_t g_din_response = DEFAULT_DIN_RESPONSE;
static log_level_t g_log_level = DEFAULT_LOG_LEVEL;

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

esp_err_t config_init(void)
{
    ESP_LOGI(TAG, "Initializing configuration");
    ESP_LOGI(TAG, "GPIO assignments: CE=%d CLK=%d DATA=%d DIN=%d",
             GPIO_CE_INPUT, GPIO_CLK_INPUT, GPIO_DATA_INPUT, GPIO_DIN_OUTPUT);
    ESP_LOGI(TAG, "Operating mode: %d", g_operating_mode);
    ESP_LOGI(TAG, "Log level: %d", g_log_level);
    return ESP_OK;
}

lc72130_operating_mode_t config_get_operating_mode(void)
{
    return g_operating_mode;
}

void config_set_operating_mode(lc72130_operating_mode_t mode)
{
    ESP_LOGI(TAG, "Operating mode changed: %d -> %d", g_operating_mode, mode);
    g_operating_mode = mode;
}

uint8_t config_get_din_response(void)
{
    return g_din_response;
}

void config_set_din_response(uint8_t response)
{
    ESP_LOGI(TAG, "D-IN response changed: 0x%02x -> 0x%02x", g_din_response, response);
    g_din_response = response;
}

log_level_t config_get_log_level(void)
{
    return g_log_level;
}

void config_set_log_level(log_level_t level)
{
    ESP_LOGI(TAG, "Log level changed: %d -> %d", g_log_level, level);
    g_log_level = level;
}

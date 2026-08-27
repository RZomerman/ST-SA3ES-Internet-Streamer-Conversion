#include "wifi_manager.h"

#include <stdint.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#define INITIAL_RETRY_DELAY_SECONDS 1U

static const char *TAG = "wifi_manager";
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_manager_state_t state = WIFI_MANAGER_STOPPED;
static uint32_t retry_delay_seconds = INITIAL_RETRY_DELAY_SECONDS;
static esp_timer_handle_t retry_timer;

static void set_state(wifi_manager_state_t new_state)
{
    portENTER_CRITICAL(&state_lock);
    state = new_state;
    portEXIT_CRITICAL(&state_lock);
}

static void connect(void)
{
    set_state(WIFI_MANAGER_CONNECTING);
    esp_err_t error = esp_wifi_connect();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connect request failed: %s", esp_err_to_name(error));
    }
}

static void retry_timer_callback(void *argument)
{
    (void)argument;
    connect();
}

static void schedule_retry(void)
{
    const uint32_t delay = retry_delay_seconds;
    const uint32_t maximum_delay = CONFIG_RADIO_WIFI_MAX_BACKOFF_SECONDS;

    set_state(WIFI_MANAGER_RETRY_WAIT);
    ESP_LOGW(TAG, "Wi-Fi disconnected; retrying in %lu seconds",
             (unsigned long)delay);

    esp_err_t error = esp_timer_start_once(retry_timer, delay * 1000000ULL);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Could not schedule Wi-Fi retry: %s", esp_err_to_name(error));
        return;
    }

    retry_delay_seconds = delay >= maximum_delay / 2U ? maximum_delay : delay * 2U;
}

static void event_handler(void *argument, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    (void)argument;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = event_data;
        ESP_LOGW(TAG, "Wi-Fi disconnect reason: %u", disconnected->reason);
        schedule_retry();
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = event_data;
        retry_delay_seconds = INITIAL_RETRY_DELAY_SECONDS;
        set_state(WIFI_MANAGER_CONNECTED);
        ESP_LOGI(TAG, "Wi-Fi connected, IPv4 address: " IPSTR,
                 IP2STR(&got_ip->ip_info.ip));
    }
}

esp_err_t wifi_manager_start(void)
{
    if (CONFIG_RADIO_WIFI_SSID[0] == '\0') {
        ESP_LOGE(TAG, "Wi-Fi SSID is not configured; run menuconfig");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Could not initialize TCP/IP");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG,
                        "Could not create event loop");

    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t initialization = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&initialization), TAG,
                        "Could not initialize Wi-Fi");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL),
        TAG, "Could not register Wi-Fi event handler");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL),
        TAG, "Could not register IP event handler");

    const esp_timer_create_args_t retry_timer_options = {
        .callback = retry_timer_callback,
        .name = "wifi_retry",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry_timer_options, &retry_timer), TAG,
                        "Could not create retry timer");

    wifi_config_t configuration = {0};
    strlcpy((char *)configuration.sta.ssid, CONFIG_RADIO_WIFI_SSID,
            sizeof(configuration.sta.ssid));
    strlcpy((char *)configuration.sta.password, CONFIG_RADIO_WIFI_PASSWORD,
            sizeof(configuration.sta.password));
    configuration.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    configuration.sta.pmf_cfg.capable = true;
    configuration.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG,
                        "Could not select station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &configuration), TAG,
                        "Could not configure station");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Could not start Wi-Fi");

    ESP_LOGI(TAG, "Wi-Fi station started for SSID '%s'", CONFIG_RADIO_WIFI_SSID);
    return ESP_OK;
}

wifi_manager_state_t wifi_manager_get_state(void)
{
    portENTER_CRITICAL(&state_lock);
    wifi_manager_state_t current_state = state;
    portEXIT_CRITICAL(&state_lock);
    return current_state;
}

bool wifi_manager_is_connected(void)
{
    return wifi_manager_get_state() == WIFI_MANAGER_CONNECTED;
}
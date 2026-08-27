#include "radio_metadata_poll.h"

#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "radio_link.h"
#include "rds_output.h"

#define POLL_INTERVAL_MS 2000
#define RESPONSE_BUFFER_SIZE 4096

static const char *TAG = "RADIO_METADATA_POLL";

typedef struct {
    char *buffer;
    size_t length;
} response_buffer_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }

    response_buffer_t *response = evt->user_data;
    size_t space_remaining = RESPONSE_BUFFER_SIZE - 1 - response->length;
    size_t to_copy = evt->data_len < (int)space_remaining ? evt->data_len : space_remaining;
    if (to_copy > 0) {
        memcpy(response->buffer + response->length, evt->data, to_copy);
        response->length += to_copy;
        response->buffer[response->length] = '\0';
    }
    return ESP_OK;
}

static void copy_json_string(const cJSON *root, const char *key, char *destination, size_t size,
                             const char *fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring != NULL && item->valuestring[0] != '\0') {
        strlcpy(destination, item->valuestring, size);
    } else {
        strlcpy(destination, fallback, size);
    }
}

static void apply_error_snapshot(void)
{
    radio_metadata_t error_snapshot = {0};
    strlcpy(error_snapshot.station, "NOINFO", sizeof(error_snapshot.station));
    strlcpy(error_snapshot.now_playing, "No Info", sizeof(error_snapshot.now_playing));
    strlcpy(error_snapshot.genre, "N/A", sizeof(error_snapshot.genre));
    strlcpy(error_snapshot.bitrate, "N/A", sizeof(error_snapshot.bitrate));
    strlcpy(error_snapshot.stream_url, "N/A", sizeof(error_snapshot.stream_url));
    error_snapshot.playback_state = RADIO_PLAYBACK_ERROR;

    esp_err_t err = radio_link_apply_now_playing(&error_snapshot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply error snapshot: %s", esp_err_to_name(err));
    }
}

static radio_playback_state_t parse_playback_state(const cJSON *root)
{
    const cJSON *state_item = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (cJSON_IsString(state_item) && state_item->valuestring != NULL) {
        if (strcasecmp(state_item->valuestring, "idle") == 0) {
            return RADIO_PLAYBACK_IDLE;
        }
        if (strcasecmp(state_item->valuestring, "search") == 0) {
            return RADIO_PLAYBACK_SEARCH;
        }
        if (strcasecmp(state_item->valuestring, "playing") == 0) {
            return RADIO_PLAYBACK_PLAYING;
        }
        return RADIO_PLAYBACK_ERROR;
    }

    const cJSON *playing_item = cJSON_GetObjectItemCaseSensitive(root, "playing");
    if (cJSON_IsBool(playing_item)) {
        return cJSON_IsTrue(playing_item) ? RADIO_PLAYBACK_PLAYING : RADIO_PLAYBACK_IDLE;
    }
    return RADIO_PLAYBACK_ERROR;
}

static void poll_now_playing(const char *ip)
{
    char url[80];
    if (snprintf(url, sizeof(url), "http://%s/api/now-playing", ip) >= (int)sizeof(url)) {
        ESP_LOGW(TAG, "Radio ESP IP too long to build request URL");
        return;
    }

    char *buffer = malloc(RESPONSE_BUFFER_SIZE);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Out of memory for now-playing response buffer");
        return;
    }
    response_buffer_t response = {.buffer = buffer, .length = 0};
    buffer[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 3000,
        .event_handler = http_event_handler,
        .user_data = &response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(buffer);
        apply_error_snapshot();
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Now-playing request failed: %s", esp_err_to_name(err));
        free(buffer);
        apply_error_snapshot();
        return;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "Now-playing request returned HTTP %d", status);
        free(buffer);
        apply_error_snapshot();
        return;
    }

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);
    if (root == NULL) {
        ESP_LOGW(TAG, "Now-playing response was not valid JSON");
        apply_error_snapshot();
        return;
    }

    radio_metadata_t now_playing = {0};
    copy_json_string(root, "station", now_playing.station, sizeof(now_playing.station), "NOINFO");
    copy_json_string(root, "now_playing", now_playing.now_playing, sizeof(now_playing.now_playing), "No Info");
    copy_json_string(root, "genre", now_playing.genre, sizeof(now_playing.genre), "N/A");
    copy_json_string(root, "bitrate", now_playing.bitrate, sizeof(now_playing.bitrate), "N/A");
    copy_json_string(root, "stream_url", now_playing.stream_url, sizeof(now_playing.stream_url), "N/A");
    now_playing.playback_state = parse_playback_state(root);
    cJSON_Delete(root);

    esp_err_t apply_err = radio_link_apply_now_playing(&now_playing);
    if (apply_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply polled now-playing metadata: %s", esp_err_to_name(apply_err));
    }
}

static void poll_task(void *arg)
{
    char ip[46];
    while (true) {
        if (radio_link_get_radio_ip(ip, sizeof(ip)) == ESP_OK) {
            poll_now_playing(ip);
        } else {
            apply_error_snapshot();
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

esp_err_t radio_metadata_poll_init(void)
{
    if (xTaskCreate(poll_task, "radio_poll", 6144, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

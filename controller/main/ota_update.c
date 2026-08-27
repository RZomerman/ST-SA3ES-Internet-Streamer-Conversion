#include "ota_update.h"

#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"

static const char *TAG = "OTA_UPDATE";

#define OTA_RECV_BUFFER_SIZE 4096

static bool token_is_valid(httpd_req_t *request)
{
    char token[64];
    if (httpd_req_get_hdr_value_str(request, "X-OTA-Token", token, sizeof(token)) != ESP_OK) {
        return false;
    }
    return strcmp(token, CONFIG_LC72130_OTA_TOKEN) == 0;
}

esp_err_t ota_update_handler(httpd_req_t *request)
{
    if (!token_is_valid(request)) {
        httpd_resp_set_status(request, "401 Unauthorized");
        return httpd_resp_sendstr(request, "Missing or incorrect X-OTA-Token header");
    }

    if (request->content_len == 0) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "Firmware body required");
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, "No OTA partition available");
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, "Failed to begin OTA update");
    }

    uint8_t *buffer = malloc(OTA_RECV_BUFFER_SIZE);
    if (buffer == NULL) {
        esp_ota_abort(ota_handle);
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, "Out of memory");
    }

    int remaining = request->content_len;
    while (remaining > 0) {
        int chunk = remaining < OTA_RECV_BUFFER_SIZE ? remaining : OTA_RECV_BUFFER_SIZE;
        int received = httpd_req_recv(request, (char *)buffer, chunk);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (received <= 0) {
            free(buffer);
            esp_ota_abort(ota_handle);
            httpd_resp_set_status(request, "400 Bad Request");
            return httpd_resp_sendstr(request, "Upload interrupted");
        }

        err = esp_ota_write(ota_handle, buffer, received);
        if (err != ESP_OK) {
            free(buffer);
            esp_ota_abort(ota_handle);
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            httpd_resp_set_status(request, "500 Internal Server Error");
            return httpd_resp_sendstr(request, "Failed to write firmware");
        }
        remaining -= received;
    }
    free(buffer);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "Firmware image validation failed");
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, "Failed to set boot partition");
    }

    ESP_LOGI(TAG, "OTA update accepted (%s); rebooting", update_partition->label);
    httpd_resp_sendstr(request, "OTA update accepted, rebooting");
    esp_restart();
    return ESP_OK;
}

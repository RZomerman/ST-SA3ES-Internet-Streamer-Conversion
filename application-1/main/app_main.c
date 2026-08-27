#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_manager.h"

static const char *TAG = "internet_radio";

void app_main(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);

    error = wifi_manager_start();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi manager did not start: %s", esp_err_to_name(error));
    }
}
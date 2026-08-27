#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    WIFI_MANAGER_STOPPED,
    WIFI_MANAGER_CONNECTING,
    WIFI_MANAGER_CONNECTED,
    WIFI_MANAGER_RETRY_WAIT,
} wifi_manager_state_t;

esp_err_t wifi_manager_start(void);
wifi_manager_state_t wifi_manager_get_state(void);
bool wifi_manager_is_connected(void);
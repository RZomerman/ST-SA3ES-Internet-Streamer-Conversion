#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include "esp_http_server.h"

esp_err_t ota_update_handler(httpd_req_t *request);

#endif

#ifndef RADIO_LINK_H
#define RADIO_LINK_H

#include <stdbool.h>

#include "esp_err.h"
#include "rds_output.h"

typedef enum {
    GPIO_OVERRIDE_AUTO = 0,
    GPIO_OVERRIDE_FORCE_ON,
    GPIO_OVERRIDE_FORCE_OFF,
} gpio_override_t;

esp_err_t radio_link_init(void);
esp_err_t radio_link_send_frequency(float frequency_mhz);
esp_err_t radio_link_process_metadata_line(const char *line);
esp_err_t radio_link_get_metadata(radio_metadata_t *metadata_out);
esp_err_t radio_link_get_radio_ip(char *ip_out, size_t ip_out_size);
esp_err_t radio_link_apply_now_playing(const radio_metadata_t *now_playing);

esp_err_t radio_link_set_st_override(gpio_override_t override);
esp_err_t radio_link_set_ast_override(gpio_override_t override);
gpio_override_t radio_link_get_st_override(void);
gpio_override_t radio_link_get_ast_override(void);
const char *radio_link_override_name(gpio_override_t override);

esp_err_t radio_link_set_si_override(gpio_override_t override);
gpio_override_t radio_link_get_si_override(void);
esp_err_t radio_link_get_init_error(void);

#endif
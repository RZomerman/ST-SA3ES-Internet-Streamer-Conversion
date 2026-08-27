#ifndef FREQUENCY_TEST_H
#define FREQUENCY_TEST_H

#include "esp_err.h"

esp_err_t frequency_test_init(void);
esp_err_t frequency_test_simulate(float frequency_mhz);

#endif
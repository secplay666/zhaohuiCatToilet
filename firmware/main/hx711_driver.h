#ifndef HX711_DRIVER_H
#define HX711_DRIVER_H

#include "esp_err.h"
long HX711_get_data();
void HX711_task(void *pvParameters);
esp_err_t HX711_init();

#define HX711_READ_ERROR 0x1FFFFFF

#endif


#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

void measure_moisture(adc_cali_handle_t adc_cal, int *raw, float *value_moisture);
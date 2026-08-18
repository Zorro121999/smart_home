#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

void measure_moisture(adc_oneshot_unit_handle_t adc, adc_channel_t channel, float *value_moisture);
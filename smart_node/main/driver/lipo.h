#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define mV_LSB     0.80
#define V_DEV     1.3

typedef struct {
    uint16_t voltage_mv;
    float soc_percent;
} soc_point_t;

void measure_soc(adc_cali_handle_t adc_cal, int *raw, float *value_soc);
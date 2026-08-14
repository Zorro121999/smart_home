#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define V_LSB     0.00322

typedef struct {
    uint16_t voltage_mv;
    uint8_t soc_percent;
} soc_point_t;

void measure_soc(adc_oneshot_unit_handle_t adc, adc_channel_t channel, float *value_soc);
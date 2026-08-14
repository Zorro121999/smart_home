#include "lipo.h"

static const soc_point_t soc_table[] = {
    { 3200,  0 },
    { 3550,  10 },
    { 3680,  20 },
    { 3720,   30 },
    { 3750,  40 },
    { 3790,  50 },
    { 3810,  60 },
    { 3890,  70 },
    { 3970,  80 },
    { 4050,  90 },
    { 4200, 100 },
};

static void convert_raw_soc(int *raw, float *soc) {
    float voltage;
    voltage = (*raw)*V_LSB*(4.2/3.3);
    if(voltage > soc_table[sizeof(soc_table) / sizeof(soc_table[0]) - 1].voltage_mv) {
        *soc = 100;
    }
    for(size_t i = 0; i < sizeof(soc_table) / sizeof(soc_table[0]); i++) {
        if(voltage < soc_table[i].voltage_mv) {
            *soc = soc_table[i].soc_percent;
        }
    }
}

void measure_soc(adc_oneshot_unit_handle_t adc, adc_channel_t channel, float *value_soc) {
    esp_err_t ret;
    //get raw 10bit value
    int value_raw;
    ret = adc_oneshot_read(adc, channel, &value_raw);
    assert(ret == ESP_OK);
    convert_raw_soc(&value_raw, value_soc);
}
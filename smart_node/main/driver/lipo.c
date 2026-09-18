#include "lipo.h"
#include "esp_log.h"

static const char* TAG = "lipo";

static const soc_point_t soc_table[] = {
    { 3200,  0.0 },
    { 3550,  10.0 },
    { 3680,  20.0 },
    { 3720,   30.0 },
    { 3750,  40.0 },
    { 3790,  50.0 },
    { 3810,  60.0 },
    { 3890,  70.0 },
    { 3970,  80.0 },
    { 4050,  90.0 },
    { 4200, 100.0 },
};

static void convert_raw_soc(int *voltage, float *soc) {
    ESP_LOGE(TAG, "voltage: %d", *voltage);
    *voltage = *voltage * V_DEV;
    if(*voltage > soc_table[10].voltage_mv) {
        *soc = 100;
    }
    else {
        for(size_t i = 0; i < 11; i++) {
            if(*voltage < soc_table[i].voltage_mv) {
                *soc = soc_table[i].soc_percent;
                break;
            }
        }
    }
}

void measure_soc(adc_cali_handle_t adc_cal, int *raw, float *value_soc) {
    esp_err_t ret;
    //get raw 10bit value
    int voltage;
    ret = adc_cali_raw_to_voltage(adc_cal, *raw, &voltage);
    assert(ret == ESP_OK);
    convert_raw_soc(&voltage, value_soc);
}
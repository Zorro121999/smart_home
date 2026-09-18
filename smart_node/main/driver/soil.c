#include "soil.h"


void measure_moisture(adc_cali_handle_t adc_cal, int *raw, float *value_moisture) {
    esp_err_t ret;
    //get raw 10bit value
    int voltage;
    ret = adc_cali_raw_to_voltage(adc_cal, *raw, &voltage);
    assert(ret == ESP_OK);
    *value_moisture = ((float)voltage/3300.0)*100;
}
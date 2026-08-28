#include "soil.h"

void measure_moisture(adc_oneshot_unit_handle_t adc, adc_channel_t channel, float *value_moisture) {
    esp_err_t ret;
    //get raw 10bit value
    int value_raw;
    float voltage;
    ret = adc_oneshot_read(adc, channel, &value_raw);
    assert(ret == ESP_OK);
    *value_moisture = (value_raw/(2^12))*100;
}
#include "bme280.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h>

static const char* TAG = "BME280";

static void transduce_temp(uint8_t *byte_value, uint8_t *calib_value, int32_t *t_fine, float *value);
static void transduce_humidity(uint8_t *byte_value, uint8_t *calib_value1, uint8_t *calib_value2, int32_t *t_fine, float *value);
static void get_temperature_calib(spi_device_handle_t spi, uint8_t *calib_value);
static void get_humidity_calib(spi_device_handle_t spi, uint8_t *calib_value1, uint8_t *calib_value2);
static void compensate_temperature(uint32_t *raw_value, uint8_t *calib_value, int32_t *t_fine, float *value);
static void compensate_humidity(uint32_t *raw_value, uint8_t *calib_value1, uint8_t *calib_value2, int32_t *t_fine, float *value);


void bme280_init(spi_device_handle_t spi, bme_cal_t *calib) {
    //check device ID
    esp_err_t ret;
    uint8_t reg_t[2] = {DEV_ID_REG, 0x00};
    uint8_t id[2];
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = reg_t,
        .rx_buffer = id
    };
    ret = spi_device_polling_transmit(spi, &t); 
    uint8_t dev_id = id[1];
    if(ret != ESP_OK) {
        ESP_LOGE(TAG,
             "Failed SPI communication");     
        esp_restart();    
    }
    if(dev_id != DEV_ID) {
        ESP_LOGE(TAG, "device id invalid");
    }
    ESP_LOGE(TAG, "device ID: %u", dev_id);

    reg_t[0] = ENABLE_HUM_REG;
    reg_t[1] = 0x02;
    memset(&t, 0, sizeof(t));
    t.length = 16;
    t.tx_buffer = &reg_t;
    ret = spi_device_polling_transmit(spi, &t); 
    get_temperature_calib(spi, calib->temp_cal);
    get_humidity_calib(spi, calib->hum_cal1, calib->hum_cal2);

    memset(&t, 0, sizeof(t));
    //enable temperature and humidity reading and set to forced mode
    reg_t[0] = MOD_REG;
    reg_t[1] = 0x21;
    t.length = 16;
    t.tx_buffer = &reg_t;
    ret = spi_device_polling_transmit(spi, &t); 
}

void bme280_measure_temp(spi_device_handle_t spi, uint8_t *temp_cal, int32_t *t_fine, float *value) {
    
    //re-enable temperature and humidity reading and set to forced mode
    esp_err_t ret;
    uint8_t config_reg[2];
    config_reg[0] = MOD_REG;
    config_reg[1] = 0x21;
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = config_reg,
    };
    ret = spi_device_polling_transmit(spi, &t);

    memset(&t, 0, sizeof(t));
    uint8_t reg_t[9] = {PRES_REG, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t reg_r[9];
    t.length = 72;
    t.tx_buffer = reg_t;
    t.rx_buffer = reg_r;
    ret = spi_device_polling_transmit(spi, &t); 
    uint8_t temp[3];
    temp[0] = reg_r[4];
    temp[1] = reg_r[5];
    temp[2] = reg_r[6];
    transduce_temp(temp, temp_cal, t_fine, value);
}

void bme280_measure_humidity(spi_device_handle_t spi, uint8_t *hum_cal1, uint8_t *hum_cal2, int32_t *t_fine, float *value) {
    esp_err_t ret;
    uint8_t config_reg[2];
    config_reg[0] = MOD_REG;
    config_reg[1] = 0x21;
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = config_reg,
    };
    ret = spi_device_polling_transmit(spi, &t);

    uint8_t reg_t[9] = {PRES_REG, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t reg_r[9];
    t.length = 72;
    t.tx_buffer = reg_t;
    t.rx_buffer = reg_r;
    ret = spi_device_polling_transmit(spi, &t); 
    uint8_t hum[2];
    hum[0] = reg_r[7];
    hum[1] = reg_r[8];
    transduce_humidity(hum, hum_cal1, hum_cal2, t_fine, value);
}

static void transduce_temp(uint8_t *byte_value, uint8_t *calib_value, int32_t *t_fine, float *value) {
    uint32_t raw_value = 0;
    raw_value = ((uint32_t)byte_value[0] << 12) |
    ((uint32_t)byte_value[1] << 4)  |
    ((uint32_t)byte_value[2] >> 4);
    compensate_temperature(&raw_value, calib_value, t_fine, value);
}

static void transduce_humidity(uint8_t *byte_value, uint8_t *calib_value1, uint8_t *calib_value2, int32_t *t_fine, float *value) {
    uint32_t raw_value = 0;
    raw_value = ((uint32_t)byte_value[0] << 8) |
    ((uint32_t)byte_value[1] << 0);
    compensate_humidity(&raw_value, calib_value1, calib_value2, t_fine, value);
}

static void get_temperature_calib(spi_device_handle_t spi, uint8_t *calib_value) {
    esp_err_t ret;
    uint8_t reg_t[7] = {TEMP_COMP_REG, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    spi_transaction_t t = {
        .length = 56,
        .tx_buffer = reg_t,
        .rx_buffer = calib_value
    };
    ret = spi_device_polling_transmit(spi, &t);
}

static void get_humidity_calib(spi_device_handle_t spi, uint8_t *calib_value1, uint8_t *calib_value2) {
    esp_err_t ret;
    uint8_t reg_t[2] = {HUM_COMP_REG1, 0x00};
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = reg_t,
        .rx_buffer = calib_value1
    };
    ret = spi_device_polling_transmit(spi, &t);
    memset(&t, 0, sizeof(t));
    uint8_t reg_t2[8] = {HUM_COMP_REG2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    t.length = 64;
    t.tx_buffer = reg_t2;
    t.rx_buffer = calib_value2;
    ret = spi_device_polling_transmit(spi, &t);
}

static void compensate_temperature(uint32_t *raw_value, uint8_t *calib_value, int32_t *t_fine, float *value) {
    int32_t var1;
    int32_t var2;
    int32_t temperature;
    int32_t temperature_min = -4000;
    int32_t temperature_max = 8500;
    uint16_t dig_t1 = ((uint16_t)calib_value[1]) | ((uint16_t)calib_value[2] << 8);
    int16_t dig_t2 = ((int16_t)calib_value[3]) | ((int16_t)calib_value[4] << 8);
    int16_t dig_t3 = ((int16_t)calib_value[5]) | ((int16_t)calib_value[6] << 8);

    var1 = (int32_t)((*raw_value / 8) - ((int32_t)dig_t1 * 2));
    var1 = (var1 * ((int32_t)dig_t2)) / 2048;
    var2 = (int32_t)((*raw_value / 16) - ((int32_t)dig_t1));
    var2 = (((var2 * var2) / 4096) * ((int32_t)dig_t3)) / 16384;
    *t_fine = var1 + var2;
    temperature = (*t_fine * 5 + 128) / 256;

    if (temperature < temperature_min)
    {
        temperature = temperature_min;
    }
    else if (temperature > temperature_max)
    {
        temperature = temperature_max;
    }
    *value = (float)(temperature/100.0f);
}

static void compensate_humidity(uint32_t *raw_value, uint8_t *calib_value1, uint8_t *calib_value2, int32_t *t_fine, float *value) {
    double humidity;
    double humidity_min = 0.0;
    double humidity_max = 100.0;
    double var1;
    double var2;
    double var3;
    double var4;
    double var5;
    double var6;
    uint8_t dig_h1 = calib_value1[1];
    int16_t dig_h2 = (int16_t)((uint16_t)calib_value2[1]) | ((uint16_t)calib_value2[2] << 8);
    uint8_t dig_h3 = calib_value2[3];
    int16_t dig_h4 = ((int16_t)calib_value2[4] << 4) |  (calib_value2[5] & 0x0F);
    int16_t dig_h5 = ((int16_t)calib_value2[6] << 4) | (calib_value2[5] >> 4);
    int8_t dig_h6 = (int8_t)calib_value2[7];

    var1 = ((double)*t_fine) - 76800.0;
    var2 = (((double)dig_h4) * 64.0 + (((double)dig_h5) / 16384.0) * var1);
    var3 = *raw_value - var2;
    var4 = ((double)dig_h2) / 65536.0;
    var5 = (1.0 + (((double)dig_h3) / 67108864.0) * var1);
    var6 = 1.0 + (((double)dig_h6) / 67108864.0) * var1 * var5;
    var6 = var3 * var4 * (var5 * var6);
    humidity = var6 * (1.0 - ((double)dig_h1) * var6 / 524288.0);

    if (humidity > humidity_max)
    {
        humidity = humidity_max;
    }
    else if (humidity < humidity_min)
    {
        humidity = humidity_min;
    }
    *value = (float)humidity;
}
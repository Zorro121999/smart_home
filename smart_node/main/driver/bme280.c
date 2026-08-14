#include "bme280.h"
#include "driver/spi_master.h"

static const char* TAG = "BME280";

void bme280_init(spi_device_handle_t spi) {
    //check device ID
    esp_err_t ret;
    uint8_t reg_t[2] = {DEV_ID_REG, 0x00};
    uint8_t id[1];
    spi_transaction_t t = {
        .length = 16,
        .rxlength = 8,
        .tx_buffer = &reg_t,
        .rx_buffer = id
    };
    ret = spi_device_polling_transmit(spi, &t); 
    if(*(uint8_t*)(t.rx_buffer) != DEV_ID) {
        ESP_LOGE(TAG, "device id invalid");
    }

    //enable temperature and humidity reading and set to forced mode
    reg_t[0] = MOD_REG;
    reg_t[1] = 0x21;
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = &reg_t
    };
    ret = spi_device_polling_transmit(spi, &t); 
    reg_t[0] = ENABLE_HUM_REG;
    reg_t[1] = 0x01;
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = &reg_t
    };
    ret = spi_device_polling_transmit(spi, &t); 
}

void bme280_measure_temp(spi_device_handle_t spi, float *value) {
    esp_err_t ret;
    uint8_t reg_t[3] = {TEMP_REG, 0x00, 0x00};
    uint8_t temp[2];
    spi_transaction_t t = {
        .length = 24,
        .rxlength = 16,
        .tx_buffer = &reg_t,
        .rx_buffer = temp
    };
    ret = spi_device_polling_transmit(spi, &t); 
}

void bme280_measure_humidity(spi_device_handle_t spi, float *value) {
    esp_err_t ret;
    uint8_t reg_t[3] = {HUM_REG, 0x00, 0x00};
    uint8_t hum[2];
    spi_transaction_t t = {
        .length = 24,
        .rxlength = 16,
        .tx_buffer = &reg_t,
        .rx_buffer = hum
    };
    ret = spi_device_polling_transmit(spi, &t); 
}
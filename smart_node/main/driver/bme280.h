#include "driver/spi_master.h"

#define DEV_ID_REG  0xD0
#define MOD_REG  0x74
#define ENABLE_HUM_REG 0x72
#define TEMP_REG  0xFA
#define HUM_REG  0xFD

#define DEV_ID 0x60

void bme280_init(spi_device_handle_t spi);
void bme280_measure_temp(spi_device_handle_t spi, float *value);
void bme280_measure_humidity(spi_device_handle_t spi, float *value);
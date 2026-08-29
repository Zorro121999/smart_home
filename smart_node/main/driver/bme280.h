#include "driver/spi_master.h"

#define DEV_ID_REG  0xD0
#define MOD_REG  0x74
#define ENABLE_HUM_REG 0x72
#define TEMP_REG  0xFA
#define HUM_REG  0xFD
#define PRES_REG 0xF7
#define TEMP_COMP_REG  0x88
#define HUM_COMP_REG1  0xA1
#define HUM_COMP_REG2  0xE1

#define DEV_ID 0x60

typedef struct {
    uint8_t temp_cal[7];
    uint8_t hum_cal1[2];
    uint8_t hum_cal2[8];
    int32_t t_fine;
} bme_cal_t;

void bme280_init(spi_device_handle_t spi, bme_cal_t *calib);
void bme280_measure_temp(spi_device_handle_t spi, uint8_t *temp_cal, int32_t *t_fine, float *value);
void bme280_measure_humidity(spi_device_handle_t spi, uint8_t *hum_cal1, uint8_t *hum_cal2, int32_t *t_fine, float *value);
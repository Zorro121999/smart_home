/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/bme280.h"
#include "driver/lipo.h"
#include "driver/soil.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#define PIN_NUM_MISO 5
#define PIN_NUM_MOSI 0
#define PIN_NUM_CLK  4
#define PIN_NUM_CS   1

#define BME280_HOST    SPI2_HOST

TaskHandle_t sensor_task_handle;
TimerHandle_t timer;
QueueHandle_t sensor_queue;

typedef struct {
    float moisture;
    float soc;
    float humidity;
    float temp;
} sensor_data_t;

spi_device_handle_t spi;
spi_bus_config_t buscfg = {
    .miso_io_num = PIN_NUM_MISO,
    .mosi_io_num = PIN_NUM_MOSI,
    .sclk_io_num = PIN_NUM_CLK,
};
spi_device_interface_config_t devcfg = {
    .mode = 0,
    .spics_io_num = PIN_NUM_CS
};

adc_oneshot_unit_handle_t adc1_handle;
adc_oneshot_unit_init_cfg_t init_config1 = {
    .unit_id = ADC_UNIT_1,
};
adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_0,
        .bitwidth = ADC_BITWIDTH_10,
    };


void esp_zb_task(void *arg) {
    sensor_data_t data;
    while (1) {
        // Warten, bis Sensor Daten geliefert hat
        xQueueReceive(
            sensor_queue,
            &data,
            portMAX_DELAY
        );
    }
}

void meas_task(void *arg) {
    sensor_data_t data;
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        data.moisture = measure_moisture(adc1_handle, ADC_CHANNEL_1);
        measure_soc(adc1_handle, ADC_CHANNEL_2, &(data.soc));
        bme280_measure_temp(spi,&(data.temp));
        bme280_measure_humidity(spi,&(data.humidity));
        xQueueSend(sensor_queue, &data, portMAX_DELAY);      
    }
}

void timer_callback(TimerHandle_t timer){
    xTaskNotificationGive(sensor_task_handle);
}

void app_main(void)
{
    //esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    //esp_zb_init(&zb_nwk_cfg);
    esp_err_t ret;

    //Initialize the SPI bus
    ret = spi_bus_initialize(BME280_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);
    //Attach the BME280 to the SPI bus
    ret = spi_bus_add_device(BME280_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);
    bme280_init(spi);

    adc_oneshot_new_unit(&init_config1, &adc1_handle);
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_1, &config);
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_2, &config);

    
    timer = xTimerCreate("sensor_timer",
        pdMS_TO_TICKS(10000), // 10 Sekunden
        pdTRUE,                // Auto-reload
        NULL,
        timer_callback
    );

    xTaskCreate(esp_zb_task, "zigbee_task", 4096, NULL, 5, NULL);
    xTaskCreate(meas_task, "measurement_task", 4096, NULL, 5, sensor_task_handle);

    ret = esp_sleep_enable_timer_wakeup(10000000LL);
    while(1) {

        esp_light_sleep_start();

    }

}   


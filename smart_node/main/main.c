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
#include "driver/sensor_cluster.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "esp_zigbee.h"

#define PIN_NUM_MISO 5
#define PIN_NUM_MOSI 0
#define PIN_NUM_CLK  4
#define PIN_NUM_CS   1

#define BME280_HOST    SPI2_HOST

TaskHandle_t sensor_task_handle;
TimerHandle_t timer;
QueueHandle_t sensor_queue;

bool device_connected = false;

typedef struct {
    float moisture;
    float soc;
    float humidity;
    float temp;
} sensor_data_t;

static float temperature = 0;
static float humidity = 0;
static float soil_moisture = 0;
static float soc = 0;

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

esp_err_t ret;
esp_zigbee_config_t zigbee_config = {
.device_config = {
    .device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE,
    .install_code_policy = false,

    .zed_config = {
        .ed_timeout = EZB_NWK_ED_TIMEOUT_2MIN,
        .keep_alive = 10000,
    },
}
};

static const char* TAG = "main";

void zigbee_send_measurement_callback(sensor_data_t *sensor_data) {
    ezb_zcl_status_t state;
    state = ezb_zcl_set_attr_value(ENDPOINT0, EZB_ZCL_CLUSTER_CLIENT, EZB_ZCL_CLUSTER_CLIENT,ATTR_TEMPERATURE_ID, EZB_ZCL_STD_MANUF_CODE, &(sensor_data->temp), false);
    state = ezb_zcl_set_attr_value(ENDPOINT0, EZB_ZCL_CLUSTER_CLIENT, EZB_ZCL_CLUSTER_CLIENT,ATTR_HUMIDITY_ID, EZB_ZCL_STD_MANUF_CODE, &(sensor_data->humidity), false);
    state = ezb_zcl_set_attr_value(ENDPOINT0, EZB_ZCL_CLUSTER_CLIENT, EZB_ZCL_CLUSTER_CLIENT,ATTR_SOIL_MOISTURE_ID, EZB_ZCL_STD_MANUF_CODE, &(sensor_data->moisture), false);
    state = ezb_zcl_set_attr_value(ENDPOINT0, EZB_ZCL_CLUSTER_CLIENT, EZB_ZCL_CLUSTER_CLIENT,ATTR_SOC_ID, EZB_ZCL_STD_MANUF_CODE, &(sensor_data->soc), false);
}



void esp_zb_task(void *arg) {
    ret = esp_zigbee_init(&zigbee_config);
    ret = esp_zigbee_start(true);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG,
             "Failed starting zigbee stack");     
        esp_restart();    
    }
    esp_zigbee_launch_mainloop();
    ezb_zcl_custom_cluster_config_t sensor_cluster_config = {
        .cluster_id = SENSOR_CLUSTER_ID,
        .init_func = NULL,
        .deinit_func = NULL
    };
    ezb_af_ep_config_t sensor_endpoint_config = {
        .ep_id = ENDPOINT0,
        .app_profile_id = 0x0104U,
        .app_device_id = 1,
        .app_device_version = 1
    };

    ezb_af_device_desc_t sensor_device = ezb_af_create_device_desc();
    ezb_af_ep_desc_t sensor_endpoint = ezb_af_create_endpoint_desc(&sensor_endpoint_config);
    ezb_zcl_cluster_desc_t sensor_cluster = ezb_zcl_custom_create_cluster_desc(&sensor_cluster_config, EZB_ZCL_CLUSTER_CLIENT);
    ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_TEMPERATURE_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_WRITE, &temperature);
    ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_HUMIDITY_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_WRITE, &humidity);
    ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_SOIL_MOISTURE_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_WRITE, &soil_moisture);
    ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_SOC_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_WRITE, &soc);

    sensor_data_t data;
    // while (1) {
    //     // Warten, bis Sensor Daten geliefert hat
    //     xQueueReceive(
    //         sensor_queue,
    //         &data,
    //         portMAX_DELAY
    //     );
    // }
}

void meas_task(void *arg) {
    sensor_data_t data;
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if(device_connected) {
            data.moisture = measure_moisture(adc1_handle, ADC_CHANNEL_1);
            measure_soc(adc1_handle, ADC_CHANNEL_2, &(data.soc));
            bme280_measure_temp(spi,&(data.temp));
            bme280_measure_humidity(spi,&(data.humidity));
            //xQueueSend(sensor_queue, &data, portMAX_DELAY);     
            esp_zigbee_task_queue_post(
            zigbee_send_measurement_callback,
            &data); 
        }
    }
}

void timer_callback(TimerHandle_t timer){
    xTaskNotificationGive(sensor_task_handle);
}

void zigbee_signal_callback(ezb_app_signal_t *signal_type) {
    ezb_app_signal_type_t signal = ezb_app_signal_get_type(signal_type);
    ezb_bdb_comm_status_t status = ezb_bdb_get_commissioning_status();
    if (signal == EZB_BDB_SIGNAL_DEVICE_REBOOT && device_connected == false) {
        if(status == EZB_BDB_STATUS_SUCCESS) {
            device_connected = true;
        }
    else if(signal == EZB_BDB_SIGNAL_DEVICE_REBOOT && device_connected == true) {
        device_connected = false;
        }
    }
}

void app_main(void)
{
    esp_err_t ret;

    //Initialize the SPI bus
    ret = spi_bus_initialize(BME280_HOST, &buscfg, SPI_DMA_DISABLED);
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

    ezb_app_signal_add_handler(zigbee_signal_callback);

    xTaskCreate(esp_zb_task, "zigbee_task", 4096, NULL, 10, NULL);
    xTaskCreate(meas_task, "measurement_task", 4096, NULL, 5, sensor_task_handle);

}   


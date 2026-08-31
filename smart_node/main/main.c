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
#include "esp_log.h"
#include "nvs_flash.h"

#define PIN_NUM_MISO 0
#define PIN_NUM_MOSI 5
#define PIN_NUM_CLK  4
#define PIN_NUM_CS   1

#define BME280_HOST    SPI2_HOST

bool zigbee_signal_callback(const ezb_app_signal_t *signal_type);

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

sensor_data_t data;


bme_cal_t bme_cal;

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
    .clock_speed_hz = 1 * 1000 * 1000,
    .spics_io_num = PIN_NUM_CS,
    .queue_size = 1
};

adc_oneshot_unit_handle_t adc1_handle;
adc_oneshot_unit_init_cfg_t init_config1 = {
    .unit_id = ADC_UNIT_1,
};
adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_0,
        .bitwidth = ADC_BITWIDTH_12,
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
},
.platform_config = {
    .storage_partition_name = "zb_storage",
    .radio_config = {
        .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE,
    },
}
};

uint32_t channel_mask = (1UL << 20);

ezb_zcl_custom_cluster_config_t sensor_cluster_config = {
        .cluster_id = SENSOR_CLUSTER_ID,
        .init_func = NULL,
        .deinit_func = NULL
    };
ezb_af_ep_config_t sensor_endpoint_config = {
    .ep_id = ENDPOINT0,
    .app_profile_id = 0x0104U,
    .app_device_id = LIVING_ROOM,
    .app_device_version = 1
};

static const char* TAG = "main";

void zigbee_send_measurement_callback(void *ctx) {
    sensor_data_t *sensor_data = (sensor_data_t*)ctx;
    ezb_zcl_status_t state;
    state = ezb_zcl_set_attr_value(ENDPOINT0, SENSOR_CLUSTER_ID, EZB_ZCL_CLUSTER_SERVER,ATTR_TEMPERATURE_ID, EZB_ZCL_STD_MANUF_CODE, &(sensor_data->temp), false);
    state = ezb_zcl_set_attr_value(ENDPOINT0, SENSOR_CLUSTER_ID, EZB_ZCL_CLUSTER_SERVER,ATTR_HUMIDITY_ID, EZB_ZCL_STD_MANUF_CODE, &(sensor_data->humidity), false);
    state = ezb_zcl_set_attr_value(ENDPOINT0, SENSOR_CLUSTER_ID, EZB_ZCL_CLUSTER_SERVER,ATTR_SOIL_MOISTURE_ID, EZB_ZCL_STD_MANUF_CODE, &(sensor_data->moisture), false);
    state = ezb_zcl_set_attr_value(ENDPOINT0, SENSOR_CLUSTER_ID, EZB_ZCL_CLUSTER_SERVER,ATTR_SOC_ID, EZB_ZCL_STD_MANUF_CODE, &(sensor_data->soc), false);
}


void esp_zb_task(void *arg) {
    ret = esp_zigbee_init(&zigbee_config);

    //bool lock = esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000));
    ESP_LOGE("ZIGBEE", "ESP Zigbee SDK: %s",
         esp_zigbee_get_version_string());
    
    ezb_af_device_desc_t sensor_device = ezb_af_create_device_desc();
    ezb_af_ep_desc_t sensor_endpoint = ezb_af_create_endpoint_desc(&sensor_endpoint_config);
    ezb_zcl_cluster_desc_t sensor_cluster = ezb_zcl_custom_create_cluster_desc(&sensor_cluster_config, EZB_ZCL_CLUSTER_SERVER);
    ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_TEMPERATURE_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_WRITE, &temperature);
    ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_HUMIDITY_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_WRITE, &humidity);
    ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_SOIL_MOISTURE_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_WRITE, &soil_moisture);
    ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_SOC_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_WRITE, &soc);

    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(channel_mask));
    ezb_app_signal_add_handler(zigbee_signal_callback);
  
    ret = esp_zigbee_start(false);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG,
             "Failed starting zigbee stack");     
        esp_restart();    
    }
    // if (!lock) {
    //     ESP_LOGE("ZB", "Lock was NOT acquired!");
    //     return;
    // }
    //esp_zigbee_lock_release();
    ESP_LOGE(TAG, "hello_zb");

    esp_zigbee_launch_mainloop();
}

void meas_task(void *arg) { 
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        //if(device_connected) {
            measure_moisture(adc1_handle, ADC_CHANNEL_1, &(data.moisture));
            measure_soc(adc1_handle, ADC_CHANNEL_2, &(data.soc));
            bme280_measure_temp(spi, (&(bme_cal))->temp_cal, &(bme_cal.t_fine), &(data.temp));
            bme280_measure_humidity(spi, (&(bme_cal))->hum_cal1, (&(bme_cal))->hum_cal2, &(bme_cal.t_fine), &(data.humidity));   
            ESP_LOGE(TAG, "temp: %.2f",data.temp);
            ESP_LOGE(TAG, "hum: %.2f",data.humidity);
            ret = esp_zigbee_task_queue_post(
            &zigbee_send_measurement_callback,
            &data); 
       // }
    }
}

void timer_callback(TimerHandle_t timer){
    xTaskNotifyGive(sensor_task_handle);
}

bool zigbee_signal_callback(const ezb_app_signal_t *app_signal) {
    ezb_app_signal_type_t signal_type = ezb_app_signal_get_type(app_signal);

    switch (signal_type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;
    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            //ESP_LOGI(TAG, "Deferred driver initialization %s", deferred_driver_init() ? "failed" : "successful");
            ESP_LOGI(TAG, "Device started up in%s factory-reset mode", ezb_bdb_is_factory_new() ? "" : " non");
            if (ezb_bdb_is_factory_new()) {
                ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device reboot");
            }
        } else {
            ESP_LOGW(TAG, "%s failed with status(0x%02x), please retry", ezb_app_signal_to_string(signal_type), status);
            //alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning, EZB_BDB_MODE_INITIALIZATION, 1000);
        }
    } break;
    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ezb_extpanid_t extended_pan_id;
            ezb_shortaddr_t address;
            address = ezb_nwk_get_short_address();
            ESP_LOGE(TAG, "end node network address:%" PRIu16, address);
            ezb_nwk_get_extended_panid(&extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully: PAN ID(0x%04hx, EXT: 0x%llx), Channel(%d), Short Address(0x%04hx)",
                     ezb_nwk_get_panid(), extended_pan_id.u64, ezb_nwk_get_current_channel(), ezb_nwk_get_short_address());
            //zdo_find_ha_light_device();
        } else {
            ESP_LOGW(TAG, "Failed to join network with status(0x%02x)", status);
            //alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning, EZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
    } break;
    case EZB_ZDO_SIGNAL_LEAVE: {
        const ezb_zdo_signal_leave_params_t *leave_params = ezb_app_signal_get_params(app_signal);
        ESP_LOGI(TAG, "Left network successfully with type(0x%02x)", leave_params->leave_type);
    } break;
    case EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
        uint8_t duration = *(uint8_t *)ezb_app_signal_get_params(app_signal);
        if (duration) {
            ESP_LOGI(TAG, "Network(0x%04hx) is open for %d seconds", ezb_nwk_get_panid(), duration);
        } else {
            ESP_LOGW(TAG, "Network(0x%04hx) closed, devices joining not allowed.", ezb_nwk_get_panid());
        }
    } break;
    default:
        ESP_LOGI(TAG, "Zigbee APP Signal: %s(type: 0x%02x)", ezb_app_signal_to_string(signal_type), signal_type);
        break;
    }
    return true;
}

static void esp_zigbee_zcl_core_action_handler(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    switch (callback_id) {
    case EZB_ZCL_CORE_DEFAULT_RSP_CB_ID: {
        ezb_zcl_cmd_default_rsp_message_t *default_rsp = (ezb_zcl_cmd_default_rsp_message_t *)message;
        ESP_LOGI(TAG, "Received ZCL Default Response with status(0x%02x)", default_rsp->in.status_code);
    } break;
    default:
        ESP_LOGW(TAG, "ZCL Core Action: ID(0x%04lx)", callback_id);
        break;
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(
        nvs_flash_init_partition("zb_storage")
    );
    //Initialize the SPI bus
    
    ret = spi_bus_initialize(BME280_HOST, &buscfg, SPI_DMA_DISABLED);
    ESP_ERROR_CHECK(ret);
    //Attach the BME280 to the SPI bus
    ret = spi_bus_add_device(BME280_HOST, &devcfg, &spi);
    
    ESP_LOGE(TAG, "hello");
    bme280_init(spi, &bme_cal);

    adc_oneshot_new_unit(&init_config1, &adc1_handle);
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_1, &config);
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_2, &config);

    
    timer = xTimerCreate("sensor_timer",
        pdMS_TO_TICKS(10000), // 10 Sekunden
        pdTRUE,                // Auto-reload
        NULL,
        timer_callback
    );
    xTimerStart(timer, pdMS_TO_TICKS(1000));

    

    xTaskCreate(esp_zb_task, "zigbee_task", 4096, NULL, 10, NULL);
    xTaskCreate(meas_task, "measurement_task", 4096, NULL, 5, &sensor_task_handle);
}   
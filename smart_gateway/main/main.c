/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "esp_zigbee.h"
#include "driver/sensor_cluster.h"
#include "esp_log.h"

static const char *TAG = "main";

typedef struct {
    float moisture;
    float soc;
    float humidity;
    float temp;
} sensor_data_t;

typedef struct {
    uint16_t sensor_id;
    uint16_t network_addr;
    uint64_t network_addr_ieee;
    sensor_data_t data;
} joined_nodes_id_t;
uint8_t nodes_index;
uint64_t new_node_addr_long;

joined_nodes_id_t joined_nodes_id[MAX_NODES];
sensor_data_t data[MAX_NODES];


esp_err_t ret;
esp_zigbee_config_t zigbee_config = {
.device_config = {
    .device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE,
    .install_code_policy = false,

    .zczr_config = {
        .max_children = 100
    },
}
};

static void rearrange_joined_nodes_array(uint8_t index) {
    for(uint8_t i = index; i<MAX_NODES-1; i++) {
        joined_nodes_id[i] = joined_nodes_id[i+1];
    }
    joined_nodes_id[MAX_NODES-1] = (joined_nodes_id_t){0};
    nodes_index--;
}

static ezb_err_t sensor_node_report_config(uint8_t ep_id, uint16_t network_addr) {
    ezb_err_t ret = ESP_OK;
    if (ep_id == SENSOR_EP) {
        float temp_change = 0.1;
        float humidity_change = 0.1;
        float moisture_change = 0.1;
        float soc_change = 10;
        ezb_zcl_config_report_record_t records[] = {
            {
                .direction = EZB_ZCL_REPORTING_SEND,
                .attr_id = ATTR_TEMPERATURE_ID,
                .client.attr_type = EZB_ZCL_ATTR_TYPE_SINGLE,
                .client.min_interval = 10,
                .client.max_interval = 60,
                .client.reportable_change = {.f32 = temp_change},
            },
            {
                .direction = EZB_ZCL_REPORTING_SEND,
                .attr_id = ATTR_HUMIDITY_ID,
                .client.attr_type = EZB_ZCL_ATTR_TYPE_SINGLE,
                .client.min_interval = 10,
                .client.max_interval = 60,
                .client.reportable_change = {.f32 = humidity_change},
            },
            {
                .direction = EZB_ZCL_REPORTING_SEND,
                .attr_id = ATTR_SOIL_MOISTURE_ID,
                .client.attr_type = EZB_ZCL_ATTR_TYPE_SINGLE,
                .client.min_interval = 10,
                .client.max_interval = 60,
                .client.reportable_change = {.f32 = moisture_change},
            },
            {
                .direction = EZB_ZCL_REPORTING_SEND,
                .attr_id = ATTR_SOC_ID,
                .client.attr_type = EZB_ZCL_ATTR_TYPE_SINGLE,
                .client.min_interval = 10,
                .client.max_interval = 60,
                .client.reportable_change = {.f32 = soc_change},
            },
        };
        ezb_zcl_config_report_cmd_t report_cmd = {
        .cmd_ctrl =
            {
                .dst_addr =
                    {
                        .addr_mode = EZB_ADDR_MODE_SHORT,
                        .u.short_addr = network_addr,
                    },
                .src_ep = ep_id,
                .dst_ep = ep_id,
                .cluster_id = SENSOR_CLUSTER_ID,
            },
        };
        report_cmd.payload.record_number = sizeof(records) / sizeof(ezb_zcl_config_report_record_t);
        report_cmd.payload.record_field = records;
        esp_zigbee_lock_acquire(portMAX_DELAY);
        ret = ezb_zcl_config_report_cmd_req(&report_cmd);
        esp_zigbee_lock_release();
    }
    return ret;
}

static void simple_desc_callback(const ezb_zdo_simple_desc_req_result_t *result, void *user_ctx) {
    uint64_t *long_addr = (uint64_t *)user_ctx;
    joined_nodes_id[nodes_index].sensor_id = result->rsp->desc.app_device_id;
    joined_nodes_id[nodes_index].network_addr = result->rsp->nwk_addr_of_interest;
    joined_nodes_id[nodes_index].network_addr_ieee = *long_addr;
    nodes_index++;
    sensor_node_report_config(result->rsp->desc.ep_id, result->rsp->nwk_addr_of_interest);
}

bool node_signal_callback(const ezb_app_signal_t *signal_type) {
    ezb_app_signal_type_t signal = ezb_app_signal_get_type(signal_type);
    if (signal == EZB_ZDO_SIGNAL_DEVICE_ANNCE) {
        const ezb_zdo_signal_device_annce_params_t *node_info_joined = (const ezb_zdo_signal_device_annce_params_t *)ezb_app_signal_get_params(signal_type);
        new_node_addr_long = node_info_joined->device_addr.u64;
        const ezb_zdo_simple_desc_req_t node_request = {
            .dst_nwk_addr = node_info_joined->short_addr,
            .field =
                {
                    .nwk_addr_of_interest = node_info_joined->short_addr,
                    .endpoint = SENSOR_EP,
                },
            .cb = simple_desc_callback,
            .user_ctx = &new_node_addr_long
        };
        for(uint8_t i=0; i<nodes_index; i++) {
            if (joined_nodes_id[i].network_addr_ieee == node_info_joined->device_addr.u64) {
                return true;
            }
        }
        //only ask for device ID if not already acquired deviceID perviously
        ezb_zdo_simple_desc_req(&node_request);
        return true;
    }
    else if (signal == EZB_ZDO_SIGNAL_DEVICE_UNAVAILABLE) {
        const ezb_zdo_signal_device_unavailable_params_t *node_info_unavailable = (const ezb_zdo_signal_device_unavailable_params_t *)ezb_app_signal_get_params(signal_type);
        for(uint8_t i=0; i<nodes_index; i++) {
            if (joined_nodes_id[i].network_addr_ieee == node_info_unavailable->device_addr.u64) {
                rearrange_joined_nodes_array(i);
                return true;
            }
        }
    }
    return false;   
}
    

static void zigbee_zcl_callback(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    switch (callback_id) {

        case EZB_ZCL_CORE_READ_ATTR_RSP_CB_ID:
        {
            ezb_zcl_cmd_read_attr_rsp_message_t *rsp =
                (ezb_zcl_cmd_read_attr_rsp_message_t *)message;

            if (rsp->info.status != EZB_ZCL_STATUS_SUCCESS) {
                // Read request ist fehlgeschlagen
                return;
            }

            ezb_zcl_read_attr_rsp_variable_t *attr =
                rsp->in.variables;
            uint16_t cluster_id = rsp->in.header->cluster_id;

            if (cluster_id == SENSOR_CLUSTER_ID) {
                while (attr != NULL) {

                    printf("Attribute ID: 0x%04x\n", attr->attr_id);
                    printf("Type: 0x%02x\n", attr->attr_type);

                    //iterate through all the connected nodes to determine the end device that the message belongs to
                    for(int i=0; i<nodes_index; i++) {
                        if (rsp->in.header->src_addr.u.extended_addr.u64 == joined_nodes_id[i].network_addr_ieee) {
                            if (attr->attr_id == ATTR_TEMPERATURE_ID) {
                                joined_nodes_id[i].data.temp = *(float *)attr->attr_value;
                                ESP_LOGE(TAG, "receiver temp: %.2f", joined_nodes_id[i].data.temp);
                            }
                            else if (attr->attr_id == ATTR_HUMIDITY_ID) {
                                joined_nodes_id[i].data.humidity = *(float *)attr->attr_value;
                                ESP_LOGE(TAG, "receiver temp: %.2f", joined_nodes_id[i].data.humidity);
                            }
                            else if (attr->attr_id == ATTR_SOIL_MOISTURE_ID) {
                                joined_nodes_id[i].data.moisture = *(float *)attr->attr_value;
                                ESP_LOGE(TAG,"receiver temp: %.2f", joined_nodes_id[i].data.moisture);
                            }
                            else if (attr->attr_id == ATTR_SOC_ID) {
                                joined_nodes_id[i].data.soc = *(float *)attr->attr_value;
                                ESP_LOGE(TAG,"receiver temp: %.2f", joined_nodes_id[i].data.soc);
                            }
                        }
                    }
                    attr = attr->next;
                }
            }

            break;
        }

        default:
            break;
    }
}

void esp_zb_task(void *arg) {
    // ezb_zcl_custom_cluster_config_t sensor_cluster_config = {
    //     .cluster_id = SENSOR_CLUSTER_ID,
    //     .init_func = NULL,
    //     .deinit_func = NULL
    // };
    // ezb_af_ep_config_t sensor_endpoint_config = {
    //     .ep_id = ENDPOINT0,
    //     .app_profile_id = 0x0104U,
    //     .app_device_id = 1,
    //     .app_device_version = 1
    // };

    bool lock = esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000));
    ret = esp_zigbee_start(true);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG,
             "Failed starting zigbee stack");     
        esp_restart();    
    }
    esp_zigbee_lock_release();
    esp_zigbee_launch_mainloop();
}

void app_main(void)
{
    bool lock = esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000));
    ret = esp_zigbee_init(&zigbee_config);
    ezb_app_signal_add_handler(node_signal_callback);
    ezb_zcl_core_action_handler_register(zigbee_zcl_callback);
    esp_zigbee_lock_release();

    xTaskCreate(esp_zb_task, "zigbee_task", 4096, NULL, 10, NULL);
      
}

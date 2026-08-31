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
#include "nvs_flash.h"
#include <inttypes.h>

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
uint8_t nodes_index = 0;
uint64_t new_node_addr_long;

joined_nodes_id_t joined_nodes_id[MAX_NODES];
sensor_data_t data[MAX_NODES];


esp_err_t ret;
esp_zigbee_config_t zigbee_config = {
.device_config = {
    .device_type = EZB_NWK_DEVICE_TYPE_COORDINATOR,
    .install_code_policy = false,

    .zczr_config = {
        .max_children = 100
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
        float temp_change = 0.01;
        float humidity_change = 0.01;
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
    ESP_LOGE(TAG, "set node report config");
    uint64_t *long_addr = (uint64_t *)user_ctx;
    joined_nodes_id[nodes_index].sensor_id = result->rsp->desc.app_device_id;
    joined_nodes_id[nodes_index].network_addr = result->rsp->nwk_addr_of_interest;
    joined_nodes_id[nodes_index].network_addr_ieee = *long_addr;
    ESP_LOGE(TAG, "end node device ID:%" PRIu16, joined_nodes_id[nodes_index].sensor_id);
    ESP_LOGE(TAG, "end node network address:%" PRIu16, joined_nodes_id[nodes_index].network_addr);
    ESP_LOGE(TAG, "node index:%" PRIu8, nodes_index);
    nodes_index++;
    sensor_node_report_config(result->rsp->desc.ep_id, result->rsp->nwk_addr_of_interest);
}

static bool node_signal_callback(const ezb_app_signal_t *app_signal) {
    ezb_err_t ret;
    ezb_app_signal_type_t signal = ezb_app_signal_get_type(app_signal);
    ESP_LOGE(TAG, "node signal callback");
    switch (signal) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGE(TAG, "Initialize Zigbee stack");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;
    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            //ESP_LOGE(TAG, "Deferred driver initialization %s", deferred_driver_init() ? "failed" : "successful");
            ESP_LOGE(TAG, "Device started up in%s factory-reset mode", ezb_bdb_is_factory_new() ? "" : " non");
            if (ezb_bdb_is_factory_new()) {
                ESP_ERROR_CHECK(ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_FORMATION));
            } else {
                ret = ezb_bdb_open_network(180);
                //ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
                ESP_LOGE(TAG,
                         "PAN ID: 0x%04X",
                         ezb_nwk_get_panid());
                if (ret!=EZB_ERR_NONE) {
                    ESP_LOGE(TAG, "Open network failed");
                }
                ESP_LOGE(TAG, "Device reboot");
            }
        } else {
            ESP_LOGW(TAG, "The %s failed with status(0x%02x), please retry", ezb_app_signal_to_string(signal), status);
            //alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning, EZB_BDB_MODE_INITIALIZATION, 1000);
        }
    } break;
    case EZB_BDB_SIGNAL_FORMATION: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ezb_extpanid_t extended_pan_id;
            ezb_nwk_get_extended_panid(&extended_pan_id);
            ESP_LOGE(TAG, "Formed network successfully: PAN ID(0x%04hx, EXT: 0x%llx), Channel(%d), Short Address(0x%04hx)",
                     ezb_nwk_get_panid(), extended_pan_id.u64, ezb_nwk_get_current_channel(), ezb_nwk_get_short_address());
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGW(TAG, "Failed to form network with status(0x%02x)", status);
            //alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning, EZB_BDB_MODE_NETWORK_FORMATION, 1000);
        }
    } break;
    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Network steering completed");
        } else {
            ESP_LOGW(TAG, "Failed to steering network with status(0x%02x)", status);
            //alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning, EZB_BDB_MODE_NETWORK_FORMATION, 1000);
        }
    } break;
    case EZB_ZDO_SIGNAL_DEVICE_ANNCE: {
        ESP_LOGE(TAG, "new node joined");
        const ezb_zdo_signal_device_annce_params_t *node_info_joined = (const ezb_zdo_signal_device_annce_params_t *)ezb_app_signal_get_params(app_signal);
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
        break;
    }
    case EZB_ZDO_SIGNAL_DEVICE_UNAVAILABLE: {
        const ezb_zdo_signal_device_unavailable_params_t *node_info_unavailable = (const ezb_zdo_signal_device_unavailable_params_t *)ezb_app_signal_get_params(app_signal);
        for(uint8_t i=0; i<nodes_index; i++) {
            if (joined_nodes_id[i].network_addr_ieee == node_info_unavailable->device_addr.u64) {
                rearrange_joined_nodes_array(i);
                return true;
            }
        }
    }
     case EZB_ZDO_SIGNAL_ERROR:
        {
            ESP_LOGE(TAG, "EZB_ZDO_SIGNAL_ERROR");

            /*
             * Laut Header besitzt dieses Signal keine Payload.
             */
            break;
        }


        case EZB_ZDO_SIGNAL_LEAVE:
        {
            const ezb_zdo_signal_leave_params_t *p =
                (const ezb_zdo_signal_leave_params_t *)app_signal;

            ESP_LOGW(TAG,
                     "EZB_ZDO_SIGNAL_LEAVE: leave_type=%u",
                     p->leave_type);

            if (p->leave_type == EZB_ZDO_LEAVE_TYPE_RESET)
            {
                ESP_LOGW(TAG, "Device left network without rejoin");
            }
            else if (p->leave_type == EZB_ZDO_LEAVE_TYPE_REJOIN)
            {
                ESP_LOGW(TAG, "Device left network and may rejoin");
            }

            break;
        }


        case EZB_ZDO_SIGNAL_LEAVE_INDICATION:
        {
            const ezb_zdo_signal_leave_indication_params_t *p =
                (const ezb_zdo_signal_leave_indication_params_t *)app_signal;

            ESP_LOGW(TAG,
                     "EZB_ZDO_SIGNAL_LEAVE_INDICATION");

            ESP_LOGW(TAG,
                     "  short_addr  = 0x%04X",
                     p->short_addr);

            ESP_LOGW(TAG,
                     "  ieee_addr   = 0x%016llX",
                     p->device_addr.u64);

            ESP_LOGW(TAG,
                     "  leave_type  = %u",
                     p->leave_type);

            if (p->leave_type == EZB_ZDO_LEAVE_TYPE_RESET)
            {
                ESP_LOGW(TAG,
                         "Device left without rejoin");
            }
            else if (p->leave_type == EZB_ZDO_LEAVE_TYPE_REJOIN)
            {
                ESP_LOGW(TAG,
                         "Device left with rejoin allowed");
            }

            break;
        }


        case EZB_ZDO_SIGNAL_DEVICE_UPDATE:
        {
            const ezb_zdo_signal_device_update_params_t *p =
                (const ezb_zdo_signal_device_update_params_t *)app_signal;

            ESP_LOGI(TAG,
                     "EZB_ZDO_SIGNAL_DEVICE_UPDATE");

            ESP_LOGI(TAG,
                     "  short_addr  = 0x%04X",
                     p->short_addr);

            ESP_LOGI(TAG,
                     "  ieee_addr   = 0x%016llX",
                     p->device_addr.u64);

            ESP_LOGI(TAG,
                     "  status      = 0x%02X",
                     p->status);

            ESP_LOGI(TAG,
                     "  tc_action   = 0x%02X",
                     p->tc_action);

            ESP_LOGI(TAG,
                     "  parent      = 0x%04X",
                     p->parent_short);

            switch (p->status)
            {
                case EZB_ZDO_UPDDEV_SECURE_REJOIN:
                    ESP_LOGI(TAG, "  status = SECURE_REJOIN");
                    break;

                case EZB_ZDO_UPDDEV_UNSECURE_JOIN:
                    ESP_LOGI(TAG, "  status = UNSecure_JOIN");
                    break;

                case EZB_ZDO_UPDDEV_DEVICE_LEFT:
                    ESP_LOGI(TAG, "  status = DEVICE_LEFT");
                    break;

                case EZB_ZDO_UPDDEV_TC_REJOIN:
                    ESP_LOGI(TAG, "  status = TC_REJOIN");
                    break;

                default:
                    ESP_LOGW(TAG,
                             "  unknown update status: 0x%02X",
                             p->status);
                    break;
            }

            break;
        }


        case EZB_ZDO_SIGNAL_DEVICE_AUTHORIZED:
        {
            const ezb_zdo_signal_device_authorized_params_t *p =
                (const ezb_zdo_signal_device_authorized_params_t *)app_signal;

            ESP_LOGI(TAG,
                     "EZB_ZDO_SIGNAL_DEVICE_AUTHORIZED");

            ESP_LOGI(TAG,
                     "  short_addr = 0x%04X",
                     p->short_addr);

            ESP_LOGI(TAG,
                     "  ieee_addr  = 0x%016llX",
                     p->device_addr.u64);

            ESP_LOGI(TAG,
                     "  type       = 0x%02X",
                     p->type);

            ESP_LOGI(TAG,
                     "  status     = 0x%02X",
                     p->status);

            switch (p->status)
            {
                case EZB_ZDO_AUTH_STATUS_SUCCESS:
                    ESP_LOGI(TAG,
                             "  authorization = SUCCESS");
                    break;

                case EZB_ZDO_AUTH_STATUS_TIMEOUT:
                    ESP_LOGW(TAG,
                             "  authorization = TIMEOUT");
                    break;

                case EZB_ZDO_AUTH_STATUS_FAILED:
                    ESP_LOGE(TAG,
                             "  authorization = FAILED");
                    break;

                default:
                    ESP_LOGW(TAG,
                             "  unknown authorization status");
                    break;
            }

            break;
        }


        case EZB_BDB_SIGNAL_FINDING_AND_BINDING_INITIATOR_FINISHED:
        {
            const ezb_bdb_signal_simple_params_t *p =
                (const ezb_bdb_signal_simple_params_t *)app_signal;

            ESP_LOGI(TAG,
                     "EZB_BDB_SIGNAL_FINDING_AND_BINDING_INITIATOR_FINISHED");

            ESP_LOGI(TAG,
                     "  status = 0x%02X",
                     p->status);

            break;
        }


        case EZB_BDB_SIGNAL_FINDING_AND_BINDING_TARGET_FINISHED:
        {
            const ezb_bdb_signal_simple_params_t *p =
                (const ezb_bdb_signal_simple_params_t *)app_signal;

            ESP_LOGI(TAG,
                     "EZB_BDB_SIGNAL_FINDING_AND_BINDING_TARGET_FINISHED");

            ESP_LOGI(TAG,
                     "  status = 0x%02X",
                     p->status);

            break;
        }


        case EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
        {
            const ezb_nwk_signal_permit_join_status_params_t *p =
                (const ezb_nwk_signal_permit_join_status_params_t *)app_signal;

            ESP_LOGI(TAG,
                     "EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS");

            ESP_LOGI(TAG,
                     "  duration = %u seconds",
                     p->duration);

            if (p->duration > 0)
            {
                ESP_LOGI(TAG,
                         "  Network is OPEN for joining");
            }
            else
            {
                ESP_LOGI(TAG,
                         "  Network is CLOSED for joining");
            }

            break;
        }


        default:
        {
            ESP_LOGI(TAG,
                     "Unhandled signal: 0x%04X (%s)",
                     signal,
                     ezb_app_signal_to_string(signal));

            return false;
        }
    }
    
    return true;   
}
    

static void zigbee_zcl_callback(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    ESP_LOGE(TAG, "zcl callback");
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
    ret = esp_zigbee_init(&zigbee_config);
    
    
    ezb_zcl_core_action_handler_register(zigbee_zcl_callback);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(channel_mask));
    ret = ezb_app_signal_add_handler(node_signal_callback);
    ESP_LOGE(TAG,
         "ezb_app_signal_add_handler: ret=0x%x",
         ret);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG,
             "Failed adding signal handler");        
    }
    ret = esp_zigbee_start(false);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG,
             "Failed starting zigbee stack");     
        esp_restart();    
    }
    else {
        ESP_LOGE(TAG, "start zigbee ok");
    }
    
    ESP_LOGE(TAG, "hello_zb");
    ret = esp_zigbee_launch_mainloop();
    if(ret != ESP_OK) {
        ESP_LOGE(TAG,
             "Failed starting zigbee stack");     
        esp_restart();    
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(
        nvs_flash_init_partition("zb_storage")
    );

    xTaskCreate(esp_zb_task, "zigbee_task", 4096, NULL, 10, NULL);
      
}

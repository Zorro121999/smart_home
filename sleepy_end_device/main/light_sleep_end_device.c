/*
 * SPDX-FileCopyrightText: 2022-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#ifdef CONFIG_PM_ENABLE
#include "esp_pm.h"
#include "esp_sleep.h"
#endif

#if CONFIG_ESP_SLEEP_DEBUG
#include "esp_private/esp_pmu.h"
#include "esp_private/esp_sleep_internal.h"
#endif

#if SOC_RTCIO_INPUT_OUTPUT_SUPPORTED
#include "driver/rtc_io.h"
#endif

#include "esp_zigbee.h"
#include "ezbee/zha.h"

#include "light_sleep_end_device.h"
#include "esp_rom_sys.h"

#include "driver/gpio.h"

#define BLINK_GPIO  24

static const char *TAG = "LIGHT_SLEEP_END_DEVICE";

esp_zigbee_config_t zigbee_config = {
.device_config = {
    .device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE,
    .install_code_policy = false,

    .zed_config = {
        .ed_timeout = EZB_NWK_ED_TIMEOUT_2MIN,
        .keep_alive = 30000,
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

static ezb_zcl_status_t customized_data_stream_server_cmd_handler(const ezb_zcl_cmd_hdr_t *header,
                                                                  const uint8_t           *payload,
                                                                  uint16_t                 payload_length)
{
    ezb_zcl_status_t ret = EZB_ZCL_STATUS_SUCCESS;
    assert(header);
    ESP_LOGI(TAG,"entered custom cluster server handler");
    if (header->cluster_id != SENSOR_CLUSTER_ID) {
        ret = EZB_ZCL_STATUS_UNSUPPORTED_CLUSTER;
       
    }

    if (EZB_ZCL_CMD_FC_IS_TO_CLI_DIRECTION(header->fc)) {
        ret = EZB_ZCL_STATUS_INVALID_FIELD;
        
    }
    return ret;
}


void esp_zigbee_zcl_customized_data_stream_server_init(uint8_t ep_id)
{
    ezb_zcl_custom_cluster_handlers_t handlers = {
        .cluster_id     = SENSOR_CLUSTER_ID,
        .cluster_role   = EZB_ZCL_CLUSTER_CLIENT,
        .process_cmd_cb = customized_data_stream_server_cmd_handler,
        .check_value_cb = NULL,
        .write_attr_cb  = NULL,
        .cmd_disc_cb    = NULL,
    };
    ezb_zcl_custom_cluster_handlers_register(&handlers);
}


ezb_zcl_custom_cluster_config_t sensor_cluster_config = {
        .cluster_id = SENSOR_CLUSTER_ID,
        .init_func = esp_zigbee_zcl_customized_data_stream_server_init,
        .deinit_func = NULL
    };
ezb_af_ep_config_t sensor_endpoint_config = {
    .ep_id = ENDPOINT0,
    .app_profile_id = 0x0104U,
    .app_device_id = LIVING_ROOM,
    .app_device_version = 1
};


static void zdo_bind_ha_light_device_result(const ezb_zdp_bind_req_result_t *result, void *user_ctx)
{
    assert(result);
    if (result->error == EZB_ERR_NONE) {
        if (result->rsp && result->rsp->status == EZB_ZDP_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Bound HA light device successfully");
        } else {
            ESP_LOGE(TAG, "Failed to bind HA light device with status (0x%02x)", result->rsp->status);
        }
    } else {
        ESP_LOGE(TAG, "Failed to bind HA light device with error (0x%04x)", result->error);
    }
}

static ezb_err_t zdo_bind_ha_light_device(uint16_t dst_short_addr, uint8_t dst_ep)
{
    ezb_err_t          ret      = EZB_ERR_FAIL;
    ezb_zdo_bind_req_t bind_req = {
        .dst_nwk_addr = ezb_nwk_get_short_address(),
        .field =
            {
                .src_ep        = ESP_ZIGBEE_HA_ON_OFF_SWITCH_EP_ID,
                .cluster_id    = EZB_ZCL_CLUSTER_ID_ON_OFF,
                .dst_addr_mode = EZB_ADDR_MODE_EXT,
                .dst_ep        = dst_ep,
            },
        .cb       = zdo_bind_ha_light_device_result,
        .user_ctx = NULL,
    };
    ezb_nwk_get_extended_address(&bind_req.field.src_addr);

    ESP_RETURN_ON_ERROR(ezb_address_extended_by_short(dst_short_addr, &bind_req.field.dst_addr.extended_addr), TAG,
                        "Failed to get extended address for destination device(0x%04hx)", dst_short_addr);

    ret = ezb_zdo_bind_req(&bind_req);
    if (ret == EZB_ERR_NONE) {
        ESP_LOGI(TAG, "Attempt to bind HA light device (short address: 0x%04hx)", dst_short_addr);
    } else {
        ESP_LOGE(TAG, "Failed to bind HA light device (short address: 0x%04hx) with error(0x%04x)", dst_short_addr, ret);
    }
    return ret;
}

static void zdo_find_ha_light_device_result(const ezb_zdo_match_desc_req_result_t *result, void *user_ctx)
{
    assert(result);
    if (result->error == EZB_ERR_NONE) {
        if (result->rsp && result->rsp->status == EZB_ZDP_STATUS_SUCCESS && result->rsp->match_length > 0 &&
            result->rsp->match_list) {
            for (size_t i = 0; i < result->rsp->match_length; i++) {
                zdo_bind_ha_light_device(result->rsp->nwk_addr_of_interest, result->rsp->match_list[i]);
            }
        }
    } else {
        ESP_LOGE(TAG, "Failed to find HA light device in the network with error(0x%04x)", result->error);
    }
}

static ezb_err_t zdo_find_ha_light_device(void)
{
    ezb_err_t ret             = EZB_ERR_FAIL;
    uint16_t  cluster_list[1] = {EZB_ZCL_CLUSTER_ID_ON_OFF};

    ezb_zdo_match_desc_req_t req = {
        .dst_nwk_addr = 0xFFFD,
        .field =
            {
                .nwk_addr_of_interest = 0xFFFD,
                .profile_id           = EZB_AF_HA_PROFILE_ID,
                .num_in_clusters      = 1,
                .num_out_clusters     = 0,
                .cluster_list         = cluster_list,
            },
        .cb       = zdo_find_ha_light_device_result,
        .user_ctx = NULL,
    };
    ret = ezb_zdo_match_desc_req(&req);
    if (ret == EZB_ERR_NONE) {
        ESP_LOGI(TAG, "Attempt to find HA light device in the network");
    } else {
        ESP_LOGE(TAG, "Failed to find HA light device in the network with error(0x%04x)", ret);
    }
    return ret;
}

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
esp_err_t esp_pm_entry_light_sleep_cb(int64_t sleep_time_us, void *arg)
{
    ESP_EARLY_LOGI(TAG, "Enter Light Sleep");
    gpio_set_level(BLINK_GPIO,0);
    return ESP_OK;
}

esp_err_t esp_pm_exit_light_sleep_cb(int64_t sleep_time_us, void *arg)
{
    ESP_EARLY_LOGI(TAG, "Exit Light Sleep");
    gpio_set_level(BLINK_GPIO,1);
    return ESP_OK;
}

esp_pm_sleep_cbs_register_config_t s_sleep_cbs_config = {
    .enter_cb          = esp_pm_entry_light_sleep_cb,
    .exit_cb           = esp_pm_exit_light_sleep_cb,
    .enter_cb_user_arg = NULL,
    .exit_cb_user_arg  = NULL,
    .enter_cb_prior    = 0,
    .exit_cb_prior     = 0,
};
#endif /* CONFIG_PM_LIGHT_SLEEP_CALLBACKS */

#ifdef CONFIG_PM_ENABLE
static esp_err_t esp_pm_light_sleep_config(void)
{
    esp_err_t rc = ESP_OK;
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
    int             cur_cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    esp_pm_config_t pm_config        = {
               .max_freq_mhz       = cur_cpu_freq_mhz,
               .min_freq_mhz       = cur_cpu_freq_mhz,
               .light_sleep_enable = true,
    };
    rc = esp_pm_configure(&pm_config);
#endif /* CONFIG_FREERTOS_USE_TICKLESS_IDLE */

#if CONFIG_ESP_SLEEP_DEBUG
    esp_sleep_set_sleep_context(&s_sleep_ctx);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    s_sleep_debug_timer = xTimerCreate("Sleep_xTimer", pdMS_TO_TICKS(20000), pdTRUE, NULL, sleep_debug_timer_cb);
    xTimerStart(s_sleep_debug_timer, 0);
#endif /* CONFIG_ESP_SLEEP_DEBUG */

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
    rc == ESP_OK ? esp_pm_light_sleep_register_cbs(&s_sleep_cbs_config) : rc;
#endif /* CONFIG_PM_LIGHT_SLEEP_CALLBACKS */
    return rc;
}
#endif /* CONFIG_PM_ENABLE */



static bool esp_zigbee_app_signal_handler(const ezb_app_signal_t *app_signal)
{
    ezb_app_signal_type_t signal_type = ezb_app_signal_get_type(app_signal);
    
    switch (signal_type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;
    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    {
    ezb_bdb_comm_status_t status =
        *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));

    ESP_LOGI(TAG,
             "DEVICE_FIRST_START status=0x%02X factory_new=%d",
             status,
             ezb_bdb_is_factory_new());

    if (status == EZB_BDB_STATUS_SUCCESS) {

        if (ezb_bdb_is_factory_new()) {

            ESP_LOGI(TAG, "Starting NETWORK_STEERING");

            ezb_err_t ret =
                ezb_bdb_start_top_level_commissioning(
                    EZB_BDB_MODE_NETWORK_STEERING);

            ESP_LOGI(TAG,
                     "NETWORK_STEERING ret=0x%04X",
                     ret);
        }
    }
    break;
    }
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            //ESP_LOGI(TAG, "Deferred driver initialization %s", deferred_driver_init() ? "failed" : "successful");
            ESP_LOGI(TAG, "Device started up in%s factory-reset mode", ezb_bdb_is_factory_new() ? "" : " non");
            if (ezb_bdb_is_factory_new()) {
                ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device reboot");
                #ifdef CONFIG_PM_ENABLE
                    ESP_LOGI(TAG,"entered pm config");
                    ESP_ERROR_CHECK(esp_pm_light_sleep_config());
                #endif
            }
        } else {
            ESP_LOGW(TAG, "%s failed with status(0x%02x), please retry", ezb_app_signal_to_string(signal_type), status);
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
            //alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning, EZB_BDB_MODE_INITIALIZATION, 1000);
        }
    } break;
    case EZB_BDB_SIGNAL_STEERING: {
        ezb_shortaddr_t address;
        address = ezb_nwk_get_short_address();
        ESP_LOGE(TAG, "end node network address:%" PRIu16, address);
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ezb_extpanid_t extended_pan_id;
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

esp_err_t esp_zigbee_create_sleepy_switch_device(void)
{
    
    
    ezb_af_device_desc_t sensor_device = ezb_af_create_device_desc();
    ezb_af_ep_desc_t sensor_endpoint = ezb_af_create_endpoint_desc(&sensor_endpoint_config);
    ezb_zcl_cluster_desc_t sensor_cluster = ezb_zcl_custom_create_cluster_desc(&sensor_cluster_config, EZB_ZCL_CLUSTER_SERVER);
    // ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_TEMPERATURE_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_WRITE | EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_REPORTING, &(data.temp));
    // if(ret != EZB_ERR_NONE) {
    //     ESP_LOGE(TAG,
    //          "Failed adding temp attribute"); 
    // }
    // ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_HUMIDITY_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_WRITE | EZB_ZCL_ATTR_ACCESS_REPORTING, &(data.humidity));
    // ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_SOIL_MOISTURE_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_WRITE | EZB_ZCL_ATTR_ACCESS_REPORTING, &(data.moisture));
    // ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_SOC_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_WRITE | EZB_ZCL_ATTR_ACCESS_REPORTING, &(data.soc));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(sensor_endpoint, sensor_cluster));
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(sensor_device, sensor_endpoint));
    ESP_ERROR_CHECK(ezb_af_device_desc_register(sensor_device));
    


  



    ezb_zcl_core_action_handler_register(esp_zigbee_zcl_core_action_handler);

    return ESP_OK;
}

esp_err_t esp_zigbee_setup_commissioning(void)
{
    ezb_aps_secur_enable_distributed_security(false);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(channel_mask));
    ezb_nwk_set_rx_on_when_idle(false);

    bool rx_on = ezb_nwk_get_rx_on_when_idle();

    ESP_LOGI(TAG, "rx_on_when_idle = %d", rx_on);
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(esp_zigbee_app_signal_handler));
    ezb_nwk_set_rx_on_when_idle(false);

    return ESP_OK;
}



#if CONFIG_ESP_SLEEP_DEBUG
static esp_sleep_context_t s_sleep_ctx;
static TimerHandle_t       s_sleep_debug_timer;

static void sleep_debug_timer_cb(TimerHandle_t xTimer)
{
    assert(xTimer);

    ESP_EARLY_LOGI(TAG, "sleep_flags 0x%08lx, trigger: 0x%08lx", s_sleep_ctx.sleep_flags, s_sleep_ctx.wakeup_triggers);
    ESP_EARLY_LOGI(TAG, "PMU_SLEEP_PD_TOP: %s", (s_sleep_ctx.sleep_flags & PMU_SLEEP_PD_TOP) ? "True" : "False");
    ESP_EARLY_LOGI(TAG, "PMU_SLEEP_PD_MODEM: %s", (s_sleep_ctx.sleep_flags & PMU_SLEEP_PD_MODEM) ? "True" : "False");
    ESP_EARLY_LOGI(TAG, "Wakeup source: 0x%04x", esp_sleep_get_wakeup_causes());
}
#endif /* CONFIG_ESP_SLEEP_DEBUG */



static void esp_zigbee_stack_main_task(void *pvParameters)
{
    esp_err_t ret;
    ret = esp_zigbee_init(&zigbee_config);

    //bool lock = esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000));
    ESP_LOGE("ZIGBEE", "ESP Zigbee SDK: %s",
         esp_zigbee_get_version_string());
    
    ezb_af_device_desc_t sensor_device = ezb_af_create_device_desc();
    ezb_af_ep_desc_t sensor_endpoint = ezb_af_create_endpoint_desc(&sensor_endpoint_config);
    ezb_zcl_cluster_desc_t sensor_cluster = ezb_zcl_custom_create_cluster_desc(&sensor_cluster_config, EZB_ZCL_CLUSTER_SERVER);
    // ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_TEMPERATURE_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_WRITE | EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_REPORTING, &(data.temp));
    // if(ret != EZB_ERR_NONE) {
    //     ESP_LOGE(TAG,
    //          "Failed adding temp attribute"); 
    // }
    // ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_HUMIDITY_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_WRITE | EZB_ZCL_ATTR_ACCESS_REPORTING, &(data.humidity));
    // ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_SOIL_MOISTURE_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_WRITE | EZB_ZCL_ATTR_ACCESS_REPORTING, &(data.moisture));
    // ret = ezb_zcl_custom_cluster_desc_add_attr(sensor_cluster, ATTR_SOC_ID, EZB_ZCL_ATTR_TYPE_SINGLE, EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_WRITE | EZB_ZCL_ATTR_ACCESS_REPORTING, &(data.soc));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(sensor_endpoint, sensor_cluster));
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(sensor_device, sensor_endpoint));
    ESP_ERROR_CHECK(ezb_af_device_desc_register(sensor_device));
    

    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(channel_mask));
    ezb_nwk_set_rx_on_when_idle(false);

    bool rx_on = ezb_nwk_get_rx_on_when_idle();

    ESP_LOGI(TAG, "rx_on_when_idle = %d", rx_on);
    ezb_app_signal_add_handler(esp_zigbee_app_signal_handler);
  
    ret = esp_zigbee_start(false);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG,
             "Failed starting zigbee stack");     
        esp_restart();    
    }
    ESP_LOGE(TAG, "hello_zb");
    //vTaskDelay(pdMS_TO_TICKS(100000));

    esp_zigbee_launch_mainloop();
    esp_zigbee_deinit();

    vTaskDelete(NULL);
}

void esp_block_task(void *arg)
{
    while (1) {

        // 3 Sekunden CPU aktiv blockieren
        esp_rom_delay_us(3000000);

        // 3 Sekunden Task blockieren -> CPU kann Idle/Sleep gehen
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(
        nvs_flash_init_partition("zb_storage")
    );

    

    ESP_LOGI(TAG, "Start ESP Zigbee Stack");
    xTaskCreate(esp_zigbee_stack_main_task, "Zigbee_main", 4096, NULL, 5, NULL);
    xTaskCreate(esp_block_task, "block_main", 4096, NULL, 10, NULL);

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BLINK_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    gpio_set_level(BLINK_GPIO,1);
}

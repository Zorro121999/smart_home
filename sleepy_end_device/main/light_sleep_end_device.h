/*
 * SPDX-FileCopyrightText: 2022-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#pragma once
#define CONFIG_ZB_EXAMPLE_PRIMARY_CHANNEL  20
#define CONFIG_ZB_EXAMPLE_SECONDARY_CHANNEL  21
#define ESP_ZIGBEE_PRIMARY_CHANNEL_MASK   (1U << CONFIG_ZB_EXAMPLE_PRIMARY_CHANNEL)
#define ESP_ZIGBEE_SECONDARY_CHANNEL_MASK (1U << CONFIG_ZB_EXAMPLE_SECONDARY_CHANNEL)

#define ESP_ZIGBEE_HA_ON_OFF_SWITCH_EP_ID (10)

#define ESP_ZIGBEE_STORAGE_PARTITION_NAME "zb_storage"

#define ESP_MANUFACTURER_NAME "\x09""ESPRESSIF"
#define ESP_MODEL_IDENTIFIER "\x07"CONFIG_IDF_TARGET

#define ESP_ZIGBEE_ZED_CONFIG()                         \
    {                                                   \
        .device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE,  \
        .install_code_policy = false,                   \
        .zed_config = {                                 \
            .ed_timeout = EZB_NWK_ED_TIMEOUT_64MIN,     \
            .keep_alive = 4000,                         \
        },                                              \
    }

#if CONFIG_SOC_IEEE802154_SUPPORTED
#define ESP_ZIGBEE_PLATFORM_CONFIG()                                 \
    {                                                                \
        .storage_partition_name = ESP_ZIGBEE_STORAGE_PARTITION_NAME, \
        .radio_config = {                                            \
            .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE,              \
        },                                                           \
    }
#else
#warning "The example is not for IEEE 802.15.4-disabled SoC usage, please refer to esp_zigbee_gateway for RCP configuration"
#endif

#define ESP_ZIGBEE_DEFAULT_CONFIG()                      \
    {                                                    \
        .device_config = ESP_ZIGBEE_ZED_CONFIG(),        \
        .platform_config = ESP_ZIGBEE_PLATFORM_CONFIG(), \
    };

#define LIVING_ROOM               0x01

#define ENDPOINT0                   0x01
#define COORDINATOR_EP              0x0A

#define SENSOR_CLUSTER_ID          0xFC00

#define ATTR_TEMPERATURE_ID        0x0000
#define ATTR_HUMIDITY_ID           0x0001
#define ATTR_SOIL_MOISTURE_ID      0x0002
#define ATTR_SOC_ID                0x0003
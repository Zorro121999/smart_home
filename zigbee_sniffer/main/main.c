#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <inttypes.h>
#include "sdkconfig.h"
#include "esp_chip_info.h"
//#include "esp_flash.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_ieee802154.h"
#include "nvs_flash.h"


#define CHANNEL 20

static const char *TAG = "802154_SNIFFER";

/*
 * esp_ieee802154_receive_done() wird aus dem Radio-Kontext
 * aufgerufen. Deshalb hier möglichst wenig Arbeit machen.
 *
 * Für unseren ersten Test geben wir den Frame direkt aus.
 *
 * Frame-Format:
 *
 *   frame[0]       = Länge
 *   frame[1..]     = MHR + MAC Payload
 *
 * Das FCS wird vom Hardware-RX geprüft und nicht als normales
 * FCS-Feld an den Callback geliefert.
 */
void IRAM_ATTR esp_ieee802154_receive_done(
    uint8_t *frame,
    esp_ieee802154_frame_info_t *frame_info)
{
    if (frame == NULL || frame_info == NULL) {
        return;
    }

    uint8_t len = frame[0] & 0x7f;

    /*
     * Achtung:
     * Dieser Callback läuft im ISR-Kontext.
     * Deshalb hier zunächst nur einen sehr kurzen Test.
     */
    ESP_DRAM_LOGI(TAG,
                  "RX! len=%u RSSI=%d LQI=%u",
                  len,
                  frame_info->rssi,
                  frame_info->lqi);

    for (int i = 0; i < len; i++) {
        ESP_DRAM_LOGI(TAG, "%02X", frame[i + 1]);
    }

    /*
     * Sehr wichtig!
     */
    esp_ieee802154_receive_handle_done(frame);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    ESP_LOGI(TAG, "Starting 802.15.4 sniffer");

    ESP_ERROR_CHECK(esp_ieee802154_enable());

    ESP_ERROR_CHECK(
        esp_ieee802154_set_channel(CHANNEL)
    );

    ESP_ERROR_CHECK(
        esp_ieee802154_set_promiscuous(true)
    );

    ESP_ERROR_CHECK(
        esp_ieee802154_set_rx_when_idle(true)
    );

    ESP_LOGI(TAG,
             "channel=%u",
             esp_ieee802154_get_channel());

    ESP_LOGI(TAG,
             "promiscuous=%d",
             esp_ieee802154_get_promiscuous());

    ESP_LOGI(TAG,
             "rx_when_idle=%d",
             esp_ieee802154_get_rx_when_idle());

    ESP_ERROR_CHECK(
        esp_ieee802154_receive()
    );

    ESP_LOGI(TAG, "RX started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
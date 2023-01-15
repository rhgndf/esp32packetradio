
#include "esp_check.h"
#include "driver/gpio.h"

#define CFG_TUSB_MCU OPT_MCU_ESP32S2
#include "tusb.h"
#include "lwip/tcpip.h"

#define RH_HAVE_SERIAL
#include "radio.h"

#include "Arduino.h"
#include "SPI.h"
#include "RH_RF24.h"

static const char* TAG = "Radio";

#define RADIO_NSEL GPIO_NUM_34
#define RADIO_IRQ  GPIO_NUM_40
#define RADIO_SDN  GPIO_NUM_39

#define RADIO_SCK  GPIO_NUM_36
#define RADIO_MISO GPIO_NUM_37
#define RADIO_MOSI GPIO_NUM_35

RH_RF24 rf24(RADIO_NSEL, RADIO_IRQ, RADIO_SDN);

void radio_sdn_reset() {
    gpio_set_level(RADIO_SDN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(RADIO_SDN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void radio_recv_task(void *arg)
{
    ESP_LOGD(TAG, "radio recv task started");
    uint8_t recvBuf[4096];
    while(true) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (!rf24.available()) continue;
        uint16_t len = 4096;
        rf24.recvRaw(recvBuf, &len);
        ESP_LOGI(TAG, "Received packet: %d bytes", len);
        struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);

        if (p)
        {
            /* pbuf_alloc() has already initialized struct; all we need to do is copy the data */
            memcpy(p->payload, recvBuf, len);

            tud_network_xmit(p, 0);
            
            pbuf_free(p);
        }
    }
}
TaskHandle_t radio_task;

esp_err_t radio_init() {
    gpio_set_level(RADIO_SDN, 1);
    gpio_set_direction(RADIO_SDN, GPIO_MODE_OUTPUT);
    radio_sdn_reset();

    if(!rf24.init()) {
        ESP_LOGE(TAG, "RF24 init failed");
        return ESP_FAIL;
    }
    rf24.setTxPower(0x4f);
    rf24.setModeIdle();

    ESP_RETURN_ON_FALSE(!radio_task, ESP_ERR_INVALID_STATE, TAG, "Radio receive task already started");

    xTaskCreate(radio_recv_task, "Radio", 8192, NULL, 5, &radio_task);
    ESP_RETURN_ON_FALSE(radio_task, ESP_FAIL, TAG, "create radio receive main task failed");
    ESP_LOGI(TAG, "Radio initialized");
    return ESP_OK;
}

extern "C" esp_err_t radio_send(const uint8_t* buf, uint16_t len) {
    ESP_LOGI(TAG, "Sending status %d", rf24.availableToSend());
    while(!rf24.availableToSend()) {
        int msToWait = pdMS_TO_TICKS(esp_random()%90+10);
        vTaskDelay(msToWait);
        ESP_LOGI(TAG, "Waiting %d ms for radio to be available", msToWait);
    }
    if(!rf24.availableToSend())return ESP_FAIL;
    ESP_LOGI(TAG, "Sending packet: %d bytes", len);
    return rf24.sendRaw((uint8_t*)buf, len) ? ESP_OK : ESP_FAIL;
}
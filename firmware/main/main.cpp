#include <stdio.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include <ESPmDNS.h>

#include "usb_network.h"
#include "wifi.h"
#include "http_server.h"
#include "logging.h"
#include "radio.h"
#include "soc/rtc_wdt.h"

#include "lwip/tcpip.h"

static const char TAG[] = "main";

extern "C" void app_main(void)
{
    /* initialize TinyUSB */
    //
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK( err );
    ESP_ERROR_CHECK(logging_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    //ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(radio_init());
    ESP_ERROR_CHECK(usb_network_init());
    //ESP_ERROR_CHECK(http_server_init());
    //MDNS.begin("esp32packetradio");
    ESP_LOGI(TAG, "Init done");

rtc_wdt_protect_off();
rtc_wdt_disable();
    while (1) {
        usb_network_traffic_task();
    }
}
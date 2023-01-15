#ifndef _USB_NETWORK_H_
#define _USB_NETWORK_H_
extern "C" {
extern struct netif netif_data;
void usb_network_traffic_task();
esp_err_t usb_network_init();
}
#endif
#include "esp_log.h"
#include "esp_check.h"

#define CFG_TUSB_MCU OPT_MCU_ESP32S2
#include "tusb.h"

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "lwip/ethip6.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"
#include "freertos/queue.h"

#include "dhcp_server.h"
#include "radio.h"

extern void usb_phy_init();

#define INIT_IP4(a, b, c, d)           \
  {                                    \
    PP_HTONL(LWIP_MAKEU32(a, b, c, d)) \
  }
static const char *TAG = "USB Network";
/* lwip context */
struct netif netif_data;

/* shared between tud_network_recv_cb() and service_traffic() */
static struct pbuf *received_frame;
QueueHandle_t frame_queue;
QueueHandle_t radio_queue;

/* this is used by this code, ./class/net/net_driver.c, and usb_descriptors.c */
/* ideally speaking, this should be generated from the hardware's unique ID (if available) */
/* it is suggested that the first byte is 0x02 to indicate a link-local address */
const uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

/* network parameters of this MCU */
static const ip4_addr_t ipaddr = INIT_IP4(192, 168, 7, 1);
static const ip4_addr_t netmask = INIT_IP4(255, 255, 255, 0);
static const ip4_addr_t gateway = INIT_IP4(0, 0, 0, 0);

/* database IP addresses that can be offered to the host; this must be in RAM to store assigned MAC addresses */
/* mac ip address                          lease time */
static dhcp_entry_t entries[] =
    {
        {{0}, INIT_IP4(192, 168, 7, 2), 24 * 60 * 60},
        {{0}, INIT_IP4(192, 168, 7, 3), 24 * 60 * 60},
        {{0}, INIT_IP4(192, 168, 7, 4), 24 * 60 * 60},
};

static dhcp_config_t dhcp_config =
    {
        .router = INIT_IP4(0, 0, 0, 0), /* router address (if any) */
        .bind_addr = IPADDR4_INIT_BYTES(192, 168, 7, 1),
        .port = 67,                      /* listen port */
        .dns = INIT_IP4(192, 168, 7, 1), /* dns server (if any) */
        "usb",                           /* dns suffix */
        TU_ARRAY_SIZE(entries),          /* num entry */
        entries                          /* entries */
};
static err_t linkoutput_fn(struct netif *netif, struct pbuf *p)
{
  (void)netif;
  for (;;)
  {
    /* if TinyUSB isn't ready, we must signal back to lwip that there is nothing we can do */
    if (!tud_ready())
      return ERR_USE;

    /* if the network driver can accept another packet, we make it happen */
    if (tud_network_can_xmit(p->tot_len))
    {
      tud_network_xmit(p, 0 /* unused for this example */);
      return ERR_OK;
    }
    vTaskDelay(1);
  }
}

static err_t ip4_output_fn(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr)
{
  return etharp_output(netif, p, addr);
}

#if LWIP_IPV6
static err_t ip6_output_fn(struct netif *netif, struct pbuf *p, const ip6_addr_t *addr)
{
  return ethip6_output(netif, p, addr);
}
#endif

static err_t netif_init_cb(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));
  netif->mtu = CFG_TUD_NET_MTU;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
  netif->state = NULL;
  netif->name[0] = 'R';
  netif->name[1] = 'F';
  netif->linkoutput = linkoutput_fn;
  netif->output = ip4_output_fn;
#if LWIP_IPV6
  netif->output_ip6 = ip6_output_fn;
#endif
  return ERR_OK;
}

static esp_err_t usb_init_netif(void)
{
  struct netif *netif = &netif_data;

  // Use the SoftAP address for the internal MAC
  esp_read_mac(netif->hwaddr, ESP_MAC_WIFI_SOFTAP);
  netif->hwaddr[0] = 0x02;
  netif->hwaddr_len = ETH_HWADDR_LEN;

  netif = netif_add(netif, &ipaddr, &netmask, &gateway, NULL, netif_init_cb, tcpip_input);
  if (!netif)
  {
    return ESP_FAIL;
  }
#if LWIP_IPV6
  netif_create_ip6_linklocal_address(netif, 1);
#endif
  // netif_set_default(netif);
  return ESP_OK;
}

/* handle any DNS requests from dns-server */
bool dns_query_proc(const char *name, ip4_addr_t *addr)
{
  if (0 == strcmp(name, "tiny.usb"))
  {
    *addr = ipaddr;
    return true;
  }
  return false;
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
  bool passthrough = true;
  ESP_LOGI(TAG, "Received %d bytes from USB", size);
  if (size)
  {
    ESP_LOGI(TAG, "test1");
    struct eth_hdr *hdr = (struct eth_hdr *)src;
    // Send packets bound for ESP32
    ESP_LOGI(TAG, "test");
    if (memcmp(hdr->dest.addr, netif_data.hwaddr, ETH_HWADDR_LEN) == 0)
    {
      passthrough = false;
    }
    else
      // If host ARP for the ESP32 ip address
      if (hdr->type == htons(ETHTYPE_ARP))
      {
        struct etharp_hdr *arp_pkt = (struct etharp_hdr *)(src + sizeof(struct eth_hdr));
        if (ip4_addr_cmp((ip4_addr_t *)&arp_pkt->dipaddr, &ipaddr))
        {
          passthrough = false;
        }
      }
      else
        // Process broadcast ethernet frames
        if (eth_addr_cmp(&hdr->dest, &ethbroadcast) && (hdr->type == htons(ETHTYPE_IP)))
        {
          struct ip_hdr *ip_pkt = (struct ip_hdr *)(src + sizeof(struct eth_hdr));
          ip4_addr_t broadcast_addr;
          ip4_addr_t zero_addr;
          ip4_addr_set_u32(&broadcast_addr, IPADDR_BROADCAST);
          ip4_addr_set_u32(&zero_addr, IPADDR_ANY);
          // Send DHCP request to ESP32 also
          if (ip4_addr_cmp(&ip_pkt->dest, &broadcast_addr) && IPH_PROTO(ip_pkt) == IP_PROTO_UDP)
          {
            struct udp_hdr *udp_pkt = (struct udp_hdr *)((const uint8_t *)ip_pkt + sizeof(struct ip_hdr));
            if (udp_pkt->dest == htons(67))
            {
              passthrough = false;
            }
          }
        }
        else if (hdr->dest.addr[0] == LL_IP6_MULTICAST_ADDR_0 && (hdr->type == htons(ETHTYPE_IPV6)))
        {
          // struct ip6_hdr* ip_pkt = (struct ip6_hdr*)(src + sizeof(struct eth_hdr));
          passthrough = false;
        }
    ESP_LOGI(TAG, "Passthrough: %d", passthrough);
    // if (!passthrough) {
    struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);

    if (p)
    {

      /* pbuf_alloc() has already initialized struct; all we need to do is copy the data */
      memcpy(p->payload, src, size);

      /* store away the pointer for service_traffic() to later handle */
      BaseType_t ret;
      ESP_LOGI(TAG, "queue send to back");
      if (passthrough)
      {
        ret = xQueueSendToBack(radio_queue, &p, pdMS_TO_TICKS(1));
        // ret = errQUEUE_FULL;
      }
      else
      {
        ret = xQueueSendToBack(frame_queue, &p, pdMS_TO_TICKS(1));
      }
      ESP_LOGI(TAG, "queue send to back finished");
      if (ret != pdTRUE)
      {
        ESP_LOGI(TAG, "Dropping frame");
        pbuf_free(p);
        return false;
      }
      ESP_LOGI(TAG, "1");
    }
    else
    {
      return false;
    }
      ESP_LOGI(TAG, "2");
    //} else {
    // radio_send(src, size);
    // tud_network_recv_renew();
    //}
  } else {
    return false;
  }
  ESP_LOGI(TAG, "3");
  tud_network_recv_renew();
  return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
  struct pbuf *p = (struct pbuf *)ref;

  (void)arg; /* unused for this example */

  return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

void tud_network_init_cb(void)
{
  /* if the network is re-initializing and we have a leftover packet, we must do a cleanup */
  if (received_frame)
  {
    pbuf_free(received_frame);
    received_frame = NULL;
  }
}

static TaskHandle_t s_tusb_tskh;
static TaskHandle_t s_rf_tskh;

/**
 * @brief This top level thread processes all usb events and invokes callbacks
 */
static void tusb_device_task(void *arg)
{
  ESP_LOGD(TAG, "tinyusb task started");
  while (1)
  { // RTOS forever loop
    tud_task();
  }
}

void usb_network_traffic_task(void)
{
  struct pbuf *received_frame;
  ESP_LOGI(TAG, "queue receive");
  if (xQueueReceive(frame_queue, &received_frame, pdMS_TO_TICKS(100)))
  {
    ESP_LOGI(TAG, "netif input");
    netif_data.input(received_frame, &netif_data);
    ESP_LOGI(TAG, "renew");
  }
  struct pbuf *received_radio_frame;
  if (xQueueReceive(radio_queue, &received_radio_frame, pdMS_TO_TICKS(100)))
  {
    radio_send(received_radio_frame->payload, received_radio_frame->tot_len);
    ESP_LOGI(TAG, "radio send finished");
  }
  vTaskDelay(pdMS_TO_TICKS(100));
  ESP_LOGI(TAG, "queue receive finished");
  //sys_check_timeouts();
}

void rf_network_traffic_send_task(void *arg)
{
  while (1)
  {
    struct pbuf *received_radio_frame;
    xQueueReceive(radio_queue, &received_radio_frame, portMAX_DELAY);
    // radio_send(received_radio_frame->payload, received_radio_frame->tot_len);
    // xQueueOverwrite(frame_queue, &received_radio_frame);
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

esp_err_t tusb_run_task(void)
{
  // This function is not garanteed to be thread safe, if invoked multiple times without calling `tusb_stop_task`, will cause memory leak
  // doing a sanity check anyway
  ESP_RETURN_ON_FALSE(!s_tusb_tskh, ESP_ERR_INVALID_STATE, TAG, "TinyUSB main task already started");
  // Create a task for tinyusb device stack:
  xTaskCreate(tusb_device_task, "TinyUSB", CONFIG_TINYUSB_TASK_STACK_SIZE, NULL, CONFIG_TINYUSB_TASK_PRIORITY, &s_tusb_tskh);
  ESP_RETURN_ON_FALSE(s_tusb_tskh, ESP_FAIL, TAG, "create TinyUSB main task failed");

  // xTaskCreate(rf_network_traffic_send_task, "RF Network Traffic", 4096, NULL, 6, &s_rf_tskh);
  return ESP_OK;
}

esp_err_t usb_wait_netif(void)
{
  TickType_t start = xTaskGetTickCount();
  do
  {
    if (netif_is_up(&netif_data))
    {
      return ESP_OK;
    }
    vTaskDelay(1);
  } while (xTaskGetTickCount() - start < pdMS_TO_TICKS(1000));
  return ESP_FAIL;
}
esp_err_t usb_network_init(void)
{

  // Initialize the network frame queue
  frame_queue = xQueueCreate(10, sizeof(struct pbuf *));
  radio_queue = xQueueCreate(10, sizeof(struct pbuf *));

  // Set the usb mac address with the station mac address
  // esp_read_mac(tud_network_mac_address, ESP_MAC_WIFI_STA);
  // tud_network_mac_address[0] = 0x02;

  usb_phy_init();
  tusb_init();
  ESP_RETURN_ON_ERROR(usb_init_netif(), TAG, "USB netif init failure");
  ESP_RETURN_ON_ERROR(tusb_run_task(), TAG, "TinyUSB task failed");
  ESP_RETURN_ON_ERROR(usb_wait_netif(), TAG, "USB netif start failed");
  ESP_LOGI(TAG, "USB netif is up");
  ESP_RETURN_ON_FALSE(dhcp_server_init(&dhcp_config) == ERR_OK, ESP_FAIL, TAG, "DHCP server start failed");
  ESP_LOGI(TAG, "USB netif DHCP is up");
  // while (dnserv_init(IP_ADDR_ANY, 53, dns_query_proc) != ERR_OK);
  ESP_LOGI(TAG, "dns init");
  return ESP_OK;
}
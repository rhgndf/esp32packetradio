#include <string.h>
#include <string>
#include <deque>
#include <vector>

#include "esp_log.h"
#include "lwip/udp.h"

#include "spiram_allocator.h"
#include "logging.h"

#include <Wire.h>
#include "Adafruit_GFX.h"
#include "Adafruit_SH110X.h"

#define i2c_Address 0x3c //initialize with the I2C addr 0x3C Typically eBay OLED's
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1   //   QT-PY / XIAO
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET, 1000000, 1000000);
bool display_init = false;

SemaphoreHandle_t log_entries_sem;
std::deque<std::pair<uint64_t, std::string>, PSRAMAllocator<std::pair<uint64_t, std::string>>> log_entries;

vprintf_like_t uart_log;
char log_str[1024];
struct pbuf * p;
struct udp_pcb * pcb;
int log_hook(const char* format, va_list args) {
    uint64_t log_time = esp_timer_get_time();
    uart_log(format, args);
    int len = vsnprintf(log_str, 1024, format, args);
    p->len = len;
    p->tot_len = len;
    return 0;
    udp_send(pcb, p);

    xSemaphoreTake(log_entries_sem, portMAX_DELAY);
    while (log_entries.size() > 100) {
        log_entries.pop_front();
    }
    log_entries.emplace_back(log_time, std::string(log_str));
    xSemaphoreGive(log_entries_sem);
    return 0;
}

std::vector<std::pair<uint64_t, std::string>, PSRAMAllocator<std::pair<uint64_t, std::string>>> logging_get_all_entries() {
    std::vector<std::pair<uint64_t, std::string>, PSRAMAllocator<std::pair<uint64_t, std::string>>> entries;
    xSemaphoreTake(log_entries_sem, portMAX_DELAY);
    for(auto &i : log_entries) {
        entries.emplace_back(i);
    }
    xSemaphoreGive(log_entries_sem);
    return entries;
}

esp_err_t logging_init() {
    uart_log = esp_log_set_vprintf(log_hook);
    log_entries_sem = xSemaphoreCreateMutex();
    p = pbuf_alloc(PBUF_TRANSPORT, 1024, PBUF_REF);
    p->payload = log_str;

    // UDP logging
    pcb = udp_new();
    ip_addr_t addr = IPADDR4_INIT_BYTES(192, 168, 7, 2);
    udp_connect(pcb, &addr, 1234);

    /*display.begin(i2c_Address, true); // Address 0x3C default
    vTaskDelay(pdMS_TO_TICKS(100));
    display.clearDisplay();
    display.display();
    display_init = true;*/

    return ESP_OK;
}

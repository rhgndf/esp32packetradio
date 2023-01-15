
#include "esp_check.h"
#include "esp_log.h"

#include <WiFi.h>

static const char TAG[] = "WIFI";

esp_err_t wifi_init() {
  WiFi.softAP("ESP32 WIFI","PASSWORD");
  return ESP_OK;
}
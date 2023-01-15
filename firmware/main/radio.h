#ifndef _RADIO_H_
#define _RADIO_H_

#include "esp_err.h"
#include "esp_log.h"

esp_err_t radio_init();

#ifdef __cplusplus
extern "C" 
#endif
esp_err_t radio_send(const uint8_t* buf, uint16_t len);

#endif
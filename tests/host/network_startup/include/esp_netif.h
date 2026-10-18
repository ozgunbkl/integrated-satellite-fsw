#ifndef TEST_NETWORK_ESP_NETIF_H
#define TEST_NETWORK_ESP_NETIF_H
#include "esp_err.h"
esp_err_t esp_netif_init(void);
esp_err_t esp_netif_deinit(void);
#endif

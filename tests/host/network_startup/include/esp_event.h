#ifndef TEST_NETWORK_ESP_EVENT_H
#define TEST_NETWORK_ESP_EVENT_H
#include "esp_err.h"
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_loop_delete_default(void);
#endif

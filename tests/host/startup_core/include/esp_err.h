#ifndef TEST_STARTUP_CORE_ESP_ERR_H
#define TEST_STARTUP_CORE_ESP_ERR_H
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
const char *esp_err_to_name(esp_err_t error);
#endif

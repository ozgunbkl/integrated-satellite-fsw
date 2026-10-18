#ifndef TEST_STARTUP_INJECTION_ESP_SYSTEM_H
#define TEST_STARTUP_INJECTION_ESP_SYSTEM_H
typedef enum { ESP_RST_POWERON = 1, ESP_RST_SW = 3, ESP_RST_PANIC = 4, ESP_RST_TASK_WDT = 6 } esp_reset_reason_t;
esp_reset_reason_t esp_reset_reason(void);
#endif

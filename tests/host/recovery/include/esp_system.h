#ifndef TEST_RECOVERY_ESP_SYSTEM_H
#define TEST_RECOVERY_ESP_SYSTEM_H
typedef enum { ESP_RST_UNKNOWN=0, ESP_RST_POWERON=1, ESP_RST_EXT=2, ESP_RST_SW=3,
    ESP_RST_PANIC=4, ESP_RST_INT_WDT=5, ESP_RST_TASK_WDT=6, ESP_RST_WDT=7,
    ESP_RST_DEEPSLEEP=8, ESP_RST_BROWNOUT=9 } esp_reset_reason_t;
esp_reset_reason_t esp_reset_reason(void);
#endif

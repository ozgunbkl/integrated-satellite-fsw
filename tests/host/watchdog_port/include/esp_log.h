#ifndef TEST_WATCHDOG_ESP_LOG_H
#define TEST_WATCHDOG_ESP_LOG_H
void TEST_Log(const char *tag, const char *format, ...);
#define ESP_LOGE(tag, ...) TEST_Log(tag, __VA_ARGS__)
#endif

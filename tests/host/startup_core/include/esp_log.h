#ifndef TEST_STARTUP_CORE_ESP_LOG_H
#define TEST_STARTUP_CORE_ESP_LOG_H
#define ESP_LOG_WARN 2
#define ESP_LOG_INFO 3
void esp_log_level_set(const char *tag, int level);
void test_log_error(const char *tag, const char *format, ...);
void test_log_warning(const char *tag, const char *format, ...);
#define ESP_LOGE(tag, ...) test_log_error(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) test_log_warning(tag, __VA_ARGS__)
#endif

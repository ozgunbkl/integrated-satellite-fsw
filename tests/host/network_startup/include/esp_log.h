#ifndef TEST_NETWORK_ESP_LOG_H
#define TEST_NETWORK_ESP_LOG_H
void test_log_error(const char *tag, const char *format, ...);
#define ESP_LOGE(tag, ...) test_log_error(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) ((void)(tag))
#endif

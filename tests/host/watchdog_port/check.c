/* Actual ESP32 adapter, mocked SDK API; no hardware timer/reset is executed. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "watchdog_port.h"
#include "esp_task_wdt.h"
#include "esp_system.h"

static esp_err_t sdk_result, status_result = ESP_ERR_NOT_FOUND;
static unsigned statuses, configurations, adds, resets, logs, reason_reads;
static const char *scenario;
static bool is(const char *name) { return strcmp(scenario, name) == 0; }
void TEST_Log(const char *tag, const char *format, ...) {
    assert(strcmp(tag, "WATCHDOG") == 0);
    char text[256]; va_list args; va_start(args, format);
    int length = vsnprintf(text, sizeof(text), format, args); va_end(args);
    assert(length > 0 && (unsigned)length < sizeof(text)); ++logs;
}
esp_err_t esp_task_wdt_status(TaskHandle_t task_handle) {
    assert(task_handle == NULL && configurations == 0); ++statuses; return status_result;
}
esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t *config) {
    assert(statuses == 1 && config != NULL);
    assert(config->timeout_ms == 15000U && config->idle_core_mask == 3U && config->trigger_panic);
    ++configurations; return sdk_result;
}
esp_err_t esp_task_wdt_add(TaskHandle_t task_handle) {
    assert(task_handle == NULL); ++adds; return sdk_result;
}
esp_err_t esp_task_wdt_reset(void) { ++resets; return sdk_result; }
esp_reset_reason_t esp_reset_reason(void) {
    assert(configurations == 1 && sdk_result == ESP_OK); ++reason_reads; return ESP_RST_POWERON;
}

int main(int argc, char **argv) {
    assert(argc == 2); scenario = argv[1];
    if (is("configure_success") || is("configure_existing") || is("configure_uninitialized") ||
        is("configure_status_error") || is("configure_error")) {
        if (is("configure_existing")) status_result = ESP_OK;
        if (is("configure_uninitialized")) status_result = ESP_ERR_INVALID_STATE;
        if (is("configure_status_error")) status_result = ESP_FAIL;
        if (is("configure_error")) sdk_result = ESP_ERR_INVALID_ARG;
        bool success = is("configure_success") || is("configure_existing");
        assert(WATCHDOG_PORT_InitRecovery() == success);
        assert(statuses == 1 && configurations == (unsigned)(status_result == ESP_OK || status_result == ESP_ERR_NOT_FOUND));
        assert(reason_reads == (unsigned)success);
    } else if (is("subscribe_success") || is("subscribe_duplicate") || is("subscribe_uninitialized") || is("subscribe_no_memory")) {
        if (is("subscribe_duplicate")) sdk_result = ESP_ERR_INVALID_ARG;
        if (is("subscribe_uninitialized")) sdk_result = ESP_ERR_INVALID_STATE;
        if (is("subscribe_no_memory")) sdk_result = ESP_ERR_NO_MEM;
        assert(WATCHDOG_PORT_SubscribeCurrentTask() == (sdk_result == ESP_OK));
        assert(adds == 1 && resets == 0 && statuses == 0 && configurations == 0);
    } else if (is("feed_success") || is("feed_unregistered") || is("feed_uninitialized") || is("feed_error")) {
        if (is("feed_unregistered")) sdk_result = ESP_ERR_NOT_FOUND;
        if (is("feed_uninitialized")) sdk_result = ESP_ERR_INVALID_STATE;
        if (is("feed_error")) sdk_result = ESP_FAIL;
        assert(WATCHDOG_PORT_FeedCurrentTask() == (sdk_result == ESP_OK));
        assert(resets == 1 && adds == 0 && statuses == 0 && configurations == 0);
    } else assert(!"Unknown SDK watchdog adapter case");
    assert(logs == (unsigned)(sdk_result != ESP_OK || (statuses && status_result != ESP_OK && status_result != ESP_ERR_NOT_FOUND)));
    fprintf(stdout, "PASS: ESP32 watchdog adapter %s; SDK calls mocked, no hardware reset.\n", scenario);
    return 0;
}

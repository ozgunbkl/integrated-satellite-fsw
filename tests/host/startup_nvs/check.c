/* Real startup policy; SDK NVS/log boundaries are offline mocks. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "startup_nvs.h"
#include "include/nvs_flash.h"

static esp_err_t injected_result;
static unsigned init_calls, erase_calls, error_logs, name_calls;
static char last_error[256];

esp_err_t nvs_flash_init(void) {
    assert(++init_calls == 1); // No retry, including the two formerly erased cases.
    return injected_result;
}

esp_err_t nvs_flash_erase(void) {
    ++erase_calls;
    assert(!"Startup must not erase NVS");
    return ESP_FAIL;
}

const char *esp_err_to_name(esp_err_t error) {
    ++name_calls;
    assert(error == injected_result);
    switch (error) {
        case ESP_ERR_NVS_NO_FREE_PAGES: return "ESP_ERR_NVS_NO_FREE_PAGES";
        case ESP_ERR_NVS_NEW_VERSION_FOUND: return "ESP_ERR_NVS_NEW_VERSION_FOUND";
        case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
        case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
        case ESP_FAIL: return "ESP_FAIL";
        default: return "UNKNOWN_ERROR";
    }
}

void test_log_error(const char *tag, const char *format, ...) {
    assert(strcmp(tag, "MAIN") == 0);
    ++error_logs;
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(last_error, sizeof(last_error), format, args);
    va_end(args);
    assert(written > 0 && (unsigned)written < sizeof(last_error));
}

int main(int argc, char **argv) {
    const esp_err_t results[] = {
        ESP_OK, ESP_ERR_NVS_NO_FREE_PAGES, ESP_ERR_NVS_NEW_VERSION_FOUND,
        ESP_ERR_NO_MEM, ESP_ERR_NOT_FOUND, ESP_FAIL, 0x7654
    };
    assert(argc == 2);
    const unsigned selected = (unsigned)strtoul(argv[1], NULL, 10);
    assert(selected < sizeof(results) / sizeof(results[0]));
    for (unsigned i = selected; i <= selected; ++i) {
        injected_result = results[i];
        init_calls = erase_calls = error_logs = name_calls = 0;
        last_error[0] = '\0';
        assert(STARTUP_InitNvs() == injected_result); // Must return, not abort.
        assert(init_calls == 1 && erase_calls == 0);
        assert(STARTUP_InitNvs() == injected_result && STARTUP_InitNvs() == injected_result);
        assert(init_calls == 1 && erase_calls == 0); // Shared init result; no second SDK init/retry.
        if (injected_result == ESP_OK) {
            assert(error_logs == 0 && name_calls == 0 && last_error[0] == '\0');
        } else {
            assert(error_logs == 1 && name_calls == 1);
            assert(strstr(last_error, "NVS initialization failed (") != NULL);
            assert(strstr(last_error, "Wi-Fi skipped; automatic NVS erase disabled.") != NULL);
            const char *expected_name = esp_err_to_name(injected_result);
            assert(strstr(last_error, expected_name) != NULL);
        }
    }
    puts("PASS: NVS success and six errors return unchanged after one attempt, no erase/retry/abort;");
    puts("      each failure reports its error and the Wi-Fi-skip/no-auto-erase policy.");
    return 0;
}

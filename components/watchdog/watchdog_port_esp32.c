#include "watchdog_port.h"
#include "watchdog_test.h"
#include "sdkconfig.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>

bool WATCHDOG_PORT_InitRecovery(void) {
    // ESP-IDF app startup owns initialization, before app_main. Reinitializing
    // or deinitializing it here could hide/remove existing SDK subscribers.
    const esp_err_t status = esp_task_wdt_status(NULL);
    if (status != ESP_OK && status != ESP_ERR_NOT_FOUND) {
        ESP_LOGE("WATCHDOG", "SDK task watchdog unavailable (0x%x)", (unsigned)status);
        return false;
    }
    const esp_task_wdt_config_t config = {
        .timeout_ms = WATCHDOG_RECOVERY_TIMEOUT_MS,
        .idle_core_mask = (1U << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1U,
        .trigger_panic = true,
    };
    // The timeout/panic policy is shared by ALL SDK subscribers. Both ESP32
    // idle tasks stay monitored; interrupt WDT configuration is untouched.
    const esp_err_t result = esp_task_wdt_reconfigure(&config);
    if (result != ESP_OK) {
        ESP_LOGE("WATCHDOG", "SDK task watchdog reconfiguration failed (0x%x)", (unsigned)result);
        return false;
    }
    const unsigned reason = (unsigned)esp_reset_reason();
    printf("WATCHDOG: SDK recovery configured timeout=%u idle_mask=%u panic=1 reset_reason=%u.\n",
           (unsigned)config.timeout_ms, (unsigned)config.idle_core_mask, reason);
#if defined(FSW_WDT_TEST_ACTIVE)
    WATCHDOG_TEST_InitRecovery(reason);
#endif
    return true;
}

bool WATCHDOG_PORT_SubscribeCurrentTask(void) {
    const esp_err_t result = esp_task_wdt_add(NULL);
    if (result != ESP_OK) {
        ESP_LOGE("WATCHDOG", "Supervisor subscription failed (0x%x)", (unsigned)result);
        return false;
    }
    return true;
}

bool WATCHDOG_PORT_FeedCurrentTask(void) {
    const esp_err_t result = esp_task_wdt_reset();
    if (result != ESP_OK) {
        ESP_LOGE("WATCHDOG", "Supervisor watchdog feed failed (0x%x)", (unsigned)result);
        return false;
    }
    return true;
}

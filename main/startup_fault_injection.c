#include "startup_fault_test.h"
#if defined(FSW_TEST_STARTUP_FAILURE)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if FSW_TEST_STARTUP_FAILURE == 1
#define STARTUP_PROFILE "hk_init"
#elif FSW_TEST_STARTUP_FAILURE == 2
#define STARTUP_PROFILE "task_early"
#define STARTUP_TARGET_NAME "CMD_PROC"
#define STARTUP_TARGET_PRIORITY 5U
extern void vCommandProcessorTask(void *argument);
#define STARTUP_TARGET_FUNCTION vCommandProcessorTask
#else
#define STARTUP_PROFILE "task_late"
#define STARTUP_TARGET_NAME "SOLAR_HARV"
#define STARTUP_TARGET_PRIORITY 1U
extern void vSolarHarvest_Task(void *argument);
#define STARTUP_TARGET_FUNCTION vSolarHarvest_Task
#endif

static bool s_initialized, s_armed;
extern bool __real_HK_Init(void);

#if defined(FSW_TEST_RECOVERY_BOOT_LOOP)
#include "recovery.h"
_Static_assert(RECOVERY_FAILED_BOOT_LIMIT == 3U, "Selected test requires three failed boots");
bool RECOVERY_LOOP_TEST_Armed(unsigned reason, bool *valid) {
    RecoverySnapshot_t state;
    *valid = RECOVERY_GetSnapshot(&state) && state.storage_ok && !state.stable &&
        state.boot_count != 0 && state.failed_boots <= 3U && (state.flags & RECOVERY_OPEN_BOOT) &&
        !(state.flags & ~(RECOVERY_OPEN_BOOT | RECOVERY_FAULT | RECOVERY_BAD_RESET | RECOVERY_BOOT_LOOP)) &&
        state.reset_reason == reason &&
        (state.failed_boots == 0 ? (reason == ESP_RST_POWERON || reason == ESP_RST_EXT || reason == ESP_RST_SW) : reason == ESP_RST_PANIC) &&
        ((state.flags & RECOVERY_BOOT_LOOP) != 0) == (state.failed_boots == 3U);
    if (!*valid) {
        printf("TEST ERROR: Recovery boot-loop injection refused invalid journal; real HK initialization retained.\n");
        return false; // Never add an abort/reboot loop when storage/counter is uncertain.
    }
    const bool armed = state.failed_boots < 3U;
    printf("TEST: RECOVERY_LOOP injection=%s reset_reason=%u boot=%lu failed_boots=%lu target=3.\n",
           armed ? "armed" : "suppressed", reason,
           (unsigned long)state.boot_count, (unsigned long)state.failed_boots);
    return armed;
}
#endif

bool __wrap_HK_Init(void) {
    if (s_initialized) {
        printf("TEST ERROR: Startup injection HK initialization repeated.\n");
        abort();
    }
    s_initialized = true;
    const unsigned reason = (unsigned)esp_reset_reason();
    // Test-only suppression after the actual SDK panic reset. The reader must
    // first prove an armed boot, the injected error and abort before accepting
    // this second boot. An unrelated first PANIC must not count as a pass.
#if defined(FSW_TEST_RECOVERY_BOOT_LOOP)
    bool valid;
    s_armed = RECOVERY_LOOP_TEST_Armed(reason, &valid);
    if (!valid) return __real_HK_Init();
#else
    s_armed = reason != ESP_RST_PANIC;
#endif
    printf("TEST: STARTUP profile=%s injection=%s reset_reason=%u.\n",
           STARTUP_PROFILE, s_armed ? "armed" : "suppressed", reason);
#if FSW_TEST_STARTUP_FAILURE == 1
    if (s_armed) {
        // Exercise the caller's false-return contract, not real heap exhaustion
        // or the mutex allocator. The real HK allocation path has host tests.
        printf("TEST: STARTUP injected kind=hk_init return=false real_call=skipped.\n");
        if (fflush(stdout) != 0) abort();
        return false;
    }
#endif
    return __real_HK_Init();
}

#if FSW_TEST_STARTUP_FAILURE != 1
_Static_assert(errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY == -1, "Test log requires SDK allocation error -1");
extern BaseType_t __real_xTaskCreatePinnedToCore(TaskFunction_t function,
    const char *name, configSTACK_DEPTH_TYPE stack, void *argument,
    UBaseType_t priority, TaskHandle_t *handle, BaseType_t core);

BaseType_t __wrap_xTaskCreatePinnedToCore(TaskFunction_t function,
    const char *name, configSTACK_DEPTH_TYPE stack, void *argument,
    UBaseType_t priority, TaskHandle_t *handle, BaseType_t core) {
    // IDF 5.5 xTaskCreate is always_inline and calls PinnedToCore. Never fail a
    // Wi-Fi/SDK/other app task: identify the selected required task by function,
    // then verify its complete metadata before injecting a return code.
    if (function != STARTUP_TARGET_FUNCTION)
        return __real_xTaskCreatePinnedToCore(function, name, stack, argument, priority, handle, core);
    static bool target_seen;
    if (!s_initialized || target_seen || name == NULL || strcmp(name, STARTUP_TARGET_NAME) != 0 ||
            stack != 4096U || argument != NULL || priority != STARTUP_TARGET_PRIORITY ||
            handle != NULL || core != tskNO_AFFINITY) {
        printf("TEST ERROR: Startup target metadata/order/retry mismatch.\n");
        abort();
    }
    target_seen = true;
    if (s_armed) {
        printf("TEST: STARTUP injected kind=task_create task=%s return=-1 real_call=skipped.\n", STARTUP_TARGET_NAME);
        if (fflush(stdout) != 0) abort();
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }
    const BaseType_t result = __real_xTaskCreatePinnedToCore(function, name, stack, argument, priority, handle, core);
    printf("TEST: STARTUP target=%s real_create_return=%ld.\n", STARTUP_TARGET_NAME, (long)result);
    return result;
}
#endif
#endif

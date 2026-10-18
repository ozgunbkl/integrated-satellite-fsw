#include "watchdog_test.h"
#ifdef FSW_TEST_WDT_GLOBAL_STARVATION
#include "esp_system.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if CONFIG_FREERTOS_NUMBER_OF_CORES != 2 || configMAX_PRIORITIES != 25
#error "Global starvation requires the pinned dual-core/25-priority configuration"
#endif

static bool s_initialized, s_armed;
static unsigned s_checks;
static atomic_uint s_ready;
static atomic_bool s_release;

static void global_worker(void *argument) {
    const unsigned core = (unsigned)(uintptr_t)argument;
    if (core > 1 || (unsigned)xPortGetCoreID() != core) {
        printf("TEST ERROR: Global worker affinity mismatch.\n");
        abort();
    }
    atomic_fetch_or_explicit(&s_ready, 1U << core, memory_order_release);
    // Let WDT_MON finish creating BOTH workers and flush the evidence marker.
    // No mutex/SDK/stdout operation remains after the release gate opens.
    while (!atomic_load_explicit(&s_release, memory_order_acquire)) vTaskDelay(1);
    for (;;) { atomic_signal_fence(memory_order_seq_cst); }
}

void WATCHDOG_TEST_InitRecovery(unsigned reset_reason) {
    if (s_initialized) {
        printf("TEST ERROR: Watchdog injection initialized twice.\n");
        abort();
    }
    s_initialized = true;
    s_armed = reset_reason != ESP_RST_TASK_WDT;
    printf("TEST: WDT profile=global_starvation injection=%s reset_reason=%u cores=3.\n",
           s_armed ? "armed" : "suppressed", reset_reason);
}

bool WATCHDOG_TEST_DropPet(WatchdogTaskID_t task) {
    (void)task;
    return false;
}

void WATCHDOG_TEST_AfterCheck(void) {
    if (!s_initialized) {
        printf("TEST ERROR: Watchdog injection not initialized.\n");
        return;
    }
    printf("TEST: WDT stack_free_min_bytes=%u.\n",
           (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    printf("TEST: WDT starvation supervisor check=%u feed=ok.\n", ++s_checks);
    if (!s_armed || s_checks != 6) return;
    s_armed = false;
    for (unsigned core = 0; core < 2; ++core) {
        if (xTaskCreatePinnedToCore(global_worker, core ? "WDT_G1" : "WDT_G0", 4096,
                                   (void *)(uintptr_t)core, configMAX_PRIORITIES - 1, NULL, (BaseType_t)core) != pdPASS) {
            printf("TEST ERROR: Global starvation worker creation failed.\n");
            abort();
        }
    }
    for (unsigned attempt = 0; attempt < 100 && atomic_load_explicit(&s_ready, memory_order_acquire) != 3U; ++attempt)
        vTaskDelay(1);
    if (atomic_load_explicit(&s_ready, memory_order_acquire) != 3U) {
        printf("TEST ERROR: Global starvation workers not ready.\n");
        abort();
    }
    printf("TEST: WDT global starvation injected cores=3 ready=3 priority=24 interrupts=enabled.\n");
    if (fflush(stdout) != 0) abort();
    // IRQs stay enabled. Priority 24 starves WDT_MON (6) and both idle tasks;
    // equal-priority EPS work may still time-slice. This is not all-task deadlock.
    atomic_store_explicit(&s_release, true, memory_order_release);
}
#endif

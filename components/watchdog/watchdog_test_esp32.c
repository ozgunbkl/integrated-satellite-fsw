#include "watchdog_test.h"
#if defined(FSW_WDT_TEST_ACTIVE) && !defined(FSW_TEST_WDT_GLOBAL_STARVATION) && !defined(FSW_TEST_WDT_TASK_RECOVERY)
#include "watchdog.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system_mode.h"
#include "fdir_service.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static bool s_initialized;
#ifdef FSW_TEST_WDT_TASK_LOSS
static atomic_flag s_drop_announced = ATOMIC_FLAG_INIT;
// Sole reader is WDT_MON. Keep the bounded snapshot off its small task stack.
static FDIR_ActiveFaultSnapshot_t s_snapshot;
#else
static bool s_stall_armed;
static unsigned s_completed_checks;
#endif

#if defined(FSW_TEST_WDT_IDLE_STARVATION) || defined(FSW_TEST_WDT_INTERRUPT_MASK)
#if CONFIG_FREERTOS_NUMBER_OF_CORES != 2 || CONFIG_ESP_INT_WDT_TIMEOUT_MS != 300
#error "Starvation tests require the pinned dual-core/300ms IWDT configuration"
#endif
#if defined(FSW_TEST_WDT_INTERRUPT_MASK) && FSW_TEST_WDT_CORE == 1 && !CONFIG_ESP_INT_WDT_CHECK_CPU1
#error "CPU1 interrupt test requires SDK CPU1 interrupt watchdog checks"
#endif
#ifdef FSW_TEST_WDT_IDLE_STARVATION
#define STARVATION_PROFILE "idle_starvation"
#define STARVATION_RESET ESP_RST_TASK_WDT
#else
#define STARVATION_PROFILE "interrupt_mask"
#define STARVATION_RESET ESP_RST_INT_WDT
#endif

static void starvation_worker(void *argument) {
    (void)argument;
    // All app/stdout locks are released before spinning or masking IRQs. No
    // custom ISR, watchdog feed, esp_restart, NVS or application state mutation.
#ifdef FSW_TEST_WDT_IDLE_STARVATION
    printf("TEST: WDT idle starvation injected core=%u interrupts=enabled.\n", (unsigned)FSW_TEST_WDT_CORE);
#else
    printf("TEST: WDT interrupt masking injected core=%u timeout_ms=300.\n", (unsigned)FSW_TEST_WDT_CORE);
#endif
    if (fflush(stdout) != 0) abort();
#ifdef FSW_TEST_WDT_INTERRUPT_MASK
    portDISABLE_INTERRUPTS();
#endif
    // A compiler-only fence keeps this a busy loop; no SDK call or yield.
    for (;;) { atomic_signal_fence(memory_order_seq_cst); }
}
#endif

void WATCHDOG_TEST_InitRecovery(unsigned reset_reason) {
    if (s_initialized) {
        printf("TEST ERROR: Watchdog injection initialized twice.\n");
        abort();
    }
    s_initialized = true;
#ifdef FSW_TEST_WDT_TASK_LOSS
    (void)reset_reason;
    printf("TEST: WDT profile=task_loss target=0; no task suspension or SDK feed change.\n");
#elif defined(FSW_TEST_WDT_SUPERVISOR_STALL)
    // SDK reports the actual TWDT cause after its panic/reset. Suppression is
    // test-only, not persistent repair or a production reboot-loop policy.
    s_stall_armed = reset_reason != ESP_RST_TASK_WDT;
    printf("TEST: WDT profile=supervisor_stall injection=%s reset_reason=%u.\n",
           s_stall_armed ? "armed" : "suppressed", reset_reason);
#else
    s_stall_armed = reset_reason != STARVATION_RESET;
    printf("TEST: WDT profile=%s injection=%s reset_reason=%u core=%u.\n",
           STARVATION_PROFILE, s_stall_armed ? "armed" : "suppressed", reset_reason,
           (unsigned)FSW_TEST_WDT_CORE);
#endif
}

bool WATCHDOG_TEST_DropPet(WatchdogTaskID_t task) {
    if (!s_initialized) return false;
#ifdef FSW_TEST_WDT_TASK_LOSS
    if (task != WDT_TASK_TM_GEN) return false;
    if (!atomic_flag_test_and_set(&s_drop_announced))
        printf("TEST: WDT heartbeat loss injected target=0.\n");
    return true;
#else
    (void)task;
    return false;
#endif
}

void WATCHDOG_TEST_AfterCheck(void) {
    if (!s_initialized) {
        printf("TEST ERROR: Watchdog injection not initialized.\n");
        return;
    }
    // ESP32 StackType_t is uint8_t. Explicit conversion also keeps host units clear.
    printf("TEST: WDT stack_free_min_bytes=%u.\n",
           (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
#ifdef FSW_TEST_WDT_TASK_LOSS
    if (!FDIR_GetActiveFaults(&s_snapshot)) {
        printf("TEST ERROR: Watchdog fault snapshot unavailable.\n");
        return;
    }
    unsigned active = 0;
    unsigned reports = 0;
    for (unsigned i = 0; i < s_snapshot.count; ++i) {
        const SystemFaultRecord_t *record = &s_snapshot.records[i];
        if (record->source == SRC_CDH && record->code == FAULT_WDT_TIMEOUT_BASE && !record->fatal) {
            active = 1;
            reports = record->reports;
        }
    }
    printf("TEST: WDT task_loss mode=%u active=%u reports=%u uncertain=%u overflow=%u feed=ok.\n",
           (unsigned)SYSTEM_MODE_Get(), active, reports,
           (unsigned)s_snapshot.recording_uncertain, (unsigned)s_snapshot.overflow);
#elif defined(FSW_TEST_WDT_SUPERVISOR_STALL)
    if (s_stall_armed && ++s_completed_checks == 6) {
        s_stall_armed = false;
        printf("TEST: WDT supervisor stall injected after 6 completed checks.\n");
        // Self-suspend AFTER successful check/feed, outside all app locks.
        // Idle tasks and mission tasks remain scheduled; no interrupt masking.
        vTaskSuspend(NULL);
        printf("TEST ERROR: Suspended watchdog resumed unexpectedly.\n");
    }
#else
    ++s_completed_checks;
    printf("TEST: WDT starvation supervisor check=%u feed=ok.\n", s_completed_checks);
    if (s_stall_armed && s_completed_checks == 6) {
        s_stall_armed = false;
        // prio=1 starves only the core's idle task; mission/SDK higher-priority
        // work can preempt until the interrupt-mask worker disables IRQs.
        if (xTaskCreatePinnedToCore(starvation_worker, "WDT_FAULT", 4096, NULL, 1, NULL, FSW_TEST_WDT_CORE) != pdPASS) {
            printf("TEST ERROR: Starvation worker creation failed.\n");
            abort();
        }
    }
#endif
}
#endif

#include "watchdog.h"
#include "watchdog_port.h"
#include "watchdog_test.h"
#include "fdir_service.h"
#include "time_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static SemaphoreHandle_t s_watchdog_mutex;
static bool s_initialized;
static TickType_t s_last_pet[WDT_TASK_COUNT];
static bool s_timeout_latched[WDT_TASK_COUNT];
static WATCHDOG_TaskTiming_t s_timing[WDT_TASK_COUNT];
typedef struct {
    uint32_t revision;
    TickType_t healthy_since;
    uint8_t samples;
} WatchdogRecovery_t;
static WatchdogRecovery_t s_recovery[WDT_TASK_COUNT];
static atomic_bool s_armed = ATOMIC_VAR_INIT(false);
static atomic_flag s_state_fault_reported = ATOMIC_FLAG_INIT;

static bool watchdog_lock(void) {
    return s_watchdog_mutex != NULL &&
           xSemaphoreTake(s_watchdog_mutex, pdMS_TO_TICKS(10)) == pdTRUE;
}

static void watchdog_report_state_failure(void) {
    if (!atomic_flag_test_and_set(&s_state_fault_reported)) {
        FDIR_ReportFault((FaultReport_t){.source = SRC_CDH, .severity = FAULT_CRITICAL,
            .fault_code = FAULT_WDT_STATE_UNAVAILABLE, .timestamp = TIME_GetSeconds()});
        printf("WATCHDOG: Heartbeat state unavailable; critical cause retained; no recovery feed for failed checks.\n");
    }
}

bool WATCHDOG_Init(void) {
    if (s_watchdog_mutex != NULL) return false;
    s_watchdog_mutex = xSemaphoreCreateMutex();
    if (s_watchdog_mutex == NULL) return false;
    if (!WATCHDOG_PORT_InitRecovery()) return false;
    s_initialized = true;
    // Static state is initially unarmed. Tasks do not exist yet; repeated init
    // is rejected rather than clearing timestamps, latches or safety records.
    printf("WATCHDOG: Heartbeat service initialized; SDK recovery timeout %u ms (panic/reset, idle cores monitored).\n",
           (unsigned)WATCHDOG_RECOVERY_TIMEOUT_MS);
    return true;
}

bool WATCHDOG_Arm(void) {
    if (!s_initialized) return false;
    if (!watchdog_lock()) return false;
    if (atomic_load(&s_armed)) {
        xSemaphoreGive(s_watchdog_mutex);
        return false;
    }
    const TickType_t epoch = xTaskGetTickCount();
    for (unsigned i = 0; i < WDT_TASK_COUNT; ++i) {
        s_last_pet[i] = epoch;
        s_timing[i] = (WATCHDOG_TaskTiming_t){0};
    }
    atomic_store(&s_armed, true);
    xSemaphoreGive(s_watchdog_mutex);
    printf("WATCHDOG: Supervision armed before task creation (%u critical tasks).\n", (unsigned)WDT_TASK_COUNT);
    return true;
}

void watchdog_pet(WatchdogTaskID_t task_id) {
    if ((unsigned int)task_id >= (unsigned int)WDT_TASK_COUNT || !atomic_load(&s_armed)) return;
#if defined(FSW_WDT_TEST_ACTIVE)
    if (WATCHDOG_TEST_DropPet(task_id)) return;
#endif
    if (!watchdog_lock()) {
        watchdog_report_state_failure();
        return;
    }
    const TickType_t now = xTaskGetTickCount();
    WatchdogRecovery_t *recovery = &s_recovery[task_id];
    if (s_timeout_latched[task_id] && recovery->revision != 0U) {
        const TickType_t gap = (TickType_t)(now - s_last_pet[task_id]);
        if (recovery->samples == 0U || gap > pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS)) {
            recovery->healthy_since = now;
            recovery->samples = 1U;
        } else if (gap != 0U && recovery->samples < WATCHDOG_RECOVERY_MIN_PETS) {
            ++recovery->samples;
        }
    }
    WATCHDOG_TaskTiming_t *timing = &s_timing[task_id];
    if (timing->pet_count != 0U) {
        const uint32_t interval_ms = (uint32_t)((TickType_t)(now - s_last_pet[task_id]) * portTICK_PERIOD_MS);
        if (timing->pet_count == 1U || interval_ms < timing->min_interval_ms) timing->min_interval_ms = interval_ms;
        if (interval_ms > timing->max_interval_ms) timing->max_interval_ms = interval_ms;
    }
    if (timing->pet_count != UINT32_MAX) ++timing->pet_count;
    s_last_pet[task_id] = now;
    xSemaphoreGive(s_watchdog_mutex);
}

bool WATCHDOG_GetTimingSnapshot(WATCHDOG_TaskTiming_t out[WDT_TASK_COUNT]) {
    if (out == NULL || !atomic_load(&s_armed) || !watchdog_lock()) return false;
    const TickType_t now = xTaskGetTickCount();
    for (unsigned i = 0; i < WDT_TASK_COUNT; ++i) {
        out[i] = s_timing[i];
        out[i].age_ms = (uint32_t)((TickType_t)(now - s_last_pet[i]) * portTICK_PERIOD_MS);
    }
    xSemaphoreGive(s_watchdog_mutex);
    return true;
}

// Heartbeat -> MODE is the only nested lock order here. These two FDIR APIs
// try only the quiet MODE ledger without waiting: no warning mutex/logs/I/O.
// Capture a revision BEFORE collecting fresh liveness evidence. Re-report,
// query/resolve failure or an actual heartbeat gap invalidates the window.
static bool watchdog_recover_locked(unsigned task, TickType_t now) {
    WatchdogRecovery_t *recovery = &s_recovery[task];
    const uint16_t code = (uint16_t)(FAULT_WDT_TIMEOUT_BASE + task);
    uint32_t revision = 0;
    if (!FDIR_TryGetFaultRevision(SRC_CDH, code, &revision) || revision == 0U) {
        *recovery = (WatchdogRecovery_t){0};
        return false;
    }
    if (revision != recovery->revision) {
        *recovery = (WatchdogRecovery_t){.revision = revision};
        return false;
    }
    const TickType_t age = (TickType_t)(now - s_last_pet[task]);
    if (age > pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS)) {
        recovery->samples = 0U;
        recovery->healthy_since = 0;
        return false;
    }
    if (recovery->samples < WATCHDOG_RECOVERY_MIN_PETS ||
        (TickType_t)(now - recovery->healthy_since) < pdMS_TO_TICKS(WATCHDOG_RECOVERY_WINDOW_MS) ||
        age > pdMS_TO_TICKS(WATCHDOG_RECOVERY_FRESH_MS)) return false;

    if (!FDIR_TryResolveCriticalFault(SRC_CDH, code, recovery->revision)) {
        *recovery = (WatchdogRecovery_t){0};
        return false;
    }
    s_timeout_latched[task] = false;
    *recovery = (WatchdogRecovery_t){0};
    return true;
}

bool WATCHDOG_Check(void) {
    if (!atomic_load(&s_armed)) return false;
    if (!watchdog_lock()) {
        watchdog_report_state_failure();
        return false;
    }
    bool expired[WDT_TASK_COUNT] = {false};
    bool recovered[WDT_TASK_COUNT] = {false};
    // Tick sampling and all heartbeat reads share the same lock as pet writes:
    // a concurrent future pet cannot make unsigned subtraction falsely expire.
    const TickType_t now = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS);
    for (unsigned i = 0; i < WDT_TASK_COUNT; ++i) {
        const TickType_t elapsed = (TickType_t)(now - s_last_pet[i]);
        if (!s_timeout_latched[i] && elapsed > timeout) {
            s_timeout_latched[i] = true;
            s_recovery[i] = (WatchdogRecovery_t){0};
            expired[i] = true;
        } else if (s_timeout_latched[i]) {
            recovered[i] = watchdog_recover_locked(i, now);
        }
    }
    xSemaphoreGive(s_watchdog_mutex);

    // Supervisor-only Check calls are serialized. A new timeout is reported
    // before a subsequent Check can capture its recovery revision. A single
    // resumed pet cannot erase the cause; each unrecovered episode reports once.
    for (unsigned i = 0; i < WDT_TASK_COUNT; ++i) {
        if (expired[i]) {
            FDIR_ReportFault((FaultReport_t){.source = SRC_CDH, .severity = FAULT_CRITICAL,
                .fault_code = (uint16_t)(FAULT_WDT_TIMEOUT_BASE + i), .timestamp = TIME_GetSeconds()});
            printf("WATCHDOG: !!! CRITICAL FAILURE: Task %u timed out !!! Cause retained; no task-triggered reset.\n", i);
        }
        if (recovered[i]) {
            printf("WATCHDOG: Task %u stable; timeout resolved; mode unchanged.\n", i);
        }
    }
    return true;
}

void vSoftwareWatchdogTask(void *pvParameters) {
    (void)pvParameters;
    // Arm was done by app_main. Never overwrite an early task heartbeat here.
    if (!WATCHDOG_PORT_SubscribeCurrentTask()) {
        // Required protection cannot be silently omitted. SDK API failure is
        // an immediate panic/reset path, not a fabricated task-timeout fault.
        abort();
    }
    printf("WATCHDOG: Software Watchdog initialized.\n");
    for (;;) {
        // Initial successful check also covers the first subscription interval.
        // A completed check may have latched an individual task fault: keep
        // supervising SAFE, without resetting just because that task stopped.
        // False/stalled checks do NOT feed. SDK can time out independently.
        if (WATCHDOG_Check()) {
            if (!WATCHDOG_PORT_FeedCurrentTask()) abort();
#if defined(FSW_WDT_TEST_ACTIVE)
            WATCHDOG_TEST_AfterCheck();
#endif
        }
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_CHECK_MS));
    }
}

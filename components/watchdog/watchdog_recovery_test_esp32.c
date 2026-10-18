#include "watchdog_test.h"
#if defined(FSW_TEST_WDT_TASK_RECOVERY)
#include "watchdog.h"
#include "fdir_service.h"
#include "system_mode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static bool s_initialized;
static atomic_uint s_stage = ATOMIC_VAR_INIT(0);
static atomic_uint s_announced = ATOMIC_VAR_INIT(0);
// WDT_MON is the only snapshot reader. Keep the ledger off its task stack.
static FDIR_ActiveFaultSnapshot_t s_snapshot;

void WATCHDOG_TEST_InitRecovery(unsigned reset_reason) {
    (void)reset_reason;
    if (s_initialized) {
        printf("TEST ERROR: Watchdog recovery injection initialized twice.\n");
        abort();
    }
    s_initialized = true;
    printf("TEST: WDT profile=task_recovery target=0.\n");
}

bool WATCHDOG_TEST_DropPet(WatchdogTaskID_t task) {
    if (!s_initialized || task != WDT_TASK_TM_GEN) return false;
    const unsigned stage = atomic_load(&s_stage);
    if (stage != 0U && stage != 3U) return false;
    const unsigned bit = 1U << stage;
    if ((atomic_fetch_or(&s_announced, bit) & bit) == 0U)
        printf("TEST: WDT recovery drop=%u target=0.\n", stage == 0U ? 1U : 2U);
    return true;
}

void WATCHDOG_TEST_AfterCheck(void) {
    if (!s_initialized) {
        printf("TEST ERROR: Watchdog recovery injection not initialized.\n");
        return;
    }
    printf("TEST: WDT stack_free_min_bytes=%u.\n",
           (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    if (!FDIR_GetActiveFaults(&s_snapshot)) {
        printf("TEST ERROR: Watchdog recovery snapshot unavailable.\n");
        return;
    }
    unsigned active = 0;
    uint32_t revision = 0;
    for (unsigned i = 0; i < s_snapshot.count; ++i) {
        const SystemFaultRecord_t *record = &s_snapshot.records[i];
        if (record->source == SRC_CDH && record->code == FAULT_WDT_TIMEOUT_BASE &&
            !record->fatal && record->reports == 1U && record->revision != 0U) {
            ++active;
            revision = record->revision;
        }
    }
    if (s_snapshot.count != active || active > 1U ||
        s_snapshot.recording_uncertain || s_snapshot.overflow) {
        printf("TEST ERROR: Watchdog recovery unexpected fault ledger.\n");
        return;
    }
    const SystemMode_t mode = SYSTEM_MODE_Get();
    unsigned stage = atomic_load(&s_stage);
    if ((stage == 1U || stage == 4U) && !active && mode != MODE_SAFE) {
        printf("TEST ERROR: Watchdog recovery changed mode without ground.\n");
        return;
    }
    if ((stage == 0U || stage == 3U) && active) {
        stage = stage == 0U ? 1U : 4U;
        atomic_store(&s_stage, stage);
        printf("TEST: WDT recovery resume=%u.\n", stage == 1U ? 1U : 2U);
    } else if ((stage == 1U || stage == 4U) && !active) {
        stage = stage == 1U ? 2U : 5U;
        atomic_store(&s_stage, stage);
        printf("TEST: WDT recovery ready_ground=%u.\n", stage == 2U ? 1U : 2U);
    } else if ((stage == 2U || stage == 5U) &&
               (mode == MODE_NOMINAL || mode == MODE_DEGRADED)) {
        stage = stage == 2U ? 3U : 6U;
        atomic_store(&s_stage, stage);
        printf("TEST: WDT recovery operating=%u mode=%u.\n", stage == 3U ? 1U : 2U, (unsigned)mode);
    }
    printf("TEST: WDT recovery stage=%u mode=%u active=%u revision=%lu count=%u uncertain=0 overflow=0 feed=ok.\n",
           stage, (unsigned)mode, active, (unsigned long)revision, (unsigned)s_snapshot.count);
}
#endif

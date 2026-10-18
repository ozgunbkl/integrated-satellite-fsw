/* Actual supervisor + FDIR/MODE; deterministic SMP ordering, no target/network. */
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "watchdog.h"
#include "watchdog_port.h"
#include "fdir_service.h"
#include "system_mode.h"
#include "time_service.h"
#include "yamcs_interface.h"
#include "commands.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

void __real_FDIR_ReportFault(FaultReport_t report);
bool __real_FDIR_TryGetFaultRevision(FaultSource_t source, uint16_t code, uint32_t *revision);
bool __real_FDIR_TryResolveCriticalFault(FaultSource_t source, uint16_t code, uint32_t revision);
typedef struct { bool held; } Mutex;
static Mutex mode_mutex, fault_mutex, watchdog_mutex;
static unsigned allocations, fail_watchdog, fail_mode, reports, tick_reads, delays;
static bool fail_allocation, pet_before_take, pet_after_unlock;
static WatchdogTaskID_t injected_task;
static TickType_t now;
static jmp_buf task_exit;
static char logs[65536], event[256];
static size_t log_length;
static const char *scenario;
static unsigned sdk_configs, sdk_subscriptions, sdk_feeds, aborts;
static bool fail_sdk_config, fail_sdk_subscribe, fail_sdk_feed, subscribed, expect_abort;
static bool quiet_ledger_access, fail_recovery_query, injecting_report;
static unsigned recovery_queries, recovery_resolves;
enum RecoveryRace { RECOVERY_NONE, RECOVERY_LOCK, RECOVERY_REREPORT, RECOVERY_FATAL, RECOVERY_AFTER_CLEAR };
static enum RecoveryRace recovery_race;

static bool is(const char *name) { return strcmp(scenario, name) == 0; }
static void unlocked(void) {
    assert(!watchdog_mutex.held && !mode_mutex.held && !fault_mutex.held);
}
bool WATCHDOG_PORT_InitRecovery(void) {
    unlocked(); assert(allocations == 3 && WATCHDOG_RECOVERY_TIMEOUT_MS > WATCHDOG_CHECK_MS);
    ++sdk_configs; return !fail_sdk_config;
}
bool WATCHDOG_PORT_SubscribeCurrentTask(void) {
    unlocked(); assert(sdk_configs == 1 && !subscribed);
    // Subscribe BEFORE even the monitor-start log, so a blocked log is covered.
    assert(strstr(logs, "Software Watchdog initialized") == NULL);
    ++sdk_subscriptions; subscribed = !fail_sdk_subscribe; return subscribed;
}
bool WATCHDOG_PORT_FeedCurrentTask(void) {
    unlocked(); assert(subscribed && tick_reads != 0);
    if (reports) assert(SYSTEM_MODE_Get() == MODE_SAFE);
    ++sdk_feeds; return !fail_sdk_feed;
}
void __wrap_abort(void) {
    unlocked(); assert(expect_abort && sdk_subscriptions == 1);
    ++aborts; longjmp(task_exit, 1);
}
static void all_pets_except(int excluded) {
    for (unsigned i = 0; i < WDT_TASK_COUNT; ++i) if ((int)i != excluded) watchdog_pet((WatchdogTaskID_t)i);
}
static FDIR_ActiveFaultSnapshot_t snapshot(void) {
    FDIR_ActiveFaultSnapshot_t out; assert(FDIR_GetActiveFaults(&out)); return out;
}
static void expect_task(unsigned task) {
    FDIR_ActiveFaultSnapshot_t out = snapshot();
    assert(out.count == 1 && out.records[0].source == SRC_CDH && !out.records[0].fatal);
    assert(out.records[0].code == FAULT_WDT_TIMEOUT_BASE + task && out.records[0].reports == 1);
    assert(FDIR_HasActiveFaults());
}
static void start(void) { assert(WATCHDOG_Init() && WATCHDOG_Arm()); }
static void expire(unsigned task) {
    now += pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS) + 1;
    all_pets_except((int)task); assert(WATCHDOG_Check()); expect_task(task);
}
int __wrap_printf(const char *format, ...) {
    assert(!watchdog_mutex.held && !mode_mutex.held && !fault_mutex.held);
    va_list args; va_start(args, format);
    int count = vsnprintf(logs + log_length, sizeof(logs) - log_length, format, args); va_end(args);
    assert(count >= 0 && (size_t)count < sizeof(logs) - log_length); log_length += (size_t)count;
    return count;
}
bool YAMCS_SendEvent(const char *format, ...) {
    assert(!watchdog_mutex.held && !mode_mutex.held && !fault_mutex.held);
    va_list args; va_start(args, format); vsnprintf(event, sizeof(event), format, args); va_end(args);
    return true;
}
uint32_t TIME_GetSeconds(void) {
    assert(!watchdog_mutex.held && !mode_mutex.held && !fault_mutex.held); return 42;
}
void __wrap_FDIR_ReportFault(FaultReport_t report) {
    assert(!watchdog_mutex.held && !mode_mutex.held && !fault_mutex.held);
    assert(report.source == SRC_CDH && report.severity == FAULT_CRITICAL && report.timestamp == 42);
    assert((report.fault_code >= FAULT_WDT_TIMEOUT_BASE && report.fault_code < FAULT_WDT_TIMEOUT_BASE + WDT_TASK_COUNT) ||
           report.fault_code == FAULT_WDT_STATE_UNAVAILABLE);
    if (is("monitor_report_stall")) {
        assert(subscribed && sdk_feeds == 4 && now == pdMS_TO_TICKS(20000));
        // Model a report that never returns. The incomplete check must not feed.
        longjmp(task_exit, 1);
    }
    ++reports; __real_FDIR_ReportFault(report);
}
bool __wrap_FDIR_TryGetFaultRevision(FaultSource_t source, uint16_t code, uint32_t *revision) {
    assert(watchdog_mutex.held && !mode_mutex.held && !fault_mutex.held && !quiet_ledger_access);
    assert(source == SRC_CDH && code >= FAULT_WDT_TIMEOUT_BASE && code < FAULT_WDT_TIMEOUT_BASE + WDT_TASK_COUNT);
    ++recovery_queries;
    if (fail_recovery_query) { fail_recovery_query = false; return false; }
    quiet_ledger_access = true;
    const bool result = __real_FDIR_TryGetFaultRevision(source, code, revision);
    quiet_ledger_access = false;
    return result;
}
bool __wrap_FDIR_TryResolveCriticalFault(FaultSource_t source, uint16_t code, uint32_t revision) {
    assert(watchdog_mutex.held && !mode_mutex.held && !fault_mutex.held && !quiet_ledger_access);
    assert(source == SRC_CDH && code >= FAULT_WDT_TIMEOUT_BASE && code < FAULT_WDT_TIMEOUT_BASE + WDT_TASK_COUNT && revision != 0);
    ++recovery_resolves;
    quiet_ledger_access = true;
    const enum RecoveryRace race = recovery_race;
    recovery_race = RECOVERY_NONE;
    if (race == RECOVERY_REREPORT || race == RECOVERY_FATAL) {
        injecting_report = true; // Model the competing reporting core, not recovery's API.
        assert(SYSTEM_MODE_RecordFault((uint8_t)source, code, race == RECOVERY_FATAL, 42));
        injecting_report = false;
    }
    if (race == RECOVERY_LOCK) fail_mode = 1;
    const bool result = __real_FDIR_TryResolveCriticalFault(source, code, revision);
    if (race == RECOVERY_AFTER_CLEAR) {
        injecting_report = true;
        assert(result && SYSTEM_MODE_RecordFault((uint8_t)source, code, false, 42));
        injecting_report = false;
    }
    quiet_ledger_access = false;
    return result;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    if (fail_allocation) return NULL;
    assert(allocations < 3);
    return allocations++ == 0 ? &mode_mutex : (allocations == 2 ? &fault_mutex : &watchdog_mutex);
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks) {
    assert(handle == &mode_mutex || handle == &fault_mutex || handle == &watchdog_mutex);
    Mutex *mutex = handle; assert(!mutex->held);
    assert(ticks == portMAX_DELAY || ticks == pdMS_TO_TICKS(10) ||
           (ticks == 0 && handle == &mode_mutex && quiet_ledger_access));
    if (handle == &watchdog_mutex) {
        assert(!mode_mutex.held && !fault_mutex.held && ticks == pdMS_TO_TICKS(10));
        if (pet_before_take) {
            pet_before_take = false; ++now; watchdog_pet(injected_task);
        }
        if (fail_watchdog) { --fail_watchdog; return pdFALSE; }
    } else {
        // Only the owner recovery's quiet MODE-ledger query/resolve may nest.
        assert(!watchdog_mutex.held || (handle == &mode_mutex && quiet_ledger_access));
        if (watchdog_mutex.held && handle == &mode_mutex)
            assert(ticks == (injecting_report ? pdMS_TO_TICKS(10) : 0));
        if (handle == &mode_mutex && fail_mode) { --fail_mode; return pdFALSE; }
    }
    mutex->held = true; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
    Mutex *mutex = handle; assert(mutex->held); mutex->held = false;
    if (handle == &watchdog_mutex && pet_after_unlock) {
        pet_after_unlock = false; ++now; watchdog_pet(injected_task);
    }
    return pdTRUE;
}
TickType_t xTaskGetTickCount(void) {
    assert(watchdog_mutex.held && !mode_mutex.held && !fault_mutex.held);
    ++tick_reads; return now;
}
void vTaskDelay(TickType_t ticks) {
    assert(ticks == pdMS_TO_TICKS(WATCHDOG_CHECK_MS));
    assert(!watchdog_mutex.held && !mode_mutex.held && !fault_mutex.held);
    ++delays;
    if (is("monitor_healthy")) {
        assert(sdk_feeds == delays && reports == 0 && SYSTEM_MODE_Get() == MODE_NOMINAL);
        if (delays == 5) longjmp(task_exit, 1);
        now += ticks; all_pets_except(-1); return;
    }
    if (is("monitor_check_recovers")) {
        assert(reports == 1 && snapshot().records[0].code == FAULT_WDT_STATE_UNAVAILABLE);
        if (delays == 2) { assert(sdk_feeds == 1); longjmp(task_exit, 1); }
        assert(sdk_feeds == 0); now += ticks; all_pets_except(-1); return;
    }
    if (is("monitor_check_stalls") || is("monitor_unarmed")) {
        assert(sdk_feeds == 0 && reports == (unsigned)is("monitor_check_stalls"));
        now += ticks;
        if (delays == 3) {
            assert(now == pdMS_TO_TICKS(WATCHDOG_RECOVERY_TIMEOUT_MS));
            // Only proves no feed for a full budget; no real SDK timer/reset here.
            longjmp(task_exit, 1);
        }
        return;
    }
    const unsigned stop = is("monitor_no_reset") ? 4 : 5;
    if (delays == stop) {
        expect_task(WDT_TASK_TM_GEN); assert(reports == 1 && SYSTEM_MODE_Get() == MODE_SAFE);
        assert(sdk_subscriptions == 1 && sdk_feeds == delays);
        longjmp(task_exit, 1);
    }
    assert(reports == 0); // No timeout at the exact 15 s boundary.
    now += ticks; all_pets_except(WDT_TASK_TM_GEN);
}

static uint32_t timeout_revision(unsigned task) {
    const FDIR_ActiveFaultSnapshot_t out = snapshot();
    for (unsigned i = 0; i < out.count; ++i) {
        if (out.records[i].source == SRC_CDH && out.records[i].code == FAULT_WDT_TIMEOUT_BASE + task)
            return out.records[i].revision;
    }
    return 0;
}

static void recovery_baseline(void) {
    all_pets_except(-1);
    assert(WATCHDOG_Check()); // Capture revision, discard earlier evidence.
    all_pets_except(-1);      // First post-revision sample, even at tick zero.
}

static void healthy_steps(unsigned count) {
    for (unsigned step = 0; step < count; ++step) {
        now += pdMS_TO_TICKS(WATCHDOG_CHECK_MS);
        all_pets_except(-1);
        assert(WATCHDOG_Check());
    }
}

static bool recovery_cases(void) {
    if (strncmp(scenario, "recovery_", 9) != 0) return false;
    unsigned task = 0;
    bool task_case = false, relapse = false, boundary = false, wrap = false;
    char candidate[64];
    for (unsigned i = 0; i < WDT_TASK_COUNT; ++i) {
        snprintf(candidate, sizeof(candidate), "recovery_task_%u", i);
        if (is(candidate)) { task = i; task_case = true; }
        snprintf(candidate, sizeof(candidate), "recovery_relapse_%u", i);
        if (is(candidate)) { task = i; relapse = true; }
        snprintf(candidate, sizeof(candidate), "recovery_boundary_%u", i);
        if (is(candidate)) { task = i; boundary = true; }
        snprintf(candidate, sizeof(candidate), "recovery_wrap_%u", i);
        if (is(candidate)) { task = i; wrap = true; }
    }
    // Task 0 places the first recovery pet at tick zero; other tasks cross wrap
    // during the window. Zero is a valid epoch, not an "uninitialized" sentinel.
    if (wrap) now = UINT32_MAX - pdMS_TO_TICKS(task == 0 ? WATCHDOG_TIMEOUT_MS : 20000U);
    if (is("recovery_mode_critical")) assert(SYSTEM_MODE_Set(MODE_CRITICAL));
    if (is("recovery_mode_emergency")) assert(SYSTEM_MODE_Set(MODE_EMERGENCY));
    start(); all_pets_except(-1); expire(task);
    const uint32_t original_revision = timeout_revision(task);
    assert(original_revision != 0 && reports == 1 && recovery_resolves == 0);

    if (is("recovery_no_resume") || is("recovery_no_record")) {
        if (is("recovery_no_record")) {
            assert(FDIR_ResolveCriticalFault(SRC_CDH, (uint16_t)(FAULT_WDT_TIMEOUT_BASE + task), original_revision));
        }
        for (unsigned step = 0; step < 10; ++step) {
            now += pdMS_TO_TICKS(WATCHDOG_CHECK_MS);
            all_pets_except((int)task); assert(WATCHDOG_Check());
        }
        assert(reports == 1 && recovery_resolves == 0 && SYSTEM_MODE_Get() == MODE_SAFE);
        assert((timeout_revision(task) != 0) == is("recovery_no_resume"));
        return true;
    }
    if (is("recovery_pre_revision_pets")) {
        for (unsigned step = 0; step < 6; ++step) {
            now += pdMS_TO_TICKS(5000); all_pets_except(-1);
        }
        assert(recovery_queries == 0 && timeout_revision(task) == original_revision);
    }
    recovery_baseline();

    if (task_case || boundary || wrap || relapse) {
        healthy_steps(5);
        assert(timeout_revision(task) == original_revision && recovery_resolves == 0);
        if (boundary) {
            now += pdMS_TO_TICKS(4990); all_pets_except(-1); assert(WATCHDOG_Check());
            assert(timeout_revision(task) == original_revision && recovery_resolves == 0);
            now += pdMS_TO_TICKS(10); all_pets_except(-1); assert(WATCHDOG_Check());
        } else healthy_steps(1);
        assert(timeout_revision(task) == 0 && snapshot().count == 0 && recovery_resolves == 1);
        assert(SYSTEM_MODE_Get() == MODE_SAFE && !FDIR_HasActiveFaults());
        WATCHDOG_TaskTiming_t timing[WDT_TASK_COUNT];
        assert(WATCHDOG_GetTimingSnapshot(timing));
        assert(timing[task].pet_count >= 8 && timing[task].max_interval_ms > WATCHDOG_TIMEOUT_MS);
        if (wrap) assert(now <= pdMS_TO_TICKS(WATCHDOG_RECOVERY_WINDOW_MS));
        if (relapse) {
            expire(task);
            assert(reports == 2 && timeout_revision(task) > original_revision);
            assert(!FDIR_ResolveCriticalFault(SRC_CDH, (uint16_t)(FAULT_WDT_TIMEOUT_BASE + task), original_revision));
            recovery_baseline(); healthy_steps(6);
            assert(timeout_revision(task) == 0 && recovery_resolves == 2 && SYSTEM_MODE_Get() == MODE_SAFE);
        } else {
            const uint8_t nominal[] = {FDIR_CMD_RESET_MODE, MODE_NOMINAL};
            FDIR_ProcessCommand(nominal, sizeof(nominal));
            assert(SYSTEM_MODE_Get() == MODE_NOMINAL && strstr(event, "CMD_ACK") != NULL);
        }
    } else if (is("recovery_unstable_gap")) {
        healthy_steps(2);
        now += pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS) + 1;
        all_pets_except(-1); assert(WATCHDOG_Check());
        healthy_steps(4); assert(timeout_revision(task) == original_revision && recovery_resolves == 0);
        healthy_steps(2); assert(timeout_revision(task) == 0 && recovery_resolves == 1);
    } else if (is("recovery_gap_at_timeout")) {
        healthy_steps(3);
        now += pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS); all_pets_except(-1); assert(WATCHDOG_Check());
        assert(timeout_revision(task) == 0 && recovery_resolves == 1);
    } else if (is("recovery_stale") || is("recovery_fresh_at_boundary")) {
        healthy_steps(5);
        now += pdMS_TO_TICKS(WATCHDOG_RECOVERY_FRESH_MS) + (is("recovery_stale") ? 1U : 0U);
        all_pets_except((int)task); assert(WATCHDOG_Check());
        assert((timeout_revision(task) != 0) == is("recovery_stale"));
        if (is("recovery_stale")) { watchdog_pet((WatchdogTaskID_t)task); assert(WATCHDOG_Check()); }
        assert(timeout_revision(task) == 0 && recovery_resolves == 1);
    } else if (is("recovery_same_tick_burst")) {
        for (unsigned i = 0; i < 100; ++i) watchdog_pet((WatchdogTaskID_t)task);
        now += pdMS_TO_TICKS(WATCHDOG_RECOVERY_WINDOW_MS);
        all_pets_except((int)task); assert(WATCHDOG_Check());
        assert(recovery_resolves == 0 && timeout_revision(task) == original_revision);
        all_pets_except(-1); assert(WATCHDOG_Check());
        assert(recovery_resolves == 0); // Gap resets proof; old bursts do not count.
        healthy_steps(6); assert(timeout_revision(task) == 0);
    } else if (is("recovery_revision_change")) {
        healthy_steps(3);
        __real_FDIR_ReportFault((FaultReport_t){.source=SRC_CDH, .severity=FAULT_CRITICAL,
            .fault_code=(uint16_t)(FAULT_WDT_TIMEOUT_BASE + task)});
        assert(timeout_revision(task) > original_revision);
        assert(!FDIR_ResolveCriticalFault(SRC_CDH, (uint16_t)(FAULT_WDT_TIMEOUT_BASE + task), original_revision));
        recovery_baseline(); healthy_steps(5);
        assert(timeout_revision(task) != 0 && recovery_resolves == 0);
        healthy_steps(1); assert(timeout_revision(task) == 0 && recovery_resolves == 1);
    } else if (is("recovery_revision_query_failure") || is("recovery_query_lock_failure") ||
               is("recovery_resolve_lock_failure") || is("recovery_rereport_before_resolve") ||
               is("recovery_fatal_before_resolve") || is("recovery_rereport_after_resolve")) {
        healthy_steps(5);
        if (is("recovery_revision_query_failure")) fail_recovery_query = true;
        if (is("recovery_query_lock_failure")) fail_mode = 1;
        if (is("recovery_resolve_lock_failure")) recovery_race = RECOVERY_LOCK;
        if (is("recovery_rereport_before_resolve")) recovery_race = RECOVERY_REREPORT;
        if (is("recovery_fatal_before_resolve")) recovery_race = RECOVERY_FATAL;
        if (is("recovery_rereport_after_resolve")) recovery_race = RECOVERY_AFTER_CLEAR;
        healthy_steps(1);
        assert(timeout_revision(task) != 0 && FDIR_HasActiveFaults());
        if (is("recovery_rereport_after_resolve")) {
            const uint32_t new_revision = timeout_revision(task);
            healthy_steps(8);
            assert(recovery_resolves == 1 && timeout_revision(task) == new_revision);
        } else if (is("recovery_fatal_before_resolve")) {
            recovery_baseline(); healthy_steps(6);
            assert(timeout_revision(task) != 0 && snapshot().records[0].fatal && SYSTEM_MODE_Get() == MODE_EMERGENCY);
        } else {
            const unsigned attempted = recovery_resolves;
            recovery_baseline(); healthy_steps(5);
            assert(timeout_revision(task) != 0 && recovery_resolves == attempted);
            healthy_steps(1); assert(timeout_revision(task) == 0 && recovery_resolves == attempted + 1);
        }
    } else if (is("recovery_other_source") || is("recovery_other_fatal") || is("recovery_same_fatal") ||
               is("recovery_overflow") || is("recovery_uncertainty") || is("recovery_state_fault")) {
        if (is("recovery_other_source") || is("recovery_other_fatal")) {
            __real_FDIR_ReportFault((FaultReport_t){.source=SRC_ARCHIVE,
                .severity=is("recovery_other_fatal") ? FAULT_FATAL : FAULT_CRITICAL, .fault_code=0xA123});
        } else if (is("recovery_same_fatal")) {
            __real_FDIR_ReportFault((FaultReport_t){.source=SRC_CDH, .severity=FAULT_FATAL,
                .fault_code=(uint16_t)(FAULT_WDT_TIMEOUT_BASE + task)});
            recovery_baseline();
        } else if (is("recovery_overflow")) {
            for (unsigned i = 0; i < SYSTEM_FAULT_CAPACITY; ++i)
                __real_FDIR_ReportFault((FaultReport_t){.source=SRC_ARCHIVE, .severity=FAULT_CRITICAL,
                    .fault_code=(uint16_t)(0xA000 + i)});
            assert(snapshot().overflow);
        } else if (is("recovery_uncertainty")) {
            fail_mode = 1;
            __real_FDIR_ReportFault((FaultReport_t){.source=SRC_ARCHIVE, .severity=FAULT_CRITICAL, .fault_code=0xA123});
            assert(snapshot().recording_uncertain);
        } else {
            fail_watchdog = 1; watchdog_pet(WDT_TASK_TM_GEN);
            assert(snapshot().count == 2);
        }
        healthy_steps(6);
        if (is("recovery_same_fatal")) assert(timeout_revision(task) != 0 && snapshot().records[0].fatal);
        else assert(timeout_revision(task) == 0);
        assert(FDIR_HasActiveFaults() && !SYSTEM_MODE_Set(MODE_NOMINAL));
        assert(SYSTEM_MODE_Get() == ((is("recovery_other_fatal") || is("recovery_same_fatal")) ? MODE_EMERGENCY : MODE_SAFE));
        if (is("recovery_other_source") || is("recovery_other_fatal")) {
            assert(snapshot().count == 1 && snapshot().records[0].source == SRC_ARCHIVE);
        }
        if (is("recovery_state_fault")) assert(snapshot().count == 1 && snapshot().records[0].code == FAULT_WDT_STATE_UNAVAILABLE);
        if (is("recovery_overflow")) assert(snapshot().overflow && snapshot().count == SYSTEM_FAULT_CAPACITY - 1);
        if (is("recovery_uncertainty")) assert(snapshot().recording_uncertain && snapshot().count == 0);
    } else if (is("recovery_other_task")) {
        now += pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS) + 1;
        all_pets_except(WDT_TASK_FDIR); assert(WATCHDOG_Check());
        const uint32_t other_revision = timeout_revision(WDT_TASK_FDIR);
        assert(other_revision != 0 && snapshot().count == 2);
        for (unsigned i = 0; i < 6; ++i) {
            now += pdMS_TO_TICKS(5000); all_pets_except(WDT_TASK_FDIR); assert(WATCHDOG_Check());
        }
        assert(timeout_revision(task) == 0 && timeout_revision(WDT_TASK_FDIR) == other_revision);
        assert(snapshot().count == 1 && SYSTEM_MODE_Get() == MODE_SAFE);
    } else if (is("recovery_pre_revision_pets") || is("recovery_ack_no_acceleration") ||
               is("recovery_mode_critical") || is("recovery_mode_emergency") || is("recovery_predeploy")) {
        healthy_steps(5);
        const uint8_t ack[] = {FDIR_CMD_ACK_FAULT};
        const uint8_t nominal[] = {FDIR_CMD_RESET_MODE, MODE_NOMINAL};
        FDIR_ProcessCommand(ack, sizeof(ack)); FDIR_ProcessCommand(nominal, sizeof(nominal));
        assert(timeout_revision(task) != 0 && strstr(event, "CMD_NACK") != NULL);
        healthy_steps(1); assert(timeout_revision(task) == 0);
        assert(SYSTEM_MODE_Get() == (is("recovery_mode_critical") ? MODE_CRITICAL :
                                    (is("recovery_mode_emergency") ? MODE_EMERGENCY : MODE_SAFE)));
        if (is("recovery_predeploy")) {
            assert(!SYSTEM_MODE_Set(MODE_NOMINAL));
            assert(SYSTEM_MODE_CompleteDeployment() && SYSTEM_MODE_Get() == MODE_SAFE);
        }
        FDIR_ProcessCommand(nominal, sizeof(nominal));
        assert(SYSTEM_MODE_Get() == MODE_NOMINAL);
    } else assert(!"Unknown recovery case");

    assert(sdk_configs == 1 && sdk_subscriptions == 0 && sdk_feeds == 0);
    return true;
}

static bool matrix(void) {
    static const char *boundaries[] = {"below", "at", "above"};
    static const char *modes[] = {"boot", "safe", "nominal", "degraded", "emergency", "critical"};
    char candidate[64];
    for (unsigned task = 0; task < WDT_TASK_COUNT; ++task) {
        for (unsigned boundary = 0; boundary < 3; ++boundary) {
            snprintf(candidate, sizeof(candidate), "boundary_%u_%s", task, boundaries[boundary]);
            if (!is(candidate)) continue;
            start(); now = pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS) + boundary - 1;
            all_pets_except((int)task); assert(WATCHDOG_Check());
            assert(reports == (unsigned)(boundary == 2));
            if (boundary == 2) expect_task(task);
            else assert(snapshot().count == 0 && SYSTEM_MODE_Get() == MODE_NOMINAL);
            return true;
        }
        snprintf(candidate, sizeof(candidate), "wrap_%u", task);
        if (is(candidate)) {
            now = UINT32_MAX - pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS) / 2;
            start(); expire(task); assert(now < pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS)); return true;
        }
        snprintf(candidate, sizeof(candidate), "latch_%u", task);
        if (is(candidate)) {
            start(); expire(task); uint32_t revision = snapshot().records[0].revision;
            all_pets_except(-1); assert(WATCHDOG_Check()); expect_task(task);
            now += pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS) + 1;
            all_pets_except((int)task); assert(WATCHDOG_Check());
            assert(reports == 1 && snapshot().records[0].revision == revision);
            assert(!WATCHDOG_Arm() && !WATCHDOG_Init()); return true;
        }
    }
    for (unsigned mode = 0; mode < 6; ++mode) {
        snprintf(candidate, sizeof(candidate), "mode_%s", modes[mode]);
        if (!is(candidate)) continue;
        assert(SYSTEM_MODE_Set((SystemMode_t)mode)); start(); expire(0);
        SystemMode_t expected = mode == MODE_CRITICAL || mode == MODE_EMERGENCY ? (SystemMode_t)mode : MODE_SAFE;
        assert(SYSTEM_MODE_Get() == expected); return true;
    }
    return false;
}

static void special(void) {
    if (strncmp(scenario, "timing_", 7) == 0) {
        WATCHDOG_TaskTiming_t out[WDT_TASK_COUNT];
        memset(out, 0xA5, sizeof(out));
        WATCHDOG_TaskTiming_t unchanged[WDT_TASK_COUNT]; memcpy(unchanged, out, sizeof(out));
        if (is("timing_unarmed")) {
            assert(!WATCHDOG_GetTimingSnapshot(out));
            assert(memcmp(out, unchanged, sizeof(out)) == 0 && tick_reads == 0 && reports == 0); return;
        }
        start();
        if (is("timing_null")) { assert(!WATCHDOG_GetTimingSnapshot(NULL)); return; }
        if (is("timing_lock_failure")) {
            fail_watchdog = 1; assert(!WATCHDOG_GetTimingSnapshot(out));
            assert(memcmp(out, unchanged, sizeof(out)) == 0 && reports == 0 && sdk_feeds == 0); return;
        }
        if (is("timing_initial")) {
            now += pdMS_TO_TICKS(2000); assert(WATCHDOG_GetTimingSnapshot(out));
            for (unsigned i = 0; i < WDT_TASK_COUNT; ++i) assert(out[i].pet_count == 0 && out[i].age_ms == 2000);
            return;
        }
        now = is("timing_wrap") ? UINT32_MAX - pdMS_TO_TICKS(50) : pdMS_TO_TICKS(5000);
        watchdog_pet(WDT_TASK_ADCS); // First actual pet excludes startup wait.
        now += pdMS_TO_TICKS(100); watchdog_pet(WDT_TASK_ADCS);
        now += pdMS_TO_TICKS(200); watchdog_pet(WDT_TASK_ADCS);
        if (is("timing_zero")) watchdog_pet(WDT_TASK_ADCS);
        now += pdMS_TO_TICKS(50); assert(WATCHDOG_GetTimingSnapshot(out));
        assert(out[WDT_TASK_ADCS].pet_count == (is("timing_zero") ? 4U : 3U));
        assert(out[WDT_TASK_ADCS].min_interval_ms == (is("timing_zero") ? 0U : 100U));
        assert(out[WDT_TASK_ADCS].max_interval_ms == 200 && out[WDT_TASK_ADCS].age_ms == 50);
        assert(out[WDT_TASK_TM_GEN].pet_count == 0 && reports == 0 && sdk_feeds == 0);
        if (is("timing_repeat_arm")) {
            assert(!WATCHDOG_Arm()); WATCHDOG_TaskTiming_t again[WDT_TASK_COUNT];
            assert(WATCHDOG_GetTimingSnapshot(again) && memcmp(out, again, sizeof(out)) == 0);
        }
    } else if (is("uninitialized")) {
        assert(!WATCHDOG_Arm() && !WATCHDOG_Check()); watchdog_pet(WDT_TASK_TM_GEN);
        assert(reports == 0 && tick_reads == 0 && allocations == 2);
    } else if (is("unarmed")) {
        assert(WATCHDOG_Init()); now = pdMS_TO_TICKS(60000);
        watchdog_pet(WDT_TASK_TM_GEN); assert(!WATCHDOG_Check());
        assert(reports == 0 && tick_reads == 0);
    } else if (is("allocation_failure")) {
        fail_allocation = true; assert(!WATCHDOG_Init() && !WATCHDOG_Arm() && !WATCHDOG_Check());
        assert(allocations == 2 && reports == 0);
    } else if (is("arm_lock_failure")) {
        assert(WATCHDOG_Init()); fail_watchdog = 1;
        assert(!WATCHDOG_Arm() && !WATCHDOG_Check() && tick_reads == 0 && reports == 0);
        assert(WATCHDOG_Arm()); now = pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS); assert(WATCHDOG_Check() && reports == 0);
    } else if (is("repeat_init") || is("repeat_arm")) {
        start(); now = pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS) + 1;
        if (is("repeat_init")) assert(!WATCHDOG_Init()); else assert(!WATCHDOG_Arm());
        all_pets_except(0); assert(WATCHDOG_Check()); expect_task(0);
    } else if (is("invalid_ids")) {
        start(); unsigned before = tick_reads;
        watchdog_pet((WatchdogTaskID_t)-1); watchdog_pet(WDT_TASK_COUNT); watchdog_pet((WatchdogTaskID_t)999);
        assert(tick_reads == before && reports == 0); expire(0);
    } else if (is("startup_grace")) {
        assert(WATCHDOG_Init()); now = pdMS_TO_TICKS(60000); // Long optional startup, no tasks exist.
        assert(WATCHDOG_Arm()); now += pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS);
        assert(WATCHDOG_Check() && reports == 0);
        ++now; assert(WATCHDOG_Check() && reports == WDT_TASK_COUNT);
    } else if (is("future_pet_before_check")) {
        start(); now = pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS) + 1; all_pets_except(0);
        injected_task = WDT_TASK_TM_GEN; pet_before_take = true;
        assert(WATCHDOG_Check() && reports == 0 && snapshot().count == 0);
    } else if (is("pet_after_timeout_commit")) {
        start(); now = pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS) + 1; all_pets_except(0);
        injected_task = WDT_TASK_TM_GEN; pet_after_unlock = true;
        assert(WATCHDOG_Check()); expect_task(0); assert(reports == 1);
        assert(WATCHDOG_Check()); expect_task(0);
    } else if (is("mutex_check_failure") || is("mutex_pet_failure") || is("mutex_failure_once")) {
        start(); fail_watchdog = 1;
        if (is("mutex_pet_failure")) watchdog_pet(WDT_TASK_EPS_MANAGER); else assert(!WATCHDOG_Check());
        assert(snapshot().count == 1 && snapshot().records[0].code == FAULT_WDT_STATE_UNAVAILABLE);
        assert(reports == 1 && SYSTEM_MODE_Get() == MODE_SAFE && strstr(logs, "Task 0 timed out") == NULL);
        if (is("mutex_failure_once")) {
            fail_watchdog = 1; watchdog_pet(WDT_TASK_EPS_MANAGER);
            fail_watchdog = 1; assert(!WATCHDOG_Check()); assert(reports == 1);
        }
        assert(WATCHDOG_Check() && reports == 1);
        all_pets_except(-1); assert(FDIR_HasActiveFaults());
    } else if (is("mode_record_failure")) {
        start(); now = pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS) + 1; all_pets_except(0); fail_mode = 1;
        assert(WATCHDOG_Check()); FDIR_ActiveFaultSnapshot_t out = snapshot();
        assert(out.count == 0 && out.recording_uncertain && reports == 1 && SYSTEM_MODE_Get() == MODE_SAFE);
        assert(!SYSTEM_MODE_Set(MODE_NOMINAL)); all_pets_except(-1); assert(WATCHDOG_Check() && reports == 1);
    } else if (is("all_missing") || is("multiple_missing")) {
        start(); now = pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS) + 1;
        if (is("multiple_missing")) for (unsigned i = 0; i < WDT_TASK_COUNT; ++i)
            if (i != WDT_TASK_EPS_MANAGER && i != WDT_TASK_FDIR) watchdog_pet((WatchdogTaskID_t)i);
        assert(WATCHDOG_Check()); FDIR_ActiveFaultSnapshot_t out = snapshot();
        unsigned expected = is("all_missing") ? WDT_TASK_COUNT : 2;
        assert(out.count == expected && reports == expected);
        for (unsigned i = 0; i < out.count; ++i) assert(out.records[i].code == FAULT_WDT_TIMEOUT_BASE +
            (is("all_missing") ? i : (i == 0 ? WDT_TASK_EPS_MANAGER : WDT_TASK_FDIR)));
    } else if (is("ack_retains")) {
        start(); expire(0); const uint8_t ack[] = {FDIR_CMD_ACK_FAULT}; FDIR_ProcessCommand(ack, sizeof(ack));
        const uint8_t nominal[] = {FDIR_CMD_RESET_MODE, MODE_NOMINAL}; FDIR_ProcessCommand(nominal, sizeof(nominal));
        assert(strstr(event, "CMD_NACK: FDIR_SET_MODE") && SYSTEM_MODE_Get() == MODE_SAFE); expect_task(0);
    } else if (is("ledger_full")) {
        for (unsigned i = 0; i < SYSTEM_FAULT_CAPACITY; ++i)
            __real_FDIR_ReportFault((FaultReport_t){.source = SRC_CDH, .severity = FAULT_CRITICAL, .fault_code = (uint16_t)(0xD000 + i)});
        start(); now = pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS) + 1; all_pets_except(0); assert(WATCHDOG_Check());
        assert(snapshot().count == SYSTEM_FAULT_CAPACITY && snapshot().overflow && reports == 1);
    } else if (is("sdk_config_failure") || is("sdk_config_repeat_failure")) {
        fail_sdk_config = true;
        assert(!WATCHDOG_Init() && !WATCHDOG_Arm() && !WATCHDOG_Check());
        watchdog_pet(WDT_TASK_TM_GEN);
        assert(allocations == 3 && sdk_configs == 1 && sdk_subscriptions == 0 && reports == 0 && tick_reads == 0);
        if (is("sdk_config_repeat_failure")) {
            fail_sdk_config = false; assert(!WATCHDOG_Init() && !WATCHDOG_Arm());
            assert(sdk_configs == 1); // No retry/reset of a partially initialized service.
        }
    } else if (is("monitor_subscribe_failure") || is("monitor_feed_failure")) {
        start(); expect_abort = true;
        fail_sdk_subscribe = is("monitor_subscribe_failure"); fail_sdk_feed = !fail_sdk_subscribe;
        if (setjmp(task_exit) == 0) { vSoftwareWatchdogTask(NULL); assert(!"monitor returned"); }
        assert(aborts == 1 && delays == 0 && reports == 0);
        assert(sdk_subscriptions == 1 && sdk_feeds == (unsigned)fail_sdk_feed);
        assert((strstr(logs, "Software Watchdog initialized") != NULL) == fail_sdk_feed);
    } else if (is("monitor_unarmed") || is("monitor_healthy") || is("monitor_check_recovers") ||
               is("monitor_check_stalls") || is("monitor_report_stall")) {
        if (is("monitor_unarmed")) assert(WATCHDOG_Init()); else start();
        if (is("monitor_check_recovers")) fail_watchdog = 1;
        if (is("monitor_check_stalls")) fail_watchdog = 100;
        if (setjmp(task_exit) == 0) { vSoftwareWatchdogTask(NULL); assert(!"monitor returned"); }
        assert(sdk_subscriptions == 1 && aborts == 0);
        if (is("monitor_report_stall")) assert(sdk_feeds == 4 && reports == 0);
    } else if (is("monitor_period") || is("monitor_no_reset")) {
        start();
        if (is("monitor_no_reset")) {
            now = pdMS_TO_TICKS(1000); watchdog_pet(WDT_TASK_TM_GEN);
            now = pdMS_TO_TICKS(5000);
        }
        if (setjmp(task_exit) == 0) { vSoftwareWatchdogTask(NULL); assert(!"monitor returned"); }
        assert(now == pdMS_TO_TICKS(20000) && strstr(logs, "Software Watchdog initialized") != NULL);
    } else assert(!"Unknown watchdog case");
}

int main(int argc, char **argv) {
    assert(argc == 2); scenario = argv[1];
    assert(WDT_TASK_EPS_MANAGER == 5 && WDT_TASK_FDIR == 6 && WDT_TASK_COUNT == 7);
    assert(SYSTEM_MODE_Init(MODE_INITIAL_BOOT) && FDIR_Init());
    if (!is("recovery_predeploy")) assert(SYSTEM_MODE_CompleteDeployment());
    if (!matrix() && !recovery_cases()) special();
    assert(!mode_mutex.held && !fault_mutex.held && !watchdog_mutex.held);
    fprintf(stdout, "PASS: software watchdog case %s (%u Hz); actual heartbeat/FDIR/MODE, mocked RTOS/SDK, no target action.\n",
            scenario, (unsigned)TEST_WDT_HZ);
    return 0;
}

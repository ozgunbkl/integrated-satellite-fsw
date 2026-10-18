/* Actual mode/FDIR/EPS/archive, deterministic mutex/time/network boundaries. */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "fdir_service.h"
#include "system_mode.h"
#include "eps_telemetry.h"
#include "eps_commands.h"
#include "eps_service.h"
#include "archive_service.h"
#include "archive_port.h"
#include "commands.h"
#include "time_service.h"
#include "yamcs_interface.h"
#include "watchdog.h"
#include "freertos/task.h"

void vEPS_Manager_Task(void *argument);
bool __real_SYSTEM_MODE_WithCurrent(SystemModeAction_t action, void *context);
bool __real_FDIR_GetFaultRevision(FaultSource_t source, uint16_t code, uint32_t *revision);
bool __real_FDIR_ResolveCriticalFault(FaultSource_t source, uint16_t code, uint32_t revision);

typedef struct { bool held; } Mutex;
static Mutex mode_mutex, fault_mutex, eps_mutex, archive_mutex;
bool Archive_PortCreateBootId(uint64_t *id) { *id = UINT64_C(0x1020304050607080); return true; }
static unsigned allocations, fail_mode, fail_eps, callbacks, delays, resolves, pets;
static bool in_manager, in_guard, in_resolve, revision_failure, resolve_failure;
static bool report_before_resolve, report_after_resolve;
static bool in_try_api;
static uint32_t now_seconds = 42;
static jmp_buf task_exit;
static char logs[65536], event[256];
static size_t log_length;
static const char *scenario;
EPS_Telemetry_t g_eps_telemetry;
SemaphoreHandle_t xEPSDataMutex = &eps_mutex;

static bool is(const char *name) { return strcmp(name, scenario) == 0; }
static void report(FaultSource_t source, uint16_t code, bool fatal) {
    FDIR_ReportFault((FaultReport_t){.source = source, .severity = fatal ? FAULT_FATAL : FAULT_CRITICAL,
        .fault_code = code, .timestamp = 999}); // Service supplies the actual timestamp.
}
static void eps_fault(bool fatal) { report(SRC_EPS, FAULT_EPS_LOW_VOLTAGE, fatal); }
static void ack(void) {
    const uint8_t command[] = {FDIR_CMD_ACK_FAULT}; FDIR_ProcessCommand(command, sizeof(command));
}
static FDIR_ActiveFaultSnapshot_t snapshot(void) {
    FDIR_ActiveFaultSnapshot_t out;
    assert(FDIR_GetActiveFaults(&out));
    assert(out.count <= SYSTEM_FAULT_CAPACITY);
    return out;
}
static uint32_t revision(FaultSource_t source, uint16_t code) {
    uint32_t value = UINT32_MAX;
    assert(FDIR_GetFaultRevision(source, code, &value));
    return value;
}
static void ground(SystemMode_t target, bool accepted) {
    const uint8_t command[] = {FDIR_CMD_RESET_MODE, (uint8_t)target};
    event[0] = '\0'; FDIR_ProcessCommand(command, sizeof(command));
    assert(strstr(event, accepted ? "CMD_ACK: FDIR_SET_MODE=" : "CMD_NACK: FDIR_SET_MODE") != NULL);
}
int __wrap_printf(const char *format, ...) {
    assert(!mode_mutex.held && !fault_mutex.held && !eps_mutex.held && !archive_mutex.held);
    va_list args; va_start(args, format);
    int count = vsnprintf(logs + log_length, sizeof(logs) - log_length, format, args);
    va_end(args);
    assert(count >= 0 && (size_t)count < sizeof(logs) - log_length);
    log_length += (size_t)count;
    return count;
}
bool YAMCS_SendEvent(const char *format, ...) {
    assert(!mode_mutex.held && !fault_mutex.held && !eps_mutex.held);
    va_list args; va_start(args, format); vsnprintf(event, sizeof(event), format, args); va_end(args);
    return true;
}
bool YAMCS_SendTelemetry(uint16_t apid, void *data, uint16_t length) {
    (void)apid; (void)data; (void)length;
    assert(!mode_mutex.held && !fault_mutex.held && !eps_mutex.held);
    return true;
}
uint32_t TIME_GetSeconds(void) {
    assert(!mode_mutex.held && !fault_mutex.held && !eps_mutex.held);
    return now_seconds;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    assert(allocations < 3);
    Mutex *slots[] = {&mode_mutex, &fault_mutex, &archive_mutex};
    return slots[allocations++];
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t timeout) {
    assert(handle == &mode_mutex || handle == &fault_mutex || handle == &eps_mutex || handle == &archive_mutex);
    if (archive_mutex.held) assert(handle == &mode_mutex && timeout == 0);
    Mutex *mutex = handle; assert(!mutex->held);
    assert(timeout == portMAX_DELAY || timeout == pdMS_TO_TICKS(10) ||
           (handle == &mode_mutex && timeout == 0));
    if (in_try_api) assert(handle == &mode_mutex && timeout == 0);
    if (handle == &eps_mutex) {
        assert(!mode_mutex.held && !fault_mutex.held);
        if (fail_eps) { --fail_eps; return pdFALSE; }
    } else if (handle == &mode_mutex) {
        assert(!eps_mutex.held || in_guard || in_resolve);
        if (fail_mode) { --fail_mode; return pdFALSE; }
    } else assert(!mode_mutex.held && !eps_mutex.held);
    mutex->held = true; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
    Mutex *mutex = handle; assert(mutex->held); mutex->held = false; return pdTRUE;
}
bool __wrap_SYSTEM_MODE_WithCurrent(SystemModeAction_t action, void *context) {
    in_guard = true; bool result = __real_SYSTEM_MODE_WithCurrent(action, context); in_guard = false;
    return result;
}
bool __wrap_FDIR_GetFaultRevision(FaultSource_t source, uint16_t code, uint32_t *value) {
    if (in_manager && revision_failure) { revision_failure = false; fail_mode = 1; }
    return __real_FDIR_GetFaultRevision(source, code, value);
}
bool __wrap_FDIR_ResolveCriticalFault(FaultSource_t source, uint16_t code, uint32_t value) {
    if (in_manager) {
        assert(eps_mutex.held && source == SRC_EPS && code == FAULT_EPS_LOW_VOLTAGE);
        assert(isfinite(g_eps_telemetry.f_BusVoltage) && g_eps_telemetry.f_BusVoltage >= EPS_VOLTAGE_RECOVERY);
    }
    in_resolve = true;
    if (report_before_resolve) {
        report_before_resolve = false;
        assert(SYSTEM_MODE_RecordFault(SRC_EPS, FAULT_EPS_LOW_VOLTAGE, false, 43));
    }
    if (resolve_failure) { resolve_failure = false; fail_mode = 1; }
    bool result = __real_FDIR_ResolveCriticalFault(source, code, value);
    if (result) ++resolves;
    if (report_after_resolve) {
        report_after_resolve = false;
        assert(SYSTEM_MODE_RecordFault(SRC_EPS, FAULT_EPS_LOW_VOLTAGE, false, 44));
    }
    in_resolve = false;
    return result;
}
void vTaskDelay(TickType_t ticks) {
    assert(ticks == pdMS_TO_TICKS(100) && !mode_mutex.held && !fault_mutex.held && !eps_mutex.held);
    ++delays; assert(pets == delays); longjmp(task_exit, 1);
}
void watchdog_pet(WatchdogTaskID_t task) {
    assert(task == WDT_TASK_EPS_MANAGER && in_manager && !eps_mutex.held && !mode_mutex.held && !fault_mutex.held);
    ++pets;
}
static void cycle(void) {
    in_manager = true;
    if (setjmp(task_exit) == 0) { vEPS_Manager_Task(NULL); assert(!"EPS task returned"); }
    in_manager = false;
}
static void permission(SystemMode_t mode, void *argument) { (void)mode; (void)argument; ++callbacks; }
static void observe(SystemMode_t mode, void *argument) { *(SystemMode_t *)argument = mode; }
static bool allowed(bool fatal, SystemMode_t target) {
    return fatal ? target == MODE_EMERGENCY :
        (target == MODE_SAFE || target == MODE_CRITICAL || target == MODE_EMERGENCY);
}

static bool matrix(void) {
    static const char *modes[] = {"boot", "safe", "nominal", "degraded", "emergency", "critical"};
    static const char *apis[] = {"set", "transition", "quiet", "ground"};
    static const char *sources[] = {"payload", "eps", "comms", "archive", "cdh", "adcs"};
    char candidate[100];
    for (unsigned fatal = 0; fatal < 2; ++fatal) {
        for (unsigned api = 0; api < 4; ++api) {
            for (unsigned target = 0; target < 6; ++target) {
                snprintf(candidate, sizeof(candidate), "gate_%s_%s_%s", fatal ? "fatal" : "critical", apis[api], modes[target]);
                if (!is(candidate)) continue;
                report(SRC_CDH, 0xC201, fatal);
                SystemMode_t previous = SYSTEM_MODE_Get(); bool accepted = allowed(fatal, (SystemMode_t)target);
                if (api == 0) assert(SYSTEM_MODE_Set((SystemMode_t)target) == accepted);
                else if (api == 1) assert(SYSTEM_MODE_Transition(previous, (SystemMode_t)target) == accepted);
                else if (api == 2) assert(SYSTEM_MODE_TransitionQuiet(previous, (SystemMode_t)target) == accepted);
                else ground((SystemMode_t)target, accepted);
                assert(SYSTEM_MODE_Get() == (accepted ? (SystemMode_t)target : previous));
                ack(); assert(FDIR_HasActiveFaults() && snapshot().count == 1);
                return true;
            }
        }
        for (unsigned source = 0; source < SRC_COUNT; ++source) {
            snprintf(candidate, sizeof(candidate), "source_%s_%s", sources[source], fatal ? "fatal" : "critical");
            if (!is(candidate)) continue;
            report((FaultSource_t)source, 0x1234, fatal);
            FDIR_ActiveFaultSnapshot_t out = snapshot();
            assert(out.count == 1 && out.records[0].source == source && out.records[0].fatal == (bool)fatal);
            assert(SYSTEM_MODE_Get() == (fatal ? MODE_EMERGENCY : MODE_SAFE));
            return true;
        }
        for (unsigned op = 0; op <= EPS_OP_PAYLOAD; ++op) {
            snprintf(candidate, sizeof(candidate), "operation_%s_%u", fatal ? "fatal" : "critical", op);
            if (!is(candidate)) continue;
            report(SRC_CDH, 0xC201, fatal);
            assert(!EPS_RunIfPermitted((EpsOperation_t)op, permission, NULL) && callbacks == 0);
            return true;
        }
    }
    static const char *health_names[] = {"below", "at", "above", "six", "low", "zero", "negative", "nan", "inf", "neg_inf"};
    static const float health[] = {8.49f, 8.5f, 8.51f, 6.01f, 2.49f, 0, -1, NAN, INFINITY, -INFINITY};
    for (unsigned v = 0; v < sizeof(health) / sizeof(health[0]); ++v) {
        snprintf(candidate, sizeof(candidate), "health_%s", health_names[v]);
        if (!is(candidate)) continue;
        eps_fault(false); uint32_t old = revision(SRC_EPS, FAULT_EPS_LOW_VOLTAGE);
        g_eps_telemetry.f_BusVoltage = health[v]; cycle();
        bool healthy = isfinite(health[v]) && health[v] >= EPS_VOLTAGE_RECOVERY;
        assert(FDIR_HasActiveFaults() == !healthy && resolves == (unsigned)healthy);
        assert((revision(SRC_EPS, FAULT_EPS_LOW_VOLTAGE) == 0) == healthy && old != 0);
        assert(SYSTEM_MODE_Get() == MODE_SAFE && EPS_GetMode() == EPS_MODE_SAFE);
        assert(!(g_eps_telemetry.ul_PDU_Register & (PDU_LOAD_PAYLOAD_BIT | PDU_LOAD_HEATER_BIT)));
        ground(MODE_NOMINAL, healthy);
        return true;
    }
    return false;
}

static bool try_query(FaultSource_t source, uint16_t code, uint32_t *value) {
    in_try_api = true;
    const bool result = FDIR_TryGetFaultRevision(source, code, value);
    in_try_api = false;
    return result;
}
static bool try_resolve(FaultSource_t source, uint16_t code, uint32_t value) {
    in_try_api = true;
    const bool result = FDIR_TryResolveCriticalFault(source, code, value);
    in_try_api = false;
    return result;
}
static bool try_api_cases(void) {
    if (strncmp(scenario, "try_", 4) != 0) return false;
    uint32_t value = 99;
    if (is("try_uninitialized")) {
        assert(!try_query(SRC_CDH, 0xC201, &value) && value == 99);
        assert(!try_resolve(SRC_CDH, 0xC201, 1));
        assert(SYSTEM_MODE_Init(MODE_INITIAL_BOOT) && FDIR_Init());
        assert(snapshot().count == 0 && !snapshot().recording_uncertain);
        return true;
    }
    report(SRC_CDH, 0xC201, false);
    const uint32_t old = revision(SRC_CDH, 0xC201);
    if (is("try_query")) {
        assert(try_query(SRC_CDH, 0xC201, &value) && value == old);
        assert(try_query(SRC_CDH, 0xC202, &value) && value == 0);
    } else if (is("try_query_busy")) {
        fail_mode = 1;
        assert(!try_query(SRC_CDH, 0xC201, &value) && value == 99);
        assert(snapshot().count == 1 && !snapshot().recording_uncertain);
    } else if (is("try_query_invalid")) {
        assert(!try_query((FaultSource_t)-1, 0xC201, &value) && value == 99);
        assert(!try_query(SRC_COUNT, 0xC201, &value) && value == 99);
        assert(!try_query(SRC_CDH, 0xC201, NULL));
    } else if (is("try_resolve_invalid")) {
        assert(!try_resolve(SRC_CDH, 0xC201, 0));
        assert(!try_resolve((FaultSource_t)-1, 0xC201, old));
        assert(!try_resolve(SRC_COUNT, 0xC201, old));
        assert(!try_resolve(SRC_EPS, 0xC201, old));
        assert(!try_resolve(SRC_CDH, 0xC202, old));
        assert(snapshot().count == 1);
    } else if (is("try_resolve_fatal")) {
        report(SRC_CDH, 0xC201, true);
        assert(!try_resolve(SRC_CDH, 0xC201, revision(SRC_CDH, 0xC201)));
        assert(snapshot().records[0].fatal && SYSTEM_MODE_Get() == MODE_EMERGENCY);
    } else {
        if (is("try_resolve_busy")) {
            fail_mode = 1; assert(!try_resolve(SRC_CDH, 0xC201, old));
            assert(snapshot().count == 1 && !snapshot().recording_uncertain);
        } else if (is("try_resolve_stale")) {
            report(SRC_CDH, 0xC201, false);
            assert(!try_resolve(SRC_CDH, 0xC201, old));
        } else if (is("try_preserves_uncertainty")) {
            fail_mode = 1; report(SRC_ARCHIVE, 0xA123, false);
            assert(snapshot().recording_uncertain);
        } else assert(is("try_resolve"));
        report(SRC_ARCHIVE, 0xA124, false);
        assert(try_resolve(SRC_CDH, 0xC201, revision(SRC_CDH, 0xC201)));
        assert(snapshot().count == 1 && snapshot().records[0].source == SRC_ARCHIVE);
        assert(SYSTEM_MODE_Get() == MODE_SAFE && !SYSTEM_MODE_Set(MODE_NOMINAL));
        if (is("try_preserves_uncertainty")) assert(snapshot().recording_uncertain);
    }
    assert(!in_try_api);
    return true;
}

static void special(void) {
    if (is("same_source_codes") || is("same_code_sources")) {
        for (unsigned i = 0; i < SRC_COUNT; ++i) report(is("same_source_codes") ? SRC_EPS : (FaultSource_t)i,
            is("same_source_codes") ? (uint16_t)(0x1000 + i) : 0x1000, false);
        FDIR_ActiveFaultSnapshot_t out = snapshot(); assert(out.count == SRC_COUNT);
        for (unsigned i = 0; i < SRC_COUNT; ++i) {
            assert(out.records[i].source == (is("same_source_codes") ? SRC_EPS : i));
            assert(out.records[i].code == (is("same_source_codes") ? 0x1000 + i : 0x1000));
        }
    } else if (is("repeat_reports") || is("severity_promotion")) {
        report(SRC_CDH, 0xC201, false); uint32_t first = revision(SRC_CDH, 0xC201);
        now_seconds = 43; report(SRC_CDH, 0xC201, is("severity_promotion"));
        now_seconds = 44; report(SRC_CDH, 0xC201, false);
        FDIR_ActiveFaultSnapshot_t out = snapshot();
        assert(out.count == 1 && out.records[0].reports == 3 && out.records[0].revision > first);
        assert(out.records[0].first_timestamp == 42 && out.records[0].last_timestamp == 44);
        assert(out.records[0].fatal == is("severity_promotion"));
    } else if (is("ack_retains")) {
        report(SRC_EPS, 0xE102, false); report(SRC_ARCHIVE, FAULT_ARC_CRC_FAIL, false);
        FDIR_ReportFault((FaultReport_t){.source = SRC_COMMS, .severity = FAULT_WARNING});
        ack(); FDIR_Process();
        assert(snapshot().count == 2 && FDIR_HasActiveFaults() && SYSTEM_MODE_Get() == MODE_SAFE);
    } else if (is("snapshot_null") || is("snapshot_lock")) {
        report(SRC_CDH, 0xC201, false);
        assert(!FDIR_GetActiveFaults(NULL));
        FDIR_ActiveFaultSnapshot_t out; memset(&out, 0xA5, sizeof(out));
        fail_mode = 1; assert(!FDIR_GetActiveFaults(&out) && out.count == 0xA5A5);
        assert(snapshot().count == 1);
    } else if (is("revision_query") || is("query_unknown")) {
        assert(revision(SRC_CDH, 0xC201) == 0);
        report(SRC_CDH, 0xC201, false); assert(revision(SRC_CDH, 0xC201) != 0);
        uint32_t value = 99; fail_mode = 1;
        assert(!FDIR_GetFaultRevision(SRC_CDH, 0xC201, &value) && value == 99);
        assert(!FDIR_GetFaultRevision((FaultSource_t)-1, 0, &value));
        assert(!FDIR_GetFaultRevision(SRC_COUNT, 0, &value));
        assert(!FDIR_GetFaultRevision(SRC_CDH, 0, NULL));
    } else if (is("resolve_wrong_revision") || is("resolve_rereport") || is("resolve_reuse") || is("resolve_lock")) {
        report(SRC_CDH, 0xC201, false); uint32_t old = revision(SRC_CDH, 0xC201);
        assert(!FDIR_ResolveCriticalFault(SRC_CDH, 0xC201, 0));
        assert(!FDIR_ResolveCriticalFault(SRC_EPS, 0xC201, old));
        assert(!FDIR_ResolveCriticalFault(SRC_CDH, 0xC202, old));
        assert(!FDIR_ResolveCriticalFault(SRC_CDH, 0xC201, old + 1));
        if (is("resolve_rereport")) report(SRC_CDH, 0xC201, false);
        if (is("resolve_reuse")) {
            assert(FDIR_ResolveCriticalFault(SRC_CDH, 0xC201, old)); report(SRC_CDH, 0xC201, false);
        }
        if (is("resolve_lock")) fail_mode = 1;
        if (!is("resolve_wrong_revision")) assert(!FDIR_ResolveCriticalFault(SRC_CDH, 0xC201, old));
        assert(FDIR_HasActiveFaults());
        assert(FDIR_ResolveCriticalFault(SRC_CDH, 0xC201, revision(SRC_CDH, 0xC201)));
        assert(!FDIR_HasActiveFaults() && SYSTEM_MODE_Get() == MODE_SAFE);
        ground(MODE_NOMINAL, true);
    } else if (is("fatal_not_resolved")) {
        eps_fault(true); assert(!FDIR_ResolveCriticalFault(SRC_EPS, FAULT_EPS_LOW_VOLTAGE,
            revision(SRC_EPS, FAULT_EPS_LOW_VOLTAGE)));
        ack(); assert(FDIR_HasActiveFaults() && SYSTEM_MODE_Get() == MODE_EMERGENCY);
    } else if (strncmp(scenario, "overflow_", 9) == 0 || is("full_duplicate")) {
        for (unsigned i = 0; i < SYSTEM_FAULT_CAPACITY; ++i) report(SRC_CDH, (uint16_t)(0xC200 + i), false);
        FDIR_ActiveFaultSnapshot_t before = snapshot();
        assert(before.count == SYSTEM_FAULT_CAPACITY && !before.overflow);
        if (is("full_duplicate")) {
            report(SRC_CDH, 0xC200, false); assert(snapshot().records[0].reports == 2 && !snapshot().overflow);
        } else {
            bool fatal = is("overflow_fatal"); report(SRC_CDH, 0xC300, fatal);
            FDIR_ActiveFaultSnapshot_t after = snapshot();
            assert(after.count == SYSTEM_FAULT_CAPACITY && after.overflow);
            assert(memcmp(before.records, after.records, sizeof(before.records)) == 0);
            if (is("overflow_after_resolve")) {
                for (unsigned i = 0; i < before.count; ++i)
                    assert(FDIR_ResolveCriticalFault(SRC_CDH, before.records[i].code, before.records[i].revision));
                assert(snapshot().count == 0 && snapshot().overflow);
            }
            ack(); ground(MODE_NOMINAL, false);
            assert(FDIR_HasActiveFaults() && SYSTEM_MODE_Get() == (fatal ? MODE_EMERGENCY : MODE_SAFE));
        }
    } else if (strncmp(scenario, "record_lock_", 12) == 0 || strncmp(scenario, "uninitialized_", 14) == 0) {
        bool fatal = strstr(scenario, "fatal") != NULL;
        fail_mode = 1; report(SRC_CDH, 0xC201, fatal); fail_mode = 0;
        if (strncmp(scenario, "uninitialized_", 14) == 0) {
            assert(SYSTEM_MODE_Init(MODE_INITIAL_BOOT)); assert(FDIR_Init());
        }
        FDIR_ActiveFaultSnapshot_t out = snapshot();
        assert(out.count == 0 && out.recording_uncertain);
        assert(SYSTEM_MODE_CompleteDeployment()); ack();
        assert(!SYSTEM_MODE_Set(MODE_INITIAL_BOOT) && !SYSTEM_MODE_Set(MODE_NOMINAL));
        assert(!SYSTEM_MODE_TransitionQuiet(fatal ? MODE_EMERGENCY : MODE_SAFE, MODE_DEGRADED));
        SystemMode_t effective = MODE_NOMINAL;
        assert(SYSTEM_MODE_WithCurrent(observe, &effective));
        assert(effective == (fatal ? MODE_EMERGENCY : MODE_SAFE) && FDIR_HasActiveFaults());
        assert(!EPS_RunIfPermitted(EPS_OP_DETUMBLE, permission, NULL));
    } else if (is("repeat_init")) {
        eps_fault(false); assert(!FDIR_Init() && !SYSTEM_MODE_Init(MODE_INITIAL_BOOT));
        assert(snapshot().count == 1 && FDIR_HasActiveFaults());
    } else if (is("quiet_no_log")) {
        size_t before = log_length;
        assert(SYSTEM_MODE_RecordFault(SRC_CDH, 0xC201, false, 42));
        uint32_t rev = revision(SRC_CDH, 0xC201);
        assert(FDIR_ResolveCriticalFault(SRC_CDH, 0xC201, rev));
        assert(log_length == before);
    } else if (strncmp(scenario, "eps_", 4) == 0) {
        eps_fault(is("eps_fatal_same_code"));
        uint32_t old = revision(SRC_EPS, FAULT_EPS_LOW_VOLTAGE);
        if (is("eps_other_fault_retained")) report(SRC_ARCHIVE, FAULT_ARC_CRC_FAIL, false);
        if (is("eps_revision_failure")) revision_failure = true;
        if (is("eps_clear_failure")) resolve_failure = true;
        if (is("eps_rereport_before_clear")) report_before_resolve = true;
        if (is("eps_report_after_clear")) report_after_resolve = true;
        if (is("eps_mutex_failure")) fail_eps = 1;
        cycle();
        if (is("eps_explicit_recovery") || is("eps_relapse")) {
            assert(!FDIR_HasActiveFaults() && SYSTEM_MODE_Get() == MODE_SAFE);
            ground(MODE_NOMINAL, true); cycle(); assert(EPS_GetMode() == EPS_MODE_RECOVERY);
            cycle(); assert(EPS_GetMode() == EPS_MODE_NOMINAL);
            if (is("eps_relapse")) {
                g_eps_telemetry.f_BusVoltage = 4.99f; cycle();
                assert(FDIR_HasActiveFaults() && SYSTEM_MODE_Get() == MODE_SAFE);
                assert(revision(SRC_EPS, FAULT_EPS_LOW_VOLTAGE) > old); ground(MODE_NOMINAL, false);
            }
        } else if (is("eps_other_fault_retained")) {
            assert(revision(SRC_EPS, FAULT_EPS_LOW_VOLTAGE) == 0);
            assert(snapshot().count == 1 && snapshot().records[0].source == SRC_ARCHIVE);
            assert(FDIR_HasActiveFaults()); ground(MODE_NOMINAL, false);
        } else if (is("eps_fatal_same_code")) {
            assert(snapshot().records[0].fatal && FDIR_HasActiveFaults() && resolves == 0);
            ground(MODE_SAFE, false); ground(MODE_NOMINAL, false);
        } else {
            assert(FDIR_HasActiveFaults() && SYSTEM_MODE_Get() == MODE_SAFE);
            if (is("eps_report_after_clear")) assert(resolves == 1 && revision(SRC_EPS, FAULT_EPS_LOW_VOLTAGE) > old);
            else assert(resolves == 0);
            cycle(); assert(!FDIR_HasActiveFaults() && SYSTEM_MODE_Get() == MODE_SAFE);
        }
    } else if (is("real_archive_crc")) {
        assert(Archive_Init()); uint8_t data[] = {1, 2, 3, 4}; ArchiveRecord_t out;
        assert(Archive_WriteRecord(REC_ID_HK, data, sizeof(data)) == ARCHIVE_OK);
        assert(Archive_WriteRecord(REC_ID_HK, data, sizeof(data)) == ARCHIVE_OK);
        uint16_t used = Archive_GetUsage();
        Archive_CorruptMemoryForTest(4); // Only this host process's local RAM archive.
        assert(Archive_ReadRecord(0, &out) == ARCHIVE_ERR_CRC);
        assert(snapshot().count == 1 && snapshot().records[0].code == FAULT_ARC_CRC_FAIL);
        assert(Archive_ReadRecord(1, &out) == ARCHIVE_OK); ack();
        assert(Archive_GetUsage() == used && FDIR_HasActiveFaults()); ground(MODE_NOMINAL, false);
        assert(Archive_ReadRecord(0, &out) == ARCHIVE_ERR_CRC && snapshot().records[0].reports == 2);
        // Complete the existing CRC maintenance check; no new target profile.
        // Reads/ACK/bad WIPE cannot heal corruption or discard these records.
        const uint8_t bad_key[] = {ARCHIVE_CMD_WIPE_ALL, 0xAB};
        const uint8_t short_wipe[] = {ARCHIVE_CMD_WIPE_ALL};
        ARCHIVE_ProcessCommand(bad_key, sizeof(bad_key));
        ARCHIVE_ProcessCommand(short_wipe, sizeof(short_wipe));
        assert(Archive_GetUsage() == used && snapshot().count == 1);
        report(SRC_CDH, 0xC201, false);
        const uint8_t wipe[] = {ARCHIVE_CMD_WIPE_ALL, 0xAA};
        ARCHIVE_ProcessCommand(wipe, sizeof(wipe));
        assert(Archive_GetUsage() == 0 && revision(SRC_ARCHIVE, FAULT_ARC_CRC_FAIL) == 0);
        assert(snapshot().count == 1 && snapshot().records[0].source == SRC_CDH);
        assert(SYSTEM_MODE_Get() == MODE_SAFE); ground(MODE_NOMINAL, false);
        assert(strstr(logs, "CRC maintenance COMPLETE"));
        // Fatal CRC is not downgraded by maintenance, even on an empty archive.
        report(SRC_ARCHIVE, FAULT_ARC_CRC_FAIL, true);
        ARCHIVE_ProcessCommand(wipe, sizeof(wipe));
        assert(snapshot().count == 2 && revision(SRC_ARCHIVE, FAULT_ARC_CRC_FAIL) != 0);
        assert(SYSTEM_MODE_Get() == MODE_EMERGENCY);
        assert(strstr(logs, "CRC maintenance DEFERRED"));
    } else if (strncmp(scenario, "predeploy_", 10) == 0) {
        bool fatal = strstr(scenario, "fatal") != NULL;
        if (strstr(scenario, "failure")) fail_mode = 1;
        eps_fault(fatal); assert(!SYSTEM_MODE_IsDeploymentComplete());
        assert(SYSTEM_MODE_CompleteDeployment() && SYSTEM_MODE_IsDeploymentComplete());
        assert(SYSTEM_MODE_Get() == (fatal ? MODE_EMERGENCY : MODE_SAFE));
        ground(MODE_NOMINAL, false); ground(MODE_DEGRADED, false);
    } else if (is("before_commit_nominal")) {
        assert(SYSTEM_MODE_Set(MODE_SAFE)); ground(MODE_NOMINAL, true);
        report(SRC_CDH, 0xC201, false); ground(MODE_NOMINAL, false);
        assert(SYSTEM_MODE_Get() == MODE_SAFE);
    } else { assert(!"Unknown fault-ledger test"); }
}

int main(int argc, char **argv) {
    assert(argc == 2); scenario = argv[1];
    if (strncmp(scenario, "uninitialized_", 14) != 0 && !is("try_uninitialized")) {
        assert(SYSTEM_MODE_Init(MODE_INITIAL_BOOT)); assert(FDIR_Init());
        if (strncmp(scenario, "predeploy_", 10) != 0) assert(SYSTEM_MODE_CompleteDeployment());
    }
    g_eps_telemetry.f_BusVoltage = 8.5f;
    g_eps_telemetry.ul_PDU_Register = PDU_LOAD_PAYLOAD_BIT | PDU_LOAD_HEATER_BIT | PDU_LOAD_COMMS_BIT;
    if (!matrix() && !try_api_cases()) special();
    assert(!mode_mutex.held && !fault_mutex.held && !eps_mutex.held && !in_manager && !in_guard && !in_resolve);
    fprintf(stdout, "PASS: active FDIR fault case %s; actual ledger/gate/EPS/archive and no target I/O.\n", scenario);
    return 0;
}

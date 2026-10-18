/* Real EPS manager/commands, system mode and FDIR; no hardware or network I/O. */
#include <assert.h>
#include <float.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "eps_telemetry.h"
#include "eps_commands.h"
#include "commands.h"
#include "system_mode.h"
#include "fdir_service.h"
#include "time_service.h"
#include "yamcs_interface.h"
#include "watchdog.h"
#include "freertos/task.h"

void vEPS_Manager_Task(void *parameters);
float EPS_CalculateSoC(float voltage);
void __real_EPS_Monitor(float voltage);
void __real_FDIR_ReportFault(FaultReport_t report);
bool __real_SYSTEM_MODE_WithCurrent(SystemModeAction_t action, void *context);
bool __real_FDIR_ResolveCriticalFault(FaultSource_t source, uint16_t code, uint32_t revision);

#define LOADS (PDU_LOAD_PAYLOAD_BIT | PDU_LOAD_HEATER_BIT)
#define OTHER_BITS (UINT32_C(0xA0000000) | PDU_LOAD_COMMS_BIT)
typedef struct { bool held; } MockMutex;
static MockMutex mode_mutex, fault_mutex, eps_mutex;
EPS_Telemetry_t g_eps_telemetry;
SemaphoreHandle_t xEPSDataMutex = &eps_mutex;
static bool allocating_mode, in_manager, guard_active, external_writer, resolving;
static unsigned fail_guard, fail_manager_lock, fail_getter_lock;
static unsigned reports, monitor_calls, guard_calls, callback_calls, delays, pets;
static bool inject_before, inject_after;
static SystemMode_t injected_mode;
static jmp_buf cycle_exit;
static char logs[32768];
static size_t log_length;

int __wrap_printf(const char *format, ...) {
    assert(!mode_mutex.held && !fault_mutex.held);
    assert(!eps_mutex.held || external_writer);
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(logs + log_length, sizeof(logs) - log_length, format, arguments);
    va_end(arguments);
    assert(written >= 0 && (size_t)written < sizeof(logs) - log_length);
    log_length += (size_t)written;
    return written;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return allocating_mode ? &mode_mutex : &fault_mutex;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks) {
    assert(handle == &mode_mutex || handle == &fault_mutex || handle == &eps_mutex);
    MockMutex *mutex = handle;
    assert(!mutex->held); // No recursive locking or reverse-order callback operations.
    if (handle == &eps_mutex) {
        assert(!mode_mutex.held && !fault_mutex.held);
        if (ticks == portMAX_DELAY) {
            assert(in_manager);
            if (fail_manager_lock) { --fail_manager_lock; return pdFALSE; }
        } else {
            assert(ticks == pdMS_TO_TICKS(10) && !in_manager);
            if (fail_getter_lock) { --fail_getter_lock; return pdFALSE; }
        }
    } else if (handle == &mode_mutex) {
        assert(ticks == portMAX_DELAY || ticks == pdMS_TO_TICKS(10));
        assert(!eps_mutex.held || guard_active || external_writer || resolving);
        if (guard_active && !external_writer && fail_guard) {
            --fail_guard;
            return pdFALSE;
        }
    } else {
        assert(!eps_mutex.held && !mode_mutex.held && ticks == portMAX_DELAY);
    }
    mutex->held = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
    MockMutex *mutex = handle;
    assert(mutex != NULL && mutex->held);
    mutex->held = false;
    return pdTRUE;
}

typedef struct {
    SystemModeAction_t action;
    void *context;
} ProxyContext;

static void proxy_action(SystemMode_t mode, void *context) {
    ProxyContext *proxy = context;
    assert(mode_mutex.held && !fault_mutex.held);
    if (in_manager) assert(eps_mutex.held);
    ++callback_calls;
    proxy->action(mode, proxy->context);
}

bool __wrap_SYSTEM_MODE_WithCurrent(SystemModeAction_t action, void *context) {
    if (action == NULL) return __real_SYSTEM_MODE_WithCurrent(action, context);
    if (in_manager) assert(eps_mutex.held);
    ++guard_calls;
    guard_active = true;
    if (inject_before) {
        inject_before = false;
        external_writer = true;
        assert(SYSTEM_MODE_Set(injected_mode));
        external_writer = false;
    }
    ProxyContext proxy = {.action = action, .context = context};
    bool applied = __real_SYSTEM_MODE_WithCurrent(proxy_action, &proxy);
    assert(!mode_mutex.held);
    if (inject_after) {
        assert(applied); // New fault decision after a valid, atomic PDU authorization.
        inject_after = false;
        external_writer = true;
        assert(SYSTEM_MODE_Set(injected_mode));
        external_writer = false;
    }
    guard_active = false;
    return applied;
}

void __wrap_EPS_Monitor(float voltage) {
    assert(!eps_mutex.held && !mode_mutex.held && !fault_mutex.held);
    ++monitor_calls;
    __real_EPS_Monitor(voltage);
}

void __wrap_FDIR_ReportFault(FaultReport_t report) {
    assert(!eps_mutex.held && !mode_mutex.held && !fault_mutex.held);
    assert(report.source == SRC_EPS && report.severity == FAULT_CRITICAL);
    assert(report.fault_code == FAULT_EPS_LOW_VOLTAGE && report.timestamp == 42);
    ++reports;
    __real_FDIR_ReportFault(report);
}

bool __wrap_FDIR_ResolveCriticalFault(FaultSource_t source, uint16_t code, uint32_t revision) {
    assert(in_manager && eps_mutex.held && !mode_mutex.held && !fault_mutex.held);
    assert(source == SRC_EPS && code == FAULT_EPS_LOW_VOLTAGE);
    assert(isfinite(g_eps_telemetry.f_BusVoltage) && g_eps_telemetry.f_BusVoltage >= EPS_VOLTAGE_RECOVERY);
    resolving = true;
    bool resolved = __real_FDIR_ResolveCriticalFault(source, code, revision);
    resolving = false;
    return resolved;
}

uint32_t TIME_GetSeconds(void) {
    assert(!eps_mutex.held && !mode_mutex.held);
    return 42;
}
bool YAMCS_SendEvent(const char *format, ...) {
    assert(!"no ground ACK/NACK or socket access expected");
    return false;
}

void vTaskDelay(TickType_t ticks) {
    assert(ticks == pdMS_TO_TICKS(100));
    assert(!eps_mutex.held && !mode_mutex.held && !fault_mutex.held);
    assert(!guard_active && in_manager);
    ++delays;
    assert(pets == delays);
    longjmp(cycle_exit, 1);
}
void watchdog_pet(WatchdogTaskID_t task) {
    assert(task == WDT_TASK_EPS_MANAGER && in_manager && !eps_mutex.held && !mode_mutex.held && !fault_mutex.held);
    ++pets;
}

static void run_cycle(void) {
    in_manager = true;
    if (setjmp(cycle_exit) == 0) {
        vEPS_Manager_Task(NULL);
        assert(!"task returned without completing its 100 ms cycle");
    }
    in_manager = false;
    assert(!eps_mutex.held && !mode_mutex.held && !fault_mutex.held);
}

static void reset_evidence(void) {
    reports = monitor_calls = guard_calls = callback_calls = delays = pets = 0;
    logs[0] = '\0';
    log_length = 0;
}

static void assert_state(eps_mode_t local, bool on, bool safe) {
    assert(EPS_GetMode() == local);
    assert((g_eps_telemetry.ul_PDU_Register & LOADS) == (on ? LOADS : 0));
    assert((g_eps_telemetry.ul_PDU_Register & ~LOADS) == OTHER_BITS);
    assert(g_eps_telemetry.xFaultFlags.safe_mode_active == safe);
    assert(g_eps_telemetry.xFaultFlags.over_current_fault == 1);
    assert(g_eps_telemetry.xFaultFlags.over_temp_fault == 1);
}

static void seed_local(unsigned local) {
    if (local == EPS_MODE_NOMINAL) return;
    if (local == EPS_MODE_LOW_VOLTAGE) {
        g_eps_telemetry.f_BusVoltage = 4.99f;
        run_cycle();
        assert_state(EPS_MODE_LOW_VOLTAGE, false, true);
    } else {
        assert(SYSTEM_MODE_Set(MODE_SAFE));
        run_cycle();
        assert_state(EPS_MODE_SAFE, false, true);
        if (local == EPS_MODE_RECOVERY) {
            assert(SYSTEM_MODE_Set(MODE_NOMINAL));
            run_cycle();
            assert_state(EPS_MODE_RECOVERY, true, false);
        }
    }
    assert(EPS_GetMode() == (eps_mode_t)local);
    g_eps_telemetry.f_BusVoltage = 8.5f;
}

static bool run_matrix(const char *name) {
    static const char *global_names[] = {"boot", "safe", "nominal", "degraded", "emergency", "critical"};
    static const char *local_names[] = {"nominal", "low", "safe", "recovery"};
    static const char *voltage_names[] = {"low", "at_low", "mid", "below_recovery", "at_recovery",
        "just_below_low", "just_above_low", "just_below_recovery", "just_above_recovery",
        "nan", "pos_inf", "neg_inf"};
    const float voltages[] = {4.99f, 5.0f, 6.0f, 8.49f, 8.5f,
        nextafterf(5.0f, -INFINITY), nextafterf(5.0f, INFINITY),
        nextafterf(8.5f, -INFINITY), nextafterf(8.5f, INFINITY), NAN, INFINITY, -INFINITY};
    // Explicit classes independent of the firmware's state-machine branches.
    static const bool low[] = {true,false,false,false,false,true,false,false,false,false,false,false};
    static const bool recovered[] = {false,false,false,false,true,false,false,false,true,false,false,false};
    char candidate[96];
    for (unsigned global = 0; global < 6; ++global) {
        for (unsigned local = 0; local < 4; ++local) {
            for (unsigned v = 0; v < sizeof(voltages) / sizeof(voltages[0]); ++v) {
                snprintf(candidate, sizeof(candidate), "%s_%s_%s", global_names[global],
                         local_names[local], voltage_names[v]);
                if (strcmp(name, candidate) != 0) continue;
                seed_local(local);
                SystemMode_t mode = (SystemMode_t)global;
                const bool selectable = local != EPS_MODE_LOW_VOLTAGE ||
                    (mode != MODE_INITIAL_BOOT && mode != MODE_NOMINAL && mode != MODE_DEGRADED);
                assert(SYSTEM_MODE_Set(mode) == selectable);
                if (!selectable) {
                    assert(FDIR_HasActiveFaults());
                    mode = MODE_SAFE; // Real seeded low-voltage cause inhibits operating/BOOT requests.
                }
                g_eps_telemetry.f_BusVoltage = voltages[v];
                reset_evidence();
                run_cycle();
                eps_mode_t expected_local = (eps_mode_t)local;
                bool on = local == EPS_MODE_NOMINAL || local == EPS_MODE_RECOVERY;
                bool safe = !on;
                const bool normal_permission = mode == MODE_NOMINAL || mode == MODE_DEGRADED;
                const bool restricted = mode == MODE_SAFE || mode == MODE_EMERGENCY || mode == MODE_CRITICAL;
                const bool invalid = v >= 9;
                const bool new_low_fault = low[v] &&
                    (local == EPS_MODE_NOMINAL || local == EPS_MODE_RECOVERY);
                if (restricted || invalid) {
                    expected_local = EPS_MODE_SAFE; on = false; safe = true;
                } else if (low[v]) {
                    expected_local = new_low_fault ? EPS_MODE_LOW_VOLTAGE : EPS_MODE_SAFE;
                    on = false; safe = true;
                } else if (local == EPS_MODE_LOW_VOLTAGE) {
                    expected_local = EPS_MODE_SAFE; on = false; safe = true;
                } else if (local == EPS_MODE_SAFE && normal_permission && recovered[v]) {
                    expected_local = EPS_MODE_RECOVERY; on = true; safe = false;
                } else if (local == EPS_MODE_RECOVERY) {
                    expected_local = normal_permission ? EPS_MODE_NOMINAL : EPS_MODE_SAFE;
                    on = normal_permission; safe = !on;
                }
                assert_state(expected_local, on, safe);
                assert(g_eps_telemetry.xFaultFlags.low_voltage_warn == (low[v] || invalid));
                assert(reports == (unsigned)(new_low_fault || invalid) && monitor_calls == 1);
                assert(isnan(g_eps_telemetry.f_StateOfCharge) == invalid);
                assert(guard_calls == 1 && callback_calls == 1 && delays == 1);
                const SystemMode_t expected_global = (new_low_fault || invalid) && !restricted ? MODE_SAFE : mode;
                assert(SYSTEM_MODE_Get() == expected_global); // EPS recovery never writes NOMINAL.
                return true;
            }
        }
    }
    return false;
}

typedef struct { unsigned calls; SystemMode_t observed; } CallbackResult;
static void check_callback(SystemMode_t mode, void *context) {
    assert(mode_mutex.held && !eps_mutex.held);
    CallbackResult *result = context;
    ++result->calls;
    result->observed = mode;
}

static void run_special(const char *name) {
    if (strcmp(name, "soc_boundaries") == 0) {
        assert(isnan(EPS_CalculateSoC(NAN)));
        assert(isnan(EPS_CalculateSoC(INFINITY)));
        assert(isnan(EPS_CalculateSoC(-INFINITY)));
        assert(EPS_CalculateSoC(-FLT_MAX) == 0 && EPS_CalculateSoC(FLT_MAX) == 100);
        assert(EPS_CalculateSoC(nextafterf(0.0f, -INFINITY)) == 0);
        assert(EPS_CalculateSoC(0.001f) > 0);
        assert(EPS_CalculateSoC(nextafterf(9.0f, -INFINITY)) < 100);
        assert(EPS_CalculateSoC(nextafterf(9.0f, INFINITY)) == 100);
        float previous = 0;
        for (unsigned i = 0; i <= 3000; ++i) {
            const float soc = EPS_CalculateSoC(-10.0f + i * 0.01f);
            assert(isfinite(soc) && soc >= previous && soc >= 0 && soc <= 100);
            previous = soc;
        }
    } else if (strcmp(name, "null_commands") == 0) {
        const uint8_t command = EPS_CMD_HEATER_CTRL;
        EPS_ProcessCommand(NULL, 0);
        EPS_ProcessCommand(NULL, 1);
        EPS_ProcessCommand(NULL, UINT16_MAX);
        EPS_ProcessCommand(&command, 0);
        assert_state(EPS_MODE_NOMINAL, true, false);
        assert(reports == 0 && monitor_calls == 0 && guard_calls == 0);
    } else if (strcmp(name, "threshold_chatter") == 0 || strcmp(name, "invalid_recovery") == 0) {
        const bool invalid = strcmp(name, "invalid_recovery") == 0;
        g_eps_telemetry.f_BusVoltage = invalid ? NAN : nextafterf(5.0f, -INFINITY);
        run_cycle();
        assert_state(invalid ? EPS_MODE_SAFE : EPS_MODE_LOW_VOLTAGE, false, true);
        assert(SYSTEM_MODE_Get() == MODE_SAFE && FDIR_HasActiveFaults());
        const float chatter[] = {5.0f, nextafterf(5.0f, INFINITY), 6.0f,
                                 nextafterf(8.5f, -INFINITY)};
        for (unsigned i = 0; i < sizeof(chatter) / sizeof(chatter[0]); ++i) {
            g_eps_telemetry.f_BusVoltage = chatter[i];
            run_cycle();
            assert_state(EPS_MODE_SAFE, false, true);
            assert(FDIR_HasActiveFaults() && !SYSTEM_MODE_Set(MODE_NOMINAL));
        }
        g_eps_telemetry.f_BusVoltage = 8.5f;
        run_cycle();
        assert_state(EPS_MODE_SAFE, false, true);
        assert(SYSTEM_MODE_Get() == MODE_SAFE && !FDIR_HasActiveFaults());
        assert(SYSTEM_MODE_Set(MODE_NOMINAL)); // Explicit restart only after verified recovery.
        g_eps_telemetry.f_BusVoltage = nextafterf(8.5f, -INFINITY);
        run_cycle(); assert_state(EPS_MODE_SAFE, false, true);
        g_eps_telemetry.f_BusVoltage = 8.5f;
        run_cycle(); assert_state(EPS_MODE_RECOVERY, true, false);
        g_eps_telemetry.f_BusVoltage = nextafterf(5.0f, INFINITY);
        run_cycle(); assert_state(EPS_MODE_NOMINAL, true, false);
        g_eps_telemetry.f_BusVoltage = nextafterf(5.0f, -INFINITY);
        run_cycle(); assert_state(EPS_MODE_LOW_VOLTAGE, false, true);
        assert(SYSTEM_MODE_Get() == MODE_SAFE && reports == 2);
    } else if (strcmp(name, "uninitialized_mode") == 0) {
        run_cycle();
        assert_state(EPS_MODE_SAFE, false, true);
        assert(callback_calls == 0 && monitor_calls == 1 && reports == 0);
    } else if (strcmp(name, "uninitialized_callback") == 0) {
        CallbackResult result = {0};
        assert(!SYSTEM_MODE_WithCurrent(check_callback, &result));
        assert(result.calls == 0 && callback_calls == 0 && !mode_mutex.held);
    } else if (strcmp(name, "mode_lock_low_voltage") == 0) {
        fail_guard = 1;
        g_eps_telemetry.f_BusVoltage = 4.99f;
        run_cycle();
        assert_state(EPS_MODE_SAFE, false, true);
        assert(callback_calls == 0 && reports == 1 && monitor_calls == 1);
        assert(g_eps_telemetry.xFaultFlags.low_voltage_warn == 1);
        assert(SYSTEM_MODE_Get() == MODE_SAFE); // Fault reporting survives a failed permission guard.
    } else if (strcmp(name, "mode_lock_failure") == 0 || strcmp(name, "mode_lock_retry") == 0) {
        fail_guard = 1;
        run_cycle();
        assert_state(EPS_MODE_SAFE, false, true);
        assert(callback_calls == 0 && strstr(logs, "unavailable (loads shed)") != NULL);
        assert(SYSTEM_MODE_Get() == MODE_NOMINAL);
        if (strcmp(name, "mode_lock_retry") == 0) {
            run_cycle();
            assert_state(EPS_MODE_RECOVERY, true, false);
            assert(callback_calls == 1 && delays == 2);
        }
    } else if (strcmp(name, "eps_lock_failure") == 0 || strcmp(name, "eps_lock_retry") == 0) {
        assert(SYSTEM_MODE_Set(MODE_SAFE));
        fail_manager_lock = 1;
        run_cycle();
        assert_state(EPS_MODE_NOMINAL, true, false); // No unprotected writes on lock failure.
        assert(guard_calls == 0 && monitor_calls == 0 && delays == 1);
        if (strcmp(name, "eps_lock_retry") == 0) {
            run_cycle();
            assert_state(EPS_MODE_SAFE, false, true);
            assert(guard_calls == 1 && delays == 2);
        }
    } else if (strcmp(name, "getter_lock_failure") == 0) {
        fail_getter_lock = 1;
        assert(EPS_GetMode() == EPS_MODE_SAFE);
        assert_state(EPS_MODE_NOMINAL, true, false);
    } else if (strcmp(name, "getter_null_mutex") == 0) {
        xEPSDataMutex = NULL;
        assert(EPS_GetMode() == EPS_MODE_SAFE);
        xEPSDataMutex = &eps_mutex;
        assert_state(EPS_MODE_NOMINAL, true, false);
    } else if (strcmp(name, "null_callback") == 0) {
        assert(!SYSTEM_MODE_WithCurrent(NULL, NULL));
        assert(guard_calls == 0 && callback_calls == 0 && SYSTEM_MODE_Get() == MODE_NOMINAL);
    } else if (strcmp(name, "callback_contract") == 0) {
        CallbackResult result = {0};
        assert(SYSTEM_MODE_WithCurrent(check_callback, &result));
        assert(result.calls == 1 && result.observed == MODE_NOMINAL);
        assert(SYSTEM_MODE_Set(MODE_EMERGENCY));
        assert(SYSTEM_MODE_WithCurrent(check_callback, &result));
        assert(result.calls == 2 && result.observed == MODE_EMERGENCY);
        assert(!mode_mutex.held);
    } else if (strcmp(name, "emergency_before_restore") == 0 ||
               strcmp(name, "critical_before_restore") == 0 || strcmp(name, "safe_before_restore") == 0) {
        seed_local(EPS_MODE_SAFE);
        assert(SYSTEM_MODE_Set(MODE_NOMINAL));
        injected_mode = strcmp(name, "emergency_before_restore") == 0 ? MODE_EMERGENCY :
            (strcmp(name, "critical_before_restore") == 0 ? MODE_CRITICAL : MODE_SAFE);
        inject_before = true;
        reset_evidence();
        run_cycle();
        assert_state(EPS_MODE_SAFE, false, true);
        assert(SYSTEM_MODE_Get() == injected_mode && !inject_before);
    } else if (strcmp(name, "safety_after_commit") == 0) {
        seed_local(EPS_MODE_SAFE);
        assert(SYSTEM_MODE_Set(MODE_NOMINAL));
        injected_mode = MODE_EMERGENCY;
        inject_after = true;
        reset_evidence();
        run_cycle();
        assert_state(EPS_MODE_RECOVERY, true, false); // Authorized before the new safety decision.
        assert(SYSTEM_MODE_Get() == MODE_EMERGENCY);
        run_cycle();
        assert_state(EPS_MODE_SAFE, false, true); // Periodic enforcement, not an instantaneous interlock.
        assert(delays == 2);
    } else if (strcmp(name, "recovery_lifecycle") == 0 || strcmp(name, "recovery_relapse") == 0) {
        seed_local(EPS_MODE_SAFE);
        reset_evidence();
        for (unsigned i = 0; i < 3; ++i) {
            run_cycle(); assert_state(EPS_MODE_SAFE, false, true);
        }
        assert(SYSTEM_MODE_Set(MODE_NOMINAL));
        g_eps_telemetry.f_BusVoltage = 8.49f;
        run_cycle(); assert_state(EPS_MODE_SAFE, false, true);
        g_eps_telemetry.f_BusVoltage = 8.5f;
        run_cycle(); assert_state(EPS_MODE_RECOVERY, true, false);
        g_eps_telemetry.f_BusVoltage = strcmp(name, "recovery_relapse") == 0 ? 4.99f : 5.0f;
        run_cycle();
        if (strcmp(name, "recovery_relapse") == 0) {
            assert_state(EPS_MODE_LOW_VOLTAGE, false, true);
            assert(SYSTEM_MODE_Get() == MODE_SAFE && reports == 1);
            run_cycle(); assert_state(EPS_MODE_SAFE, false, true);
        } else {
            assert_state(EPS_MODE_NOMINAL, true, false);
            assert(SYSTEM_MODE_Get() == MODE_NOMINAL && reports == 0);
        }
    } else if (strcmp(name, "boot_preserve_off") == 0) {
        assert(SYSTEM_MODE_Set(MODE_INITIAL_BOOT));
        g_eps_telemetry.ul_PDU_Register &= ~LOADS;
        run_cycle(); assert_state(EPS_MODE_NOMINAL, false, false);
    } else if (strcmp(name, "comms_off_preserved") == 0) {
        g_eps_telemetry.ul_PDU_Register &= ~PDU_LOAD_COMMS_BIT;
        assert(SYSTEM_MODE_Set(MODE_SAFE));
        run_cycle();
        assert(g_eps_telemetry.ul_PDU_Register == UINT32_C(0xA0000000));
        assert(SYSTEM_MODE_Set(MODE_NOMINAL));
        run_cycle();
        assert(g_eps_telemetry.ul_PDU_Register == (UINT32_C(0xA0000000) | LOADS));
        assert(EPS_GetMode() == EPS_MODE_RECOVERY);
    } else if (strcmp(name, "unrelated_bits") == 0) {
        assert(SYSTEM_MODE_Set(MODE_CRITICAL));
        run_cycle(); assert_state(EPS_MODE_SAFE, false, true);
        assert(SYSTEM_MODE_Set(MODE_DEGRADED));
        run_cycle(); assert_state(EPS_MODE_RECOVERY, true, false);
    } else if (strcmp(name, "repeated_safe") == 0) {
        assert(SYSTEM_MODE_Set(MODE_SAFE));
        reset_evidence();
        for (unsigned i = 0; i < 10; ++i) {
            run_cycle(); assert_state(EPS_MODE_SAFE, false, true);
        }
        assert(reports == 0 && delays == 10);
        const char *first = strstr(logs, "MANAGER: EPS mode");
        assert(first != NULL && strstr(first + 1, "MANAGER: EPS mode") == NULL);
    } else if (strcmp(name, "heater_stub") == 0) {
        seed_local(EPS_MODE_SAFE);
        reset_evidence();
        const uint8_t heater_on[] = {EPS_CMD_HEATER_CTRL, 1};
        const uint8_t heater_off[] = {EPS_CMD_HEATER_CTRL, 0};
        EPS_ProcessCommand(heater_on, sizeof(heater_on));
        EPS_ProcessCommand(heater_off, sizeof(heater_off));
        assert_state(EPS_MODE_SAFE, false, true);
        assert(strstr(logs, "stub; no load change") != NULL);
        assert(strstr(logs, "command executed") == NULL && reports == 0);
    } else if (strcmp(name, "monitor_3_3") == 0) {
        EPS_Monitor(3.3f); assert(reports == 0);
        EPS_Monitor(3.29f); assert(reports == 1 && SYSTEM_MODE_Get() == MODE_SAFE);
        assert(SYSTEM_MODE_Set(MODE_EMERGENCY));
        EPS_Monitor(3.29f); assert(reports == 2 && SYSTEM_MODE_Get() == MODE_EMERGENCY);
    } else if (strcmp(name, "soc_sunrise") == 0) {
        assert(EPS_CalculateSoC(0.0f) == 0 && EPS_CalculateSoC(-1.0f) == 0);
        assert(EPS_CalculateSoC(9.0f) == 100 && EPS_CalculateSoC(10.0f) == 100);
        assert(fabsf(EPS_CalculateSoC(7.2f) - 80.0f) < 0.001f);
        g_eps_telemetry.f_BusVoltage = 7.2f;
        g_eps_telemetry.f_SolarInputPower = 1.0f;
        run_cycle();
        assert(g_eps_telemetry.f_SolarInputPower == 1.0f);
        assert(fabsf(g_eps_telemetry.f_StateOfCharge - 80.0f) < 0.001f);
        run_cycle();
        assert(g_eps_telemetry.f_SolarInputPower == 1.0f);
        assert_state(EPS_MODE_NOMINAL, true, false);
    } else {
        assert(!"unknown special case");
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    if (strcmp(argv[1], "uninitialized_mode") != 0 && strcmp(argv[1], "uninitialized_callback") != 0) {
        allocating_mode = true;
        // Explicit logical completion; real deployment timing has its own test.
        assert(SYSTEM_MODE_Init(MODE_INITIAL_BOOT));
        assert(SYSTEM_MODE_CompleteDeployment());
        allocating_mode = false;
    }
    assert(FDIR_Init());
    g_eps_telemetry.f_BusVoltage = 8.5f;
    g_eps_telemetry.ul_PDU_Register = OTHER_BITS | LOADS;
    g_eps_telemetry.xFaultFlags.over_current_fault = 1;
    g_eps_telemetry.xFaultFlags.over_temp_fault = 1;
    reset_evidence();
    if (!run_matrix(argv[1])) run_special(argv[1]);
    assert(!eps_mutex.held && !mode_mutex.held && !fault_mutex.held);
    assert(!guard_active && !external_writer && !in_manager);
    fprintf(stdout, "PASS: EPS load policy %s (real manager/mode/FDIR/commands).\n", argv[1]);
    return 0;
}

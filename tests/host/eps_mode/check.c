/* Real EPS monitor + mode service + FDIR; deterministic host-only interleavings. */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "eps_control.h"
#include "eps_telemetry.h"
#include "eps_commands.h"
#include "fdir_service.h"
#include "system_mode.h"
#include "time_service.h"
#include "watchdog.h"
#include "yamcs_interface.h"

typedef struct { bool held; } MockMutex;
static MockMutex mode_mutex, fault_mutex, eps_mutex;
EPS_Telemetry_t g_eps_telemetry;
SemaphoreHandle_t xEPSDataMutex = &eps_mutex;

typedef enum {
    INJECT_NONE, FAIL_CRITICAL_LOCK, FAIL_RECOVERY_LOCK,
    EMERGENCY_DURING_REPORT, EMERGENCY_BEFORE_FDIR_SAFE,
    EMERGENCY_BEFORE_CRITICAL, EMERGENCY_BEFORE_RECOVERY,
    CHANGE_BEFORE_CRITICAL, CHANGE_BEFORE_RECOVERY
} Injection;

static Injection injection;
static bool allocating_mode, fail_eps_lock, fail_next_mode_write;
static bool injected, mode_uninitialized;
static unsigned pets, delays, reports, critical_attempts, recovery_attempts;
static unsigned cycles = 1, expected_reports, expected_critical_success, expected_recovery_success;
static unsigned expected_critical_failure, expected_recovery_failure;
static SystemMode_t expected_first, expected_final;
static jmp_buf task_exit;
static char logs[16384];
static size_t log_length;

bool __real_SYSTEM_MODE_Set(SystemMode_t mode);
bool __real_SYSTEM_MODE_Transition(SystemMode_t expected, SystemMode_t next);
void __real_FDIR_ReportFault(FaultReport_t report);
bool __real_SYSTEM_MODE_RecordFault(uint8_t source, uint16_t code, bool fatal, uint32_t timestamp);

int __wrap_printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(logs + log_length, sizeof(logs) - log_length, format, arguments);
    va_end(arguments);
    assert(written >= 0 && (size_t)written < sizeof(logs) - log_length);
    log_length += (size_t)written;
    return written;
}

static unsigned occurrences(const char *message) {
    unsigned count = 0;
    const char *position = logs;
    while ((position = strstr(position, message)) != NULL) {
        ++count;
        position += strlen(message);
    }
    return count;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return allocating_mode ? &mode_mutex : &fault_mutex;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks) {
    assert(handle == &mode_mutex || handle == &fault_mutex || handle == &eps_mutex);
    MockMutex *mutex = handle;
    assert(!mutex->held);
    if (handle == &eps_mutex) {
        assert(ticks == pdMS_TO_TICKS(100));
        if (fail_eps_lock) return pdFALSE;
    } else if (handle == &mode_mutex) {
        assert(ticks == portMAX_DELAY || ticks == pdMS_TO_TICKS(10));
        if (ticks == pdMS_TO_TICKS(10) && fail_next_mode_write) {
            fail_next_mode_write = false;
            return pdFALSE;
        }
    } else {
        assert(ticks == portMAX_DELAY);
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

/* Unconditional writes in the real monitor would violate this bounded policy. */
bool __wrap_SYSTEM_MODE_Set(SystemMode_t mode) {
    assert(!"unexpected unconditional SYSTEM_MODE_Set in power fault/recovery path");
    return false;
}

static void inject_mode(SystemMode_t mode) {
    assert(!eps_mutex.held && !mode_mutex.held && !fault_mutex.held);
    assert(__real_SYSTEM_MODE_Set(mode));
    injected = true;
}

void __wrap_FDIR_ReportFault(FaultReport_t report) {
    assert(!eps_mutex.held);
    assert(report.source == SRC_EPS && report.severity == FAULT_CRITICAL);
    assert(report.fault_code == FAULT_EPS_LOW_VOLTAGE && report.timestamp == 42);
    ++reports;
    if (injection == EMERGENCY_DURING_REPORT && !injected) {
        inject_mode(MODE_EMERGENCY);
    }
    __real_FDIR_ReportFault(report);
}

bool __wrap_SYSTEM_MODE_RecordFault(uint8_t source, uint16_t code, bool fatal, uint32_t timestamp) {
    assert(!eps_mutex.held && !fault_mutex.held && !mode_mutex.held);
    if (injection == EMERGENCY_BEFORE_FDIR_SAFE && !injected) inject_mode(MODE_EMERGENCY);
    return __real_SYSTEM_MODE_RecordFault(source, code, fatal, timestamp);
}

bool __wrap_SYSTEM_MODE_Transition(SystemMode_t expected, SystemMode_t next) {
    assert(!eps_mutex.held && !mode_mutex.held && !fault_mutex.held);
    if (next == MODE_CRITICAL) {
        ++critical_attempts;
        // The real FDIR power-fault path has already moved eligible modes to SAFE.
        assert(expected == MODE_SAFE);
        if (!injected && injection == FAIL_CRITICAL_LOCK) {
            fail_next_mode_write = true;
            injected = true;
        } else if (!injected && injection == EMERGENCY_BEFORE_CRITICAL) {
            inject_mode(MODE_EMERGENCY);
        } else if (!injected && injection == CHANGE_BEFORE_CRITICAL) {
            assert(!__real_SYSTEM_MODE_Set(MODE_DEGRADED)); // New active-fault gate rejects the old bypass.
            inject_mode(MODE_CRITICAL); // A legal concurrent safety decision still invalidates the CAS.
        }
    } else if (expected == MODE_CRITICAL && next == MODE_SAFE) {
        ++recovery_attempts;
        if (!injected && injection == FAIL_RECOVERY_LOCK) {
            fail_next_mode_write = true;
            injected = true;
        } else if (!injected && injection == EMERGENCY_BEFORE_RECOVERY) {
            inject_mode(MODE_EMERGENCY);
        } else if (!injected && injection == CHANGE_BEFORE_RECOVERY) {
            inject_mode(MODE_NOMINAL); // Independent writer; the monitor must not overwrite it.
        }
    } else {
        assert(next == MODE_SAFE); // Existing FDIR escalation, not power recovery.
        if (!injected && injection == EMERGENCY_BEFORE_FDIR_SAFE) {
            inject_mode(MODE_EMERGENCY);
        }
    }
    return __real_SYSTEM_MODE_Transition(expected, next);
}

uint32_t TIME_GetSeconds(void) { return 42; }
bool YAMCS_SendEvent(const char *format, ...) {
    assert(!"no ground-command ACK/NACK or network event expected in this test");
    return false;
}

void watchdog_pet(WatchdogTaskID_t task_id) {
    assert(task_id == WDT_TASK_EPS_MON);
    assert(!eps_mutex.held && !mode_mutex.held && !fault_mutex.held);
    ++pets;
}

void vTaskDelay(TickType_t ticks) {
    assert(ticks == pdMS_TO_TICKS(1000));
    assert(!eps_mutex.held && !mode_mutex.held && !fault_mutex.held);
    ++delays;
    assert(pets == delays);
    if (delays == 1) {
        assert(SYSTEM_MODE_Get() == expected_first);
        if (cycles == 2) {
            // A rejected transition did not emit a success message or retry in a busy loop.
            assert(occurrences("CDH: Low voltage! Entered MODE_CRITICAL.") == 0);
            assert(occurrences("CDH: Power Recovery detected") == 0);
            assert(critical_attempts + recovery_attempts == 1);
        }
    }
    if (delays == cycles) longjmp(task_exit, 1);
}

static bool configure_matrix(const char *name, SystemMode_t *initial, float *voltage) {
    static const char *mode_names[] = {"boot", "safe", "nominal", "degraded", "emergency", "critical"};
    static const char *voltage_names[] = {"low", "at_low", "at_recovery", "recovery",
        "just_below_low", "just_above_low", "just_below_recovery", "just_above_recovery"};
    const float voltages[] = {2.49f, 2.5f, 6.0f, 6.01f,
        nextafterf(2.5f, -INFINITY), nextafterf(2.5f, INFINITY),
        nextafterf(6.0f, -INFINITY), nextafterf(6.0f, INFINITY)};
    char candidate[64];
    for (unsigned v = 0; v < sizeof(voltages) / sizeof(voltages[0]); ++v) {
        for (unsigned m = 0; m < 6; ++m) {
            snprintf(candidate, sizeof(candidate), "%s_%s", voltage_names[v], mode_names[m]);
            if (strcmp(name, candidate) != 0) continue;
            *initial = (SystemMode_t)m;
            *voltage = voltages[v];
            expected_first = expected_final = *initial;
            if (*voltage < 2.5f && *initial != MODE_CRITICAL) {
                expected_reports = 1;
                if (*initial != MODE_EMERGENCY) {
                    expected_first = expected_final = MODE_CRITICAL;
                    expected_critical_success = 1;
                }
            } else if (*voltage > 6.0f && *initial == MODE_CRITICAL) {
                expected_first = expected_final = MODE_SAFE;
                expected_recovery_success = 1;
            }
            return true;
        }
    }
    static const char *invalid_names[] = {"nan", "pos_inf", "neg_inf"};
    const float invalid_values[] = {NAN, INFINITY, -INFINITY};
    for (unsigned v = 0; v < 3; ++v) {
        for (unsigned m = 0; m < 6; ++m) {
            snprintf(candidate, sizeof(candidate), "%s_%s", invalid_names[v], mode_names[m]);
            if (strcmp(name, candidate) != 0) continue;
            *initial = (SystemMode_t)m;
            *voltage = invalid_values[v];
            expected_first = expected_final = m == MODE_EMERGENCY || m == MODE_CRITICAL ?
                (SystemMode_t)m : MODE_SAFE;
            expected_reports = 1;
            return true;
        }
    }
    return false;
}

static void configure_special(const char *name, SystemMode_t *initial, float *voltage) {
    *initial = MODE_NOMINAL;
    *voltage = 2.49f;
    expected_reports = 1;
    if (strcmp(name, "eps_mutex_failure") == 0) {
        fail_eps_lock = true;
        expected_reports = 0;
        expected_first = expected_final = MODE_NOMINAL;
    } else if (strcmp(name, "uninitialized_mode") == 0) {
        mode_uninitialized = true;
        expected_first = expected_final = MODE_SAFE;
        expected_critical_failure = 1;
    } else if (strcmp(name, "critical_lock_failure") == 0 || strcmp(name, "critical_retry") == 0) {
        injection = FAIL_CRITICAL_LOCK;
        expected_first = expected_final = MODE_SAFE;
        expected_critical_failure = 1;
        if (strcmp(name, "critical_retry") == 0) {
            cycles = 2;
            expected_reports = 2;
            expected_final = MODE_CRITICAL;
            expected_critical_success = 1;
        }
    } else if (strcmp(name, "recovery_lock_failure") == 0 || strcmp(name, "recovery_retry") == 0) {
        *initial = MODE_CRITICAL;
        *voltage = 6.01f;
        expected_reports = 0;
        injection = FAIL_RECOVERY_LOCK;
        expected_first = expected_final = MODE_CRITICAL;
        expected_recovery_failure = 1;
        if (strcmp(name, "recovery_retry") == 0) {
            cycles = 2;
            expected_final = MODE_SAFE;
            expected_recovery_success = 1;
        }
    } else if (strcmp(name, "emergency_during_report") == 0) {
        injection = EMERGENCY_DURING_REPORT;
        expected_first = expected_final = MODE_EMERGENCY;
    } else if (strcmp(name, "emergency_before_fdir_safe") == 0) {
        injection = EMERGENCY_BEFORE_FDIR_SAFE;
        expected_first = expected_final = MODE_EMERGENCY;
    } else if (strcmp(name, "emergency_before_critical") == 0) {
        injection = EMERGENCY_BEFORE_CRITICAL;
        expected_first = expected_final = MODE_EMERGENCY;
        expected_critical_failure = 1;
    } else if (strcmp(name, "emergency_before_recovery") == 0) {
        *initial = MODE_CRITICAL;
        *voltage = 6.01f;
        expected_reports = 0;
        injection = EMERGENCY_BEFORE_RECOVERY;
        expected_first = expected_final = MODE_EMERGENCY;
        expected_recovery_failure = 1;
    } else if (strcmp(name, "critical_changed_before_transition") == 0) {
        injection = CHANGE_BEFORE_CRITICAL;
        expected_first = expected_final = MODE_CRITICAL;
        expected_critical_failure = 1;
    } else if (strcmp(name, "recovery_changed_before_transition") == 0) {
        *initial = MODE_CRITICAL;
        *voltage = 6.01f;
        expected_reports = 0;
        injection = CHANGE_BEFORE_RECOVERY;
        expected_first = expected_final = MODE_NOMINAL;
        expected_recovery_failure = 1;
    } else {
        assert(!"unknown test scenario");
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    SystemMode_t initial;
    float voltage;
    if (!configure_matrix(argv[1], &initial, &voltage)) {
        configure_special(argv[1], &initial, &voltage);
    }
    if (!mode_uninitialized) {
        allocating_mode = true;
        // This component fixture exercises post-deployment power policy.
        assert(SYSTEM_MODE_Init(MODE_INITIAL_BOOT));
        assert(SYSTEM_MODE_CompleteDeployment());
        assert(__real_SYSTEM_MODE_Set(initial));
        allocating_mode = false;
    }
    assert(FDIR_Init());
    g_eps_telemetry.f_BusVoltage = voltage;
    // Reset initialization logs so success assertions concern only the real task body.
    logs[0] = '\0';
    log_length = 0;
    if (setjmp(task_exit) == 0) {
        vEPSMonitoringTask(NULL);
        assert(!"monitor unexpectedly returned before its delay");
    }
    assert(SYSTEM_MODE_Get() == expected_final);
    if (!isfinite(voltage)) {
        assert(FDIR_HasActiveFaults());
        assert(!__real_SYSTEM_MODE_Set(MODE_NOMINAL));
        assert(critical_attempts == 0 && recovery_attempts == 0);
    }
    assert(reports == expected_reports && pets == cycles && delays == cycles);
    assert(occurrences("CDH: Low voltage! Entered MODE_CRITICAL.") == expected_critical_success);
    assert(occurrences("CDH: Power Recovery detected") == expected_recovery_success);
    assert(occurrences("CDH: CRITICAL transition not applied") == expected_critical_failure);
    assert(occurrences("CDH: SAFE recovery transition not applied") == expected_recovery_failure);
    assert(strstr(logs, "Transitioning to MODE_NOMINAL") == NULL);
    if (expected_recovery_success) {
        assert(strstr(logs, "Entered MODE_SAFE; MODE_NOMINAL not requested.") != NULL);
    }
    if (injection != INJECT_NONE) assert(injected);
    assert(!eps_mutex.held && !mode_mutex.held && !fault_mutex.held);
    fprintf(stdout, "PASS: EPS mode %s (real monitor/mode/FDIR; %u cycle(s)).\n", argv[1], cycles);
    return 0;
}

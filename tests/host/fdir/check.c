/* Real FDIR service/task and mode service; deterministic RTOS/YAMCS boundaries. */
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "fdir_service.h"
#include "fdir_task.h"
#include "system_mode.h"
#include "commands.h"
#include "time_service.h"
#include "yamcs_interface.h"
#include "watchdog.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

void __real_FDIR_Process(void);
typedef struct { bool held; } MockMutex;
static MockMutex mode_mutex, fault_mutex;
static unsigned allocations, mode_failures, process_fault_failures, next_fault_failures;
static bool fail_allocation, processing, ack_before_lock;
static int mode_before_lock = -1;
typedef enum { AFTER_NONE, AFTER_WARNING, AFTER_ACK, AFTER_EMERGENCY } AfterAction;
static AfterAction after_unlock;
static unsigned process_calls, mode_attempts, delays, handled_logs, pets;
static unsigned stack_queries, stack_reports;
static TickType_t now = 41;
static jmp_buf task_exit;
static const char *scenario;
static char logs[32768], event[256];
static size_t log_length;

static bool is(const char *name) { return strcmp(scenario, name) == 0; }
static void warning(FaultSource_t source) {
    FDIR_ReportFault((FaultReport_t){.source = source, .severity = FAULT_WARNING, .fault_code = 0xA123});
}
static void warnings(FaultSource_t source, unsigned count) {
    for (unsigned i = 0; i < count; ++i) warning(source);
}
static void ack(void) {
    const uint8_t command[] = {FDIR_CMD_ACK_FAULT};
    FDIR_ProcessCommand(command, sizeof(command));
}
static void ground_mode(SystemMode_t mode, bool accepted) {
    const uint8_t command[] = {FDIR_CMD_RESET_MODE, (uint8_t)mode};
    event[0] = '\0';
    FDIR_ProcessCommand(command, sizeof(command));
    assert(strstr(event, accepted ? "CMD_ACK: FDIR_SET_MODE=" : "CMD_NACK: FDIR_SET_MODE") != NULL);
}
int __wrap_printf(const char *format, ...) {
    assert(!mode_mutex.held && !fault_mutex.held);
    va_list arguments; va_start(arguments, format);
    int written = vsnprintf(logs + log_length, sizeof(logs) - log_length, format, arguments);
    va_end(arguments);
    assert(written >= 0 && (size_t)written < sizeof(logs) - log_length);
    if (strstr(format, "Warning threshold handled")) ++handled_logs;
    if (strstr(format, "stack_free_min_bytes")) ++stack_reports;
    log_length += (size_t)written;
    return written;
}
bool YAMCS_SendEvent(const char *format, ...) {
    assert(!mode_mutex.held && !fault_mutex.held);
    va_list arguments; va_start(arguments, format);
    int written = vsnprintf(event, sizeof(event), format, arguments);
    va_end(arguments);
    assert(written >= 0 && (size_t)written < sizeof(event));
    return true;
}
uint32_t TIME_GetSeconds(void) { return 42; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    ++allocations;
    if (fail_allocation) return NULL;
    assert(allocations <= 2);
    return allocations == 1 ? &mode_mutex : &fault_mutex;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t timeout) {
    assert(handle == &mode_mutex || handle == &fault_mutex);
    MockMutex *mutex = handle;
    assert(!mutex->held);
    assert(timeout == pdMS_TO_TICKS(10) || timeout == portMAX_DELAY);
    if (handle == &fault_mutex) {
        assert(!mode_mutex.held); // Never reverse the FDIR -> mode order.
        if (processing && timeout == pdMS_TO_TICKS(10) && ack_before_lock) {
            ack_before_lock = false; ack();
        }
        if (next_fault_failures) { --next_fault_failures; return pdFALSE; }
        if (processing && timeout == pdMS_TO_TICKS(10) && process_fault_failures) {
            --process_fault_failures; return pdFALSE;
        }
    } else if (processing && fault_mutex.held) {
        assert(timeout == pdMS_TO_TICKS(10));
        ++mode_attempts;
        if (mode_before_lock >= 0) {
            SystemMode_t injected = (SystemMode_t)mode_before_lock;
            mode_before_lock = -1;
            assert(SYSTEM_MODE_TransitionQuiet(MODE_NOMINAL, injected));
        }
        if (mode_failures) { --mode_failures; return pdFALSE; }
    }
    mutex->held = true;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
    MockMutex *mutex = handle;
    assert(mutex->held);
    mutex->held = false;
    if (handle == &fault_mutex && processing && after_unlock != AFTER_NONE) {
        AfterAction action = after_unlock; after_unlock = AFTER_NONE;
        if (action == AFTER_WARNING) warning(SRC_CDH);
        else if (action == AFTER_ACK) ack();
        else assert(SYSTEM_MODE_Set(MODE_EMERGENCY));
    }
    return pdTRUE;
}
void __wrap_FDIR_Process(void) {
    assert(!mode_mutex.held && !fault_mutex.held && !processing);
    ++process_calls;
    processing = true;
    __real_FDIR_Process();
    processing = false;
    assert(!mode_mutex.held && !fault_mutex.held);
}
static void init_mode(void) { assert(SYSTEM_MODE_Init(MODE_INITIAL_BOOT)); }
static void post_deployment(SystemMode_t mode) {
    assert(SYSTEM_MODE_CompleteDeployment());
    assert(SYSTEM_MODE_Set(mode));
}
TickType_t xTaskGetTickCount(void) { return now; }
unsigned uxTaskGetStackHighWaterMark(void *task) {
    assert(task == NULL && !mode_mutex.held && !fault_mutex.held && !processing);
    assert(process_calls == pets && process_calls == delays + 1);
    ++stack_queries;
    return 400; // Mock StackType_t is 4 bytes: expect 1600 bytes, not 400.
}
void watchdog_pet(WatchdogTaskID_t task) {
    assert(task == WDT_TASK_FDIR && !mode_mutex.held && !fault_mutex.held && !processing);
    ++pets;
}
void vTaskDelayUntil(TickType_t *previous_wake, TickType_t increment) {
    assert(!mode_mutex.held && !fault_mutex.held && !processing);
    assert(increment == pdMS_TO_TICKS(1000));
    assert(*previous_wake == now);
    *previous_wake += increment; now = *previous_wake;
    ++delays;
    assert(process_calls == delays); // First check is immediate, then periodic.
    assert(pets == delays);
    if (delays == 1) {
        if (is("task_nominal")) {
            assert(SYSTEM_MODE_Get() == MODE_NOMINAL && handled_logs == 0);
            warning(SRC_CDH);
        } else if (is("task_boot_deployment")) {
            assert(SYSTEM_MODE_Get() == MODE_INITIAL_BOOT && FDIR_HasActiveFaults());
            assert(SYSTEM_MODE_CompleteDeployment());
        } else if (is("task_ack")) {
            assert(SYSTEM_MODE_Get() == MODE_SAFE && FDIR_HasActiveFaults());
            ack(); ground_mode(MODE_NOMINAL, true);
        } else if (is("task_mode_retry") || is("task_counter_retry")) {
            assert(SYSTEM_MODE_Get() == MODE_NOMINAL && FDIR_HasActiveFaults() && handled_logs == 0);
        } else if (is("task_safe")) {
            assert(SYSTEM_MODE_Get() == MODE_SAFE && FDIR_HasActiveFaults());
        }
    }
    if (delays == (is("task_stack_periodic") ? 65u : 3u)) longjmp(task_exit, 1);
}
static void run_task(void) {
    if (setjmp(task_exit) == 0) { vFDIRTask(NULL); assert(!"FDIR task returned"); }
    const unsigned cycles = is("task_stack_periodic") ? 65u : 3u;
    assert(delays == cycles && process_calls == cycles && now == 41 + 1000 * cycles);
    assert(stack_queries == cycles / 30 && stack_reports == stack_queries);
    if (stack_queries) assert(strstr(logs, "FDIR: stack_free_min_bytes=1600.") != NULL);
    assert(strstr(logs, "Warning processing task initialized (1000 ms period)") != NULL);
}

static bool matrix(void) {
    static const char *names[] = {"boot", "safe", "nominal", "degraded", "emergency", "critical"};
    char candidate[80];
    for (unsigned mode = 0; mode < 6; ++mode) {
        for (unsigned count = 0; count < 5; ++count) {
            snprintf(candidate, sizeof(candidate), "mode_%s_count_%u", names[mode], count);
            if (!is(candidate)) continue;
            post_deployment((SystemMode_t)mode);
            warnings(SRC_CDH, count);
            FDIR_Process();
            bool handled = count >= 3 && (mode == MODE_NOMINAL || mode == MODE_DEGRADED);
            assert(SYSTEM_MODE_Get() == (handled ? MODE_DEGRADED : (SystemMode_t)mode));
            assert(FDIR_HasActiveFaults() == (count > 0 && !handled));
            assert(handled_logs == (handled ? 1u : 0u));
            FDIR_Process();
            assert(handled_logs == (handled ? 1u : 0u));
            if (count >= 3 && !handled) {
                ground_mode(MODE_NOMINAL, true);
                FDIR_Process();
                assert(SYSTEM_MODE_Get() == MODE_DEGRADED && !FDIR_HasActiveFaults() && handled_logs == 1);
            }
            return true;
        }
    }
    static const char *sources[] = {"payload", "eps", "comms", "archive", "cdh", "adcs"};
    for (unsigned source = 0; source < SRC_COUNT; ++source) {
        snprintf(candidate, sizeof(candidate), "source_%s", sources[source]);
        if (!is(candidate)) continue;
        post_deployment(MODE_NOMINAL);
        warnings((FaultSource_t)source, 3); FDIR_Process();
        assert(SYSTEM_MODE_Get() == MODE_DEGRADED && handled_logs == 1 && !FDIR_HasActiveFaults());
        return true;
    }
    return false;
}

int main(int argc, char **argv) {
    assert(argc == 2); scenario = argv[1];
    if (!is("mode_uninitialized")) init_mode();
    else allocations = 1; // Reserve the mock slot, without initializing mode.
    if (is("fdir_uninitialized")) {
        FDIR_Process(); assert(FDIR_HasActiveFaults()); ack();
        assert(strstr(event, "CMD_NACK: FDIR_ACK_FAULT unavailable") != NULL);
        warning(SRC_CDH); assert(SYSTEM_MODE_Get() == MODE_INITIAL_BOOT && handled_logs == 0);
    } else if (is("allocation_failure")) {
        fail_allocation = true; assert(!FDIR_Init()); FDIR_Process();
        assert(FDIR_HasActiveFaults() && handled_logs == 0);
    } else {
        assert(FDIR_Init());
        if (matrix()) { /* Assertions are in the matrix. */ }
        else if (is("repeat_init")) {
            post_deployment(MODE_NOMINAL); warnings(SRC_CDH, 3);
            assert(!FDIR_Init() && allocations == 2); FDIR_Process();
            assert(SYSTEM_MODE_Get() == MODE_DEGRADED && handled_logs == 1);
        } else if (is("mode_uninitialized")) {
            warnings(SRC_CDH, 3); FDIR_Process();
            assert(FDIR_HasActiveFaults() && SYSTEM_MODE_Get() == MODE_SAFE && handled_logs == 0);
        } else if (strncmp(scenario, "task_", 5) == 0) {
            if (!is("task_boot_deployment")) post_deployment(is("task_safe") || is("task_ack") ? MODE_SAFE : MODE_NOMINAL);
            warnings(SRC_CDH, is("task_nominal") ? 2 : 3);
            if (is("task_mode_retry")) mode_failures = 2;
            if (is("task_counter_retry")) process_fault_failures = 1;
            run_task();
            if (is("task_safe")) assert(SYSTEM_MODE_Get() == MODE_SAFE && FDIR_HasActiveFaults() && handled_logs == 0);
            else if (is("task_ack")) assert(SYSTEM_MODE_Get() == MODE_NOMINAL && !FDIR_HasActiveFaults() && handled_logs == 0);
            else assert(SYSTEM_MODE_Get() == MODE_DEGRADED && !FDIR_HasActiveFaults() && handled_logs == 1);
        } else if (is("pre_deployment_ground")) {
            warnings(SRC_CDH, 3);
            ground_mode(MODE_NOMINAL, false); ground_mode(MODE_DEGRADED, false);
            FDIR_Process();
            assert(SYSTEM_MODE_Get() == MODE_INITIAL_BOOT && !SYSTEM_MODE_IsDeploymentComplete());
            assert(FDIR_HasActiveFaults() && handled_logs == 0);
            assert(SYSTEM_MODE_CompleteDeployment()); FDIR_Process();
            assert(SYSTEM_MODE_Get() == MODE_DEGRADED && !FDIR_HasActiveFaults() && handled_logs == 1);
        } else if (is("quiet_contract")) {
            size_t before = log_length;
            assert(!SYSTEM_MODE_TransitionQuiet(MODE_INITIAL_BOOT, MODE_DEGRADED));
            assert(!SYSTEM_MODE_TransitionQuiet((SystemMode_t)-1, MODE_SAFE));
            assert(!SYSTEM_MODE_TransitionQuiet(MODE_INITIAL_BOOT, (SystemMode_t)6));
            assert(!SYSTEM_MODE_TransitionQuiet(MODE_NOMINAL, MODE_SAFE));
            assert(SYSTEM_MODE_TransitionQuiet(MODE_INITIAL_BOOT, MODE_SAFE));
            assert(log_length == before && SYSTEM_MODE_Get() == MODE_SAFE);
            assert(SYSTEM_MODE_CompleteDeployment()); before = log_length;
            assert(SYSTEM_MODE_TransitionQuiet(MODE_SAFE, MODE_NOMINAL));
            assert(log_length == before && SYSTEM_MODE_Get() == MODE_NOMINAL);
        } else {
            post_deployment(MODE_NOMINAL);
            if (is("invalid_reports")) {
                FDIR_ReportFault((FaultReport_t){.source = (FaultSource_t)-1, .severity = FAULT_FATAL});
                FDIR_ReportFault((FaultReport_t){.source = SRC_COUNT, .severity = FAULT_WARNING});
                FDIR_ReportFault((FaultReport_t){.source = SRC_CDH, .severity = (FaultSeverity_t)-1});
                FDIR_ReportFault((FaultReport_t){.source = SRC_CDH, .severity = (FaultSeverity_t)4});
                FDIR_Process(); assert(!FDIR_HasActiveFaults() && SYSTEM_MODE_Get() == MODE_NOMINAL && handled_logs == 0);
            } else if (is("info_not_warning") || is("non_eps_critical")) {
                for (unsigned i = 0; i < 4; ++i) FDIR_ReportFault((FaultReport_t){.source = SRC_ARCHIVE,
                    .severity = is("info_not_warning") ? FAULT_INFO : FAULT_CRITICAL});
                FDIR_Process();
                assert(FDIR_HasActiveFaults() == is("non_eps_critical"));
                assert(SYSTEM_MODE_Get() == (is("non_eps_critical") ? MODE_SAFE : MODE_NOMINAL));
            } else if (is("empty_commands")) {
                FDIR_ProcessCommand(NULL, 1); FDIR_ProcessCommand(NULL, 0);
                const uint8_t command[] = {FDIR_CMD_ACK_FAULT};
                FDIR_ProcessCommand(command, 0); assert(!FDIR_HasActiveFaults() && SYSTEM_MODE_Get() == MODE_NOMINAL);
            } else if (is("multiple_sources") || is("partial_sources")) {
                for (unsigned source = 0; source < SRC_COUNT; ++source) warnings((FaultSource_t)source,
                    is("multiple_sources") || source == SRC_CDH ? 3 : 2);
                FDIR_Process(); assert(SYSTEM_MODE_Get() == MODE_DEGRADED);
                assert(handled_logs == (is("multiple_sources") ? SRC_COUNT : 1));
                assert(FDIR_HasActiveFaults() == is("partial_sources"));
                if (is("partial_sources")) {
                    for (unsigned source = 0; source < SRC_COUNT; ++source) if (source != SRC_CDH) warning((FaultSource_t)source);
                    FDIR_Process(); assert(handled_logs == SRC_COUNT && !FDIR_HasActiveFaults());
                }
            } else if (is("repeated_batches")) {
                for (unsigned i = 0; i < 3; ++i) {
                    warnings(SRC_CDH, 3); FDIR_Process();
                    assert(!FDIR_HasActiveFaults() && handled_logs == i + 1);
                }
                assert(SYSTEM_MODE_Get() == MODE_DEGRADED);
            } else if (is("saturation")) {
                warnings(SRC_CDH, 100); FDIR_Process();
                assert(!FDIR_HasActiveFaults() && handled_logs == 1);
                warning(SRC_CDH); FDIR_Process(); assert(FDIR_HasActiveFaults() && handled_logs == 1);
            } else {
                warnings(SRC_CDH, 3);
                if (is("mode_lock_retry") || is("persistent_mode_lock")) {
                    mode_failures = is("persistent_mode_lock") ? 6 : 2;
                    FDIR_Process(); assert(SYSTEM_MODE_Get() == MODE_NOMINAL && FDIR_HasActiveFaults() && handled_logs == 0);
                    if (is("persistent_mode_lock")) {
                        FDIR_Process(); FDIR_Process();
                        assert(FDIR_HasActiveFaults() && handled_logs == 0 && mode_attempts == 6);
                    }
                    FDIR_Process(); assert(SYSTEM_MODE_Get() == MODE_DEGRADED && !FDIR_HasActiveFaults() && handled_logs == 1);
                } else if (is("counter_lock_retry")) {
                    process_fault_failures = 1; FDIR_Process();
                    assert(SYSTEM_MODE_Get() == MODE_NOMINAL && FDIR_HasActiveFaults() && mode_attempts == 0);
                    FDIR_Process(); assert(SYSTEM_MODE_Get() == MODE_DEGRADED && !FDIR_HasActiveFaults());
                } else if (is("ack_before_process") || is("ack_at_boundary")) {
                    if (is("ack_at_boundary")) ack_before_lock = true; else ack();
                    FDIR_Process(); assert(SYSTEM_MODE_Get() == MODE_NOMINAL && !FDIR_HasActiveFaults() && handled_logs == 0);
                    warnings(SRC_CDH, 2); FDIR_Process(); assert(SYSTEM_MODE_Get() == MODE_NOMINAL);
                    warning(SRC_CDH); FDIR_Process(); assert(SYSTEM_MODE_Get() == MODE_DEGRADED && handled_logs == 1);
                } else if (is("ack_unavailable")) {
                    next_fault_failures = 1; ack();
                    assert(strstr(event, "CMD_NACK: FDIR_ACK_FAULT unavailable") != NULL && FDIR_HasActiveFaults());
                    FDIR_Process(); assert(SYSTEM_MODE_Get() == MODE_DEGRADED && handled_logs == 1);
                } else if (is("ack_after_commit")) {
                    after_unlock = AFTER_ACK; FDIR_Process();
                    assert(SYSTEM_MODE_Get() == MODE_DEGRADED && !FDIR_HasActiveFaults() && handled_logs == 1);
                } else if (is("new_warning_after_commit")) {
                    after_unlock = AFTER_WARNING; FDIR_Process();
                    assert(SYSTEM_MODE_Get() == MODE_DEGRADED && FDIR_HasActiveFaults() && handled_logs == 1);
                    ground_mode(MODE_NOMINAL, true); FDIR_Process(); assert(SYSTEM_MODE_Get() == MODE_NOMINAL);
                    warnings(SRC_CDH, 2); FDIR_Process(); assert(SYSTEM_MODE_Get() == MODE_DEGRADED && handled_logs == 2);
                } else if (is("emergency_after_commit")) {
                    after_unlock = AFTER_EMERGENCY; FDIR_Process();
                    assert(SYSTEM_MODE_Get() == MODE_EMERGENCY && !FDIR_HasActiveFaults() && handled_logs == 1);
                } else if (is("safe_before_commit") || is("critical_before_commit") || is("emergency_before_commit")) {
                    SystemMode_t injected = is("safe_before_commit") ? MODE_SAFE : (is("critical_before_commit") ? MODE_CRITICAL : MODE_EMERGENCY);
                    mode_before_lock = injected; FDIR_Process();
                    assert(SYSTEM_MODE_Get() == injected && FDIR_HasActiveFaults() && handled_logs == 0);
                } else if (is("degraded_before_commit")) {
                    mode_before_lock = MODE_DEGRADED; FDIR_Process();
                    assert(SYSTEM_MODE_Get() == MODE_DEGRADED && !FDIR_HasActiveFaults() && handled_logs == 1);
                } else if (is("fatal_keeps_warning") || is("eps_critical_keeps_warning")) {
                    FDIR_ReportFault((FaultReport_t){.source = SRC_EPS,
                        .severity = is("fatal_keeps_warning") ? FAULT_FATAL : FAULT_CRITICAL});
                    FDIR_Process();
                    assert(SYSTEM_MODE_Get() == (is("fatal_keeps_warning") ? MODE_EMERGENCY : MODE_SAFE));
                    assert(FDIR_HasActiveFaults() && handled_logs == 0); ack();
                    assert(FDIR_HasActiveFaults()); // ACK leaves the active critical/fatal record intact.
                    assert(SYSTEM_MODE_Get() == (is("fatal_keeps_warning") ? MODE_EMERGENCY : MODE_SAFE));
                } else if (is("ack_does_not_recover")) {
                    ground_mode(MODE_SAFE, true); ack(); FDIR_Process();
                    assert(SYSTEM_MODE_Get() == MODE_SAFE && !FDIR_HasActiveFaults());
                } else if (is("query_lock_failure")) {
                    next_fault_failures = 1; assert(FDIR_HasActiveFaults());
                    FDIR_Process(); assert(SYSTEM_MODE_Get() == MODE_DEGRADED && !FDIR_HasActiveFaults());
                    next_fault_failures = 1; assert(FDIR_HasActiveFaults());
                    assert(!FDIR_HasActiveFaults());
                } else { assert(!"Unknown scenario"); }
            }
        }
    }
    assert(!mode_mutex.held && !fault_mutex.held && !processing);
    fprintf(stdout, "PASS: FDIR warning case %s; real guarded service/task and retained decisions.\n", scenario);
    return 0;
}

/* Real mode service, deployment task and FDIR ground handler. No target or network I/O. */
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "system_mode.h"
#include "fdir_service.h"
#include "commands.h"
#include "time_service.h"
#include "yamcs_interface.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

void vDeploymentTask(void *argument);
bool __real_SYSTEM_MODE_CompleteDeployment(void);

typedef struct { bool held; } MockMutex;
static MockMutex mode_mutex, fault_mutex;
static bool allocating_mode, fail_allocation, fail_next_mode_take;
static bool inside_completion, persistent_completion_failure;
static unsigned allocation_calls, completion_failures, completion_attempts;
static unsigned delays, deletes, retries, stop_after_retries;
static TickType_t delay_trace[12];
static bool inject_before_commit, inject_after_commit, fault_in_silence, fault_in_burn, fault_in_retry;
static jmp_buf task_exit;
static char logs[32768], last_event[128];
static size_t log_length;

int __wrap_printf(const char *format, ...) {
    assert(!mode_mutex.held && !fault_mutex.held);
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
    while ((position = strstr(position, message)) != NULL) { ++count; position += strlen(message); }
    return count;
}
static void reset_evidence(void) {
    logs[0] = last_event[0] = '\0';
    log_length = 0;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    ++allocation_calls;
    if (fail_allocation) return NULL;
    return allocating_mode ? &mode_mutex : &fault_mutex;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks) {
    assert(handle == &mode_mutex || handle == &fault_mutex);
    MockMutex *mutex = handle;
    assert(!mutex->held);
    assert(ticks == pdMS_TO_TICKS(10) || ticks == portMAX_DELAY);
    if (handle == &mode_mutex) {
        if (fail_next_mode_take) { fail_next_mode_take = false; return pdFALSE; }
        if (inside_completion && (completion_failures || persistent_completion_failure)) {
            if (completion_failures) --completion_failures;
            return pdFALSE;
        }
    }
    mutex->held = true;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
    MockMutex *mutex = handle;
    assert(mutex->held);
    mutex->held = false;
    return pdTRUE;
}
uint32_t TIME_GetSeconds(void) { return 42; }
bool YAMCS_SendEvent(const char *format, ...) {
    assert(!mode_mutex.held && !fault_mutex.held);
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(last_event, sizeof(last_event), format, arguments);
    va_end(arguments);
    return true;
}
static void report_fault(FaultSeverity_t severity) {
    FaultReport_t report = {.source = severity == FAULT_FATAL ? SRC_ADCS : SRC_EPS,
        .severity = severity, .fault_code = 0x1234, .timestamp = 42};
    FDIR_ReportFault(report);
}
bool __wrap_SYSTEM_MODE_CompleteDeployment(void) {
    ++completion_attempts;
    if (inject_before_commit) { inject_before_commit = false; report_fault(FAULT_FATAL); }
    inside_completion = true;
    bool completed = __real_SYSTEM_MODE_CompleteDeployment();
    inside_completion = false;
    if (completed && inject_after_commit) { inject_after_commit = false; report_fault(FAULT_FATAL); }
    return completed;
}
static bool operating(SystemMode_t mode) { return mode == MODE_NOMINAL || mode == MODE_DEGRADED; }
static void ground_set(SystemMode_t mode, bool accepted) {
    const uint8_t command[] = {FDIR_CMD_RESET_MODE, (uint8_t)mode};
    last_event[0] = '\0';
    FDIR_ProcessCommand(command, sizeof(command));
    assert(strstr(last_event, accepted ? "CMD_ACK: FDIR_SET_MODE=" : "CMD_NACK: FDIR_SET_MODE") != NULL);
}

void vTaskDelay(TickType_t ticks) {
    assert(!mode_mutex.held && !fault_mutex.held && !inside_completion);
    assert(delays < sizeof(delay_trace) / sizeof(delay_trace[0]));
    delay_trace[delays++] = ticks;
    assert(!SYSTEM_MODE_IsDeploymentComplete() && deletes == 0);
    if (delays == 1) {
        assert(ticks == pdMS_TO_TICKS(10000));
        assert(completion_attempts == 0);
        if (fault_in_silence) report_fault(FAULT_CRITICAL);
    } else if (delays == 2) {
        assert(ticks == pdMS_TO_TICKS(2000));
        assert(completion_attempts == 0);
        if (fault_in_burn) report_fault(FAULT_FATAL);
    } else {
        assert(ticks == pdMS_TO_TICKS(100));
        ++retries;
        assert(completion_attempts == retries);
        assert(occurrences("DEPLOY: Simulation completion recorded;") == 0);
        if (fault_in_retry) { fault_in_retry = false; assert(SYSTEM_MODE_Set(MODE_CRITICAL)); }
    }
    const SystemMode_t prior = SYSTEM_MODE_Get();
    ground_set(MODE_NOMINAL, false);
    ground_set(MODE_DEGRADED, false);
    assert(SYSTEM_MODE_Get() == prior);
    if (stop_after_retries && retries == stop_after_retries) longjmp(task_exit, 2);
}
void vTaskDelete(void *task) {
    assert(task == NULL && !mode_mutex.held && !fault_mutex.held);
    assert(SYSTEM_MODE_IsDeploymentComplete());
    assert(occurrences("DEPLOY: Simulation completion recorded;") == 1);
    ++deletes;
    longjmp(task_exit, 1);
}
static int run_task(void) {
    int result = setjmp(task_exit);
    if (result == 0) { vDeploymentTask(NULL); assert(!"deployment task returned unexpectedly"); }
    return result;
}

static bool matrix(const char *name) {
    static const char *names[] = {"boot", "safe", "nominal", "degraded", "emergency", "critical"};
    static const char *apis[] = {"set", "transition", "ground"};
    char candidate[96];
    for (unsigned after = 0; after < 2; ++after) {
        for (unsigned api = 0; api < 3; ++api) {
            for (unsigned from = 0; from < 6; ++from) {
                if (!after && operating((SystemMode_t)from)) continue;
                for (unsigned to = 0; to < 6; ++to) {
                    snprintf(candidate, sizeof(candidate), "%s_%s_%s_%s", after ? "after" : "before",
                             apis[api], names[from], names[to]);
                    if (strcmp(candidate, name) != 0) continue;
                    if (after) assert(SYSTEM_MODE_CompleteDeployment());
                    assert(SYSTEM_MODE_Set((SystemMode_t)from));
                    reset_evidence();
                    bool accepted = after || !operating((SystemMode_t)to);
                    if (api == 0) assert(SYSTEM_MODE_Set((SystemMode_t)to) == accepted);
                    else if (api == 1) assert(SYSTEM_MODE_Transition((SystemMode_t)from, (SystemMode_t)to) == accepted);
                    else ground_set((SystemMode_t)to, accepted);
                    assert(SYSTEM_MODE_Get() == (SystemMode_t)(accepted ? to : from));
                    assert(SYSTEM_MODE_IsDeploymentComplete() == (bool)after);
                    return true;
                }
            }
        }
    }
    for (unsigned state = 0; state < 6; ++state) {
        snprintf(candidate, sizeof(candidate), "complete_%s", names[state]);
        if (strcmp(candidate, name) != 0) continue;
        if (operating((SystemMode_t)state)) assert(SYSTEM_MODE_CompleteDeployment());
        assert(SYSTEM_MODE_Set((SystemMode_t)state));
        reset_evidence();
        assert(SYSTEM_MODE_CompleteDeployment());
        const SystemMode_t expected = state == MODE_INITIAL_BOOT ? MODE_NOMINAL : (SystemMode_t)state;
        assert(SYSTEM_MODE_IsDeploymentComplete() && SYSTEM_MODE_Get() == expected);
        assert(SYSTEM_MODE_CompleteDeployment() && SYSTEM_MODE_Get() == expected);
        assert(occurrences("Deployment simulation completion recorded.") == (unsigned)!operating((SystemMode_t)state));
        return true;
    }
    return false;
}

static void observer(SystemMode_t mode, void *context) {
    assert(mode_mutex.held);
    *(SystemMode_t *)context = mode;
}
static void special(const char *name) {
    if (strncmp(name, "task_", 5) == 0) {
        SystemMode_t expected = MODE_NOMINAL;
        if (strcmp(name, "task_safe") == 0) { assert(SYSTEM_MODE_Set(MODE_SAFE)); expected = MODE_SAFE; }
        else if (strcmp(name, "task_emergency") == 0) { assert(SYSTEM_MODE_Set(MODE_EMERGENCY)); expected = MODE_EMERGENCY; }
        else if (strcmp(name, "task_critical") == 0) { assert(SYSTEM_MODE_Set(MODE_CRITICAL)); expected = MODE_CRITICAL; }
        else if (strcmp(name, "task_fault_silence") == 0) { fault_in_silence = true; expected = MODE_SAFE; }
        else if (strcmp(name, "task_fatal_burn") == 0) { fault_in_burn = true; expected = MODE_EMERGENCY; }
        else if (strcmp(name, "task_retry") == 0) completion_failures = 2;
        else if (strcmp(name, "task_fault_retry") == 0) { completion_failures = 2; fault_in_retry = true; expected = MODE_CRITICAL; }
        else if (strcmp(name, "task_fault_before_commit") == 0) { inject_before_commit = true; expected = MODE_EMERGENCY; }
        else if (strcmp(name, "task_fault_after_commit") == 0) { inject_after_commit = true; expected = MODE_EMERGENCY; }
        else if (strcmp(name, "task_still_retrying") == 0) { persistent_completion_failure = true; stop_after_retries = 3; expected = MODE_INITIAL_BOOT; }
        else assert(strcmp(name, "task_boot") == 0);
        reset_evidence();
        int stopped = run_task();
        assert(stopped == (stop_after_retries ? 2 : 1));
        assert(deletes == (stop_after_retries ? 0U : 1U) && delays == 2 + retries);
        assert(delay_trace[0] == 10000 && delay_trace[1] == 2000);
        assert(SYSTEM_MODE_Get() == expected && SYSTEM_MODE_IsDeploymentComplete() == !stop_after_retries);
        assert(occurrences("Simulation starting safety wait") == 1);
        assert(occurrences("simulating deployment (no burn-wire output)") == 1);
        assert(occurrences("Completion record unavailable") == retries);
        if (!stop_after_retries) assert(completion_attempts == retries + 1);
    } else if (strcmp(name, "completion_lock_failure") == 0) {
        fail_next_mode_take = true; assert(!SYSTEM_MODE_CompleteDeployment());
        assert(SYSTEM_MODE_Get() == MODE_INITIAL_BOOT && !SYSTEM_MODE_IsDeploymentComplete());
        assert(strstr(logs, "completion recorded") == NULL);
        assert(SYSTEM_MODE_CompleteDeployment() && SYSTEM_MODE_IsDeploymentComplete());
    } else if (strcmp(name, "query_lock_failure") == 0) {
        assert(SYSTEM_MODE_CompleteDeployment()); fail_next_mode_take = true;
        assert(!SYSTEM_MODE_IsDeploymentComplete());
        assert(SYSTEM_MODE_IsDeploymentComplete() && SYSTEM_MODE_Get() == MODE_NOMINAL);
    } else if (strcmp(name, "idempotent_boot_return") == 0) {
        assert(SYSTEM_MODE_CompleteDeployment()); assert(SYSTEM_MODE_Set(MODE_INITIAL_BOOT)); reset_evidence();
        assert(SYSTEM_MODE_CompleteDeployment() && SYSTEM_MODE_Get() == MODE_INITIAL_BOOT);
        assert(SYSTEM_MODE_IsDeploymentComplete() && log_length == 0);
    } else if (strcmp(name, "repeat_init") == 0) {
        assert(SYSTEM_MODE_CompleteDeployment());
        assert(!SYSTEM_MODE_Init(MODE_INITIAL_BOOT));
        assert(SYSTEM_MODE_IsDeploymentComplete() && SYSTEM_MODE_Get() == MODE_NOMINAL);
    } else if (strcmp(name, "ack_not_deployment") == 0) {
        report_fault(FAULT_WARNING); assert(FDIR_HasActiveFaults());
        const uint8_t ack[] = {FDIR_CMD_ACK_FAULT}; FDIR_ProcessCommand(ack, sizeof(ack));
        assert(!FDIR_HasActiveFaults() && !SYSTEM_MODE_IsDeploymentComplete());
        ground_set(MODE_NOMINAL, false); assert(SYSTEM_MODE_Get() == MODE_INITIAL_BOOT);
    } else if (strcmp(name, "invalid_modes") == 0) {
        assert(!SYSTEM_MODE_Set((SystemMode_t)-1) && !SYSTEM_MODE_Set((SystemMode_t)6));
        assert(!SYSTEM_MODE_Transition((SystemMode_t)-1, MODE_SAFE));
        assert(!SYSTEM_MODE_Transition(MODE_INITIAL_BOOT, (SystemMode_t)6));
        assert(!SYSTEM_MODE_Transition(MODE_SAFE, MODE_EMERGENCY));
        const uint8_t bad[] = {FDIR_CMD_RESET_MODE, 255}; FDIR_ProcessCommand(bad, sizeof(bad));
        assert(strstr(last_event, "CMD_NACK") && SYSTEM_MODE_Get() == MODE_INITIAL_BOOT);
    } else if (strcmp(name, "mutation_lock_failure") == 0) {
        assert(SYSTEM_MODE_CompleteDeployment()); assert(SYSTEM_MODE_Set(MODE_SAFE));
        fail_next_mode_take = true; assert(!SYSTEM_MODE_Set(MODE_NOMINAL));
        fail_next_mode_take = true; assert(!SYSTEM_MODE_Transition(MODE_SAFE, MODE_DEGRADED));
        assert(SYSTEM_MODE_Get() == MODE_SAFE && SYSTEM_MODE_IsDeploymentComplete());
    } else if (strcmp(name, "ground_missing_mode") == 0) {
        const uint8_t missing[] = {FDIR_CMD_RESET_MODE}; FDIR_ProcessCommand(missing, sizeof(missing));
        assert(strstr(last_event, "CMD_NACK") && !SYSTEM_MODE_IsDeploymentComplete());
    } else if (strcmp(name, "with_current") == 0) {
        SystemMode_t observed = MODE_SAFE;
        assert(SYSTEM_MODE_WithCurrent(observer, &observed) && observed == MODE_INITIAL_BOOT);
        assert(SYSTEM_MODE_CompleteDeployment());
        assert(SYSTEM_MODE_WithCurrent(observer, &observed) && observed == MODE_NOMINAL);
    } else if (strcmp(name, "critical_fault_preserved") == 0 || strcmp(name, "fatal_fault_preserved") == 0) {
        bool fatal = strcmp(name, "fatal_fault_preserved") == 0;
        report_fault(fatal ? FAULT_FATAL : FAULT_CRITICAL);
        assert(SYSTEM_MODE_CompleteDeployment());
        assert(SYSTEM_MODE_Get() == (fatal ? MODE_EMERGENCY : MODE_SAFE));
        // Completion is not a fault-clear/recovery authorization.
        assert(SYSTEM_MODE_IsDeploymentComplete());
    } else assert(!"unknown scenario");
}

int main(int argc, char **argv) {
    assert(argc == 2);
    allocating_mode = true;
    if (strcmp(argv[1], "uninitialized") == 0) {
        assert(!SYSTEM_MODE_CompleteDeployment() && !SYSTEM_MODE_IsDeploymentComplete());
        assert(!SYSTEM_MODE_Set(MODE_NOMINAL) && !SYSTEM_MODE_Transition(MODE_SAFE, MODE_DEGRADED));
    } else if (strncmp(argv[1], "init_reject_", 12) == 0) {
        SystemMode_t mode = strcmp(argv[1], "init_reject_nominal") == 0 ? MODE_NOMINAL : MODE_DEGRADED;
        assert(!SYSTEM_MODE_Init(mode) && allocation_calls == 0);
        assert(SYSTEM_MODE_Init(MODE_INITIAL_BOOT) && !SYSTEM_MODE_IsDeploymentComplete());
    } else if (strcmp(argv[1], "init_allocation_failure") == 0) {
        fail_allocation = true; assert(!SYSTEM_MODE_Init(MODE_INITIAL_BOOT));
        assert(!SYSTEM_MODE_CompleteDeployment() && !SYSTEM_MODE_IsDeploymentComplete());
        fail_allocation = false; assert(SYSTEM_MODE_Init(MODE_INITIAL_BOOT));
    } else {
        assert(SYSTEM_MODE_Init(MODE_INITIAL_BOOT)); allocating_mode = false; assert(FDIR_Init());
        reset_evidence(); if (!matrix(argv[1])) special(argv[1]);
    }
    assert(!mode_mutex.held && !fault_mutex.held && !inside_completion);
    fprintf(stdout, "PASS: deployment gate %s.\n", argv[1]);
    return 0;
}

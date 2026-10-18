/* Real global hook; mock scheduling yields instead of monopolizing host CPUs. */
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "watchdog_test.h"
#include "freertos/task.h"
#include "esp_system.h"

static const char *scenario;
static unsigned creations, aborts, flushes, supervisor_delays, selected_core, staged;
static bool in_worker;
static TaskFunction_t workers[2];
static void *arguments[2];
static jmp_buf aborted, worker_exit;
static char output[32768];
static size_t used;
static int is(const char *name) { return strcmp(scenario, name) == 0; }
int __wrap_printf(const char *format, ...) {
    va_list args; va_start(args, format);
    int n = vsnprintf(output + used, sizeof(output) - used, format, args); va_end(args);
    assert(n >= 0 && (size_t)n < sizeof(output) - used); used += (size_t)n; return n;
}
void __wrap_abort(void) { ++aborts; longjmp(aborted, 1); }
int __wrap_fflush(FILE *stream) {
    assert(stream == stdout && creations == 2 && staged == 3);
    assert(strstr(output, "injected cores=3 ready=3 priority=24 interrupts=enabled"));
    ++flushes; return is("flush_failure") ? -1 : 0;
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { assert(task == NULL); return 1024; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t task, const char *name, uint32_t stack,
                                  void *argument, UBaseType_t priority, TaskHandle_t *handle, BaseType_t core) {
    assert(creations < 2 && core == (BaseType_t)creations && (uintptr_t)argument == creations);
    assert(strcmp(name, core ? "WDT_G1" : "WDT_G0") == 0 && stack == 4096 && priority == 24 && handle == NULL);
    assert(strstr(output, "supervisor check=6 feed=ok") && flushes == 0);
    workers[core] = task; arguments[core] = argument; ++creations;
    return (is("first_create_failure") && creations == 1) || (is("second_create_failure") && creations == 2) ? 0 : pdPASS;
}
BaseType_t xPortGetCoreID(void) { return (BaseType_t)(is("affinity_failure") ? 1 - selected_core : selected_core); }
static int run_worker(unsigned core) {
    selected_core = core; in_worker = true;
    int result = setjmp(worker_exit);
    if (result == 0) workers[core](arguments[core]);
    in_worker = false; return result;
}
void vTaskDelay(TickType_t ticks) {
    assert(ticks == 1);
    if (in_worker) longjmp(worker_exit, 1); // Gate still closed: no busy-loop yet.
    ++supervisor_delays;
    assert(creations == 2 && flushes == 0 && supervisor_delays <= 100);
    if (is("ready_timeout")) return;
    for (unsigned core = 0; core < (is("partial_ready") ? 1U : 2U); ++core) {
        if (!(staged & (1U << core))) {
            assert(run_worker(core) == 1);
            staged |= 1U << core;
        }
    }
}
void TEST_DisableInterrupts(void) { assert(!"Global profile must keep interrupts enabled"); }
void TEST_Nop(void) {
    assert(in_worker && creations == 2 && staged == 3 && flushes == 1);
    assert(strstr(output, "injected cores=3 ready=3 priority=24 interrupts=enabled"));
    longjmp(worker_exit, 2);
}
int main(int argc, char **argv) {
    assert(argc == 2); scenario = argv[1];
    if (is("uninitialized")) {
        WATCHDOG_TEST_AfterCheck(); assert(creations == 0 && strstr(output, "not initialized"));
    } else {
        WATCHDOG_TEST_InitRecovery(is("recovery_suppressed") ? ESP_RST_TASK_WDT : is("wrong_reason_armed") ? ESP_RST_INT_WDT : 1);
        if (is("repeat_init")) {
            if (setjmp(aborted) == 0) WATCHDOG_TEST_InitRecovery(1);
            assert(aborts == 1 && strstr(output, "initialized twice"));
        } else if (is("initialized")) {
            assert(strstr(output, "profile=global_starvation injection=armed reset_reason=1 cores=3"));
        } else if (is("all_pets_preserved")) {
            for (unsigned i = 0; i < WDT_TASK_COUNT; ++i) assert(!WATCHDOG_TEST_DropPet((WatchdogTaskID_t)i));
        } else if (is("recovery_suppressed")) {
            for (unsigned i = 0; i < 20; ++i) WATCHDOG_TEST_AfterCheck();
            assert(creations == 0 && flushes == 0 && strstr(output, "injection=suppressed"));
        } else {
            for (unsigned i = 0; i < 5; ++i) WATCHDOG_TEST_AfterCheck();
            assert(creations == 0);
            if (setjmp(aborted) == 0) WATCHDOG_TEST_AfterCheck();
            if (is("first_create_failure") || is("second_create_failure")) {
                assert(aborts == 1 && flushes == 0 && strstr(output, "creation failed"));
                assert(creations == (is("first_create_failure") ? 1U : 2U));
            } else if (is("ready_timeout") || is("partial_ready")) {
                assert(aborts == 1 && flushes == 0 && supervisor_delays == 100 && strstr(output, "not ready"));
                assert(staged == (is("partial_ready") ? 1U : 0U));
            } else if (is("affinity_failure")) {
                assert(aborts == 1 && flushes == 0 && strstr(output, "affinity mismatch"));
            } else if (is("flush_failure")) {
                assert(aborts == 1 && flushes == 1 && run_worker(0) == 1 && run_worker(1) == 1);
            } else {
                assert(aborts == 0 && creations == 2 && staged == 3 && flushes == 1);
                if (is("worker0_order")) assert(run_worker(0) == 2);
                if (is("worker1_order")) assert(run_worker(1) == 2);
                for (unsigned i = 0; i < 10; ++i) WATCHDOG_TEST_AfterCheck();
                assert(creations == 2); // No second worker pair in the same boot.
            }
        }
    }
    puts("PASS: gated dual-core global hook; mocked scheduling/IRQ/fence, no target execution.");
    return 0;
}

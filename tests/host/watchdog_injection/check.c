/* Real test hooks; SDK/RTOS/ledger boundary mocked, no target suspension/reset. */
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "watchdog_test.h"
#include "watchdog.h"
#include "system_mode.h"
#include "fdir_service.h"
#include "freertos/task.h"

static const char *scenario;
static char text[8192];
static size_t used;
static unsigned suspensions, snapshots, aborts, stack_reads;
static bool snapshot_failure, return_from_suspend;
static jmp_buf stopped;
static FDIR_ActiveFaultSnapshot_t fixture;
static bool is(const char *s) { return strcmp(scenario, s) == 0; }
int __wrap_printf(const char *format, ...) {
    va_list args; va_start(args, format);
    int n = vsnprintf(text + used, sizeof(text) - used, format, args); va_end(args);
    assert(n >= 0 && (size_t)n < sizeof(text) - used); used += (size_t)n; return n;
}
void __wrap_abort(void) { ++aborts; longjmp(stopped, 1); }
void vTaskSuspend(TaskHandle_t task) {
    assert(task == NULL); ++suspensions;
    if (!return_from_suspend) longjmp(stopped, 1);
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) {
    assert(task == NULL); ++stack_reads; return 1024;
}
SystemMode_t SYSTEM_MODE_Get(void) { return MODE_SAFE; }
bool FDIR_GetActiveFaults(FDIR_ActiveFaultSnapshot_t *out) {
    assert(out != NULL); ++snapshots;
    if (snapshot_failure) return false;
    *out = fixture; return true;
}

int main(int argc, char **argv) {
    assert(argc == 2); scenario = argv[1];
    if (is("uninitialized")) {
        assert(!WATCHDOG_TEST_DropPet(WDT_TASK_TM_GEN));
        WATCHDOG_TEST_AfterCheck(); assert(strstr(text, "TEST ERROR:") && snapshots == 0 && stack_reads == 0);
    } else {
        WATCHDOG_TEST_InitRecovery(is("recovery_suppressed") ? 6U : (is("wrong_reason_armed") ? 4U : 1U));
        if (is("repeat_init")) {
            if (setjmp(stopped) == 0) { WATCHDOG_TEST_InitRecovery(1); assert(!"repeat init returned"); }
            assert(aborts == 1 && strstr(text, "initialized twice"));
        } else if (is("initialized")) {
            assert(strstr(text, "TEST: WDT profile="));
#ifdef FSW_TEST_WDT_TASK_LOSS
        } else if (is("drop_target") || is("drop_once")) {
            assert(WATCHDOG_TEST_DropPet(WDT_TASK_TM_GEN));
            if (is("drop_once")) {
                for (unsigned i = 0; i < 20; ++i) assert(WATCHDOG_TEST_DropPet(WDT_TASK_TM_GEN));
                const char *mark = strstr(text, "heartbeat loss injected");
                assert(mark && strstr(mark + 1, "heartbeat loss injected") == NULL);
            }
        } else if (is("other_roles")) {
            for (unsigned i = 1; i < WDT_TASK_COUNT; ++i) assert(!WATCHDOG_TEST_DropPet((WatchdogTaskID_t)i));
            assert(!WATCHDOG_TEST_DropPet((WatchdogTaskID_t)-1) && !WATCHDOG_TEST_DropPet(WDT_TASK_COUNT));
        } else if (is("active_snapshot") || is("empty_snapshot") || is("snapshot_failure") ||
                   is("fatal_not_match") || is("wrong_key_no_match")) {
            fixture.count = is("empty_snapshot") ? 0 : 1;
            fixture.records[0] = (SystemFaultRecord_t){.source = SRC_CDH, .code = FAULT_WDT_TIMEOUT_BASE,
                                                     .reports = 1, .revision = 42};
            fixture.records[0].fatal = is("fatal_not_match");
            if (is("wrong_key_no_match")) ++fixture.records[0].code;
            snapshot_failure = is("snapshot_failure");
            WATCHDOG_TEST_AfterCheck();
            assert(stack_reads == 1 && strstr(text, "stack_free_min_bytes=1024"));
            assert(snapshots == 1 && fixture.records[0].revision == 42);
            if (snapshot_failure) assert(strstr(text, "TEST ERROR:") && strstr(text, "feed=ok") == NULL);
            else assert(strstr(text, is("active_snapshot") ? "mode=1 active=1 reports=1" : "active=0 reports=0"));
#else
        } else if (is("all_pets_preserved")) {
            for (unsigned i = 0; i < WDT_TASK_COUNT; ++i) assert(!WATCHDOG_TEST_DropPet((WatchdogTaskID_t)i));
        } else if (is("recovery_suppressed")) {
            for (unsigned i = 0; i < 30; ++i) WATCHDOG_TEST_AfterCheck();
            assert(stack_reads == 30);
            assert(suspensions == 0 && strstr(text, "injection=suppressed reset_reason=6"));
        } else if (is("sixth_check") || is("wrong_reason_armed") || is("unexpected_resume")) {
            for (unsigned i = 0; i < 5; ++i) WATCHDOG_TEST_AfterCheck();
            assert(suspensions == 0);
            return_from_suspend = is("unexpected_resume");
            if (setjmp(stopped) == 0) WATCHDOG_TEST_AfterCheck();
            assert(suspensions == 1 && strstr(text, "after 6 completed checks"));
            assert(stack_reads == 6);
            if (return_from_suspend) assert(strstr(text, "resumed unexpectedly"));
            for (unsigned i = 0; i < 12; ++i) WATCHDOG_TEST_AfterCheck();
            assert(suspensions == 1); // No second injection in the same process.
#endif
        } else assert(!"Unknown injection test");
    }
    fprintf(stdout, "PASS: watchdog test-only hook %s; no hardware changes.\n", scenario);
    return 0;
}

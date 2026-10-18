/* Real recovery injection hook; no SDK, task suspension, command or fault clear. */
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

static char output[16384];
static size_t used;
static unsigned snapshots, stacks, aborts;
static bool fail_snapshot;
static SystemMode_t mode = MODE_SAFE;
static FDIR_ActiveFaultSnapshot_t ledger;
static jmp_buf stopped;
int __wrap_printf(const char *format, ...) {
    va_list args; va_start(args, format);
    const int count = vsnprintf(output + used, sizeof(output) - used, format, args);
    va_end(args);
    assert(count >= 0 && (size_t)count < sizeof(output) - used);
    used += (size_t)count; return count;
}
void __wrap_abort(void) { ++aborts; longjmp(stopped, 1); }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { assert(task == NULL); ++stacks; return 1234; }
SystemMode_t SYSTEM_MODE_Get(void) { return mode; }
bool FDIR_GetActiveFaults(FDIR_ActiveFaultSnapshot_t *out) {
    ++snapshots; if (fail_snapshot) return false; *out = ledger; return true;
}
static void active(uint32_t revision) {
    ledger = (FDIR_ActiveFaultSnapshot_t){.count=1};
    ledger.records[0] = (SystemFaultRecord_t){.source=SRC_CDH, .code=FAULT_WDT_TIMEOUT_BASE,
        .revision=revision, .reports=1};
    mode = MODE_SAFE;
}
static void ready1(void) {
    active(11); WATCHDOG_TEST_AfterCheck();
    assert(!WATCHDOG_TEST_DropPet(WDT_TASK_TM_GEN));
    ledger.count = 0; WATCHDOG_TEST_AfterCheck();
    assert(strstr(output, "ready_ground=1"));
}
static void second_drop(void) {
    ready1(); mode = MODE_NOMINAL; WATCHDOG_TEST_AfterCheck();
    assert(WATCHDOG_TEST_DropPet(WDT_TASK_TM_GEN));
    assert(strstr(output, "operating=1 mode=2"));
}
int main(int argc, char **argv) {
    assert(argc == 2); const char *scenario = argv[1];
    if (strcmp(scenario, "uninitialized") == 0) {
        assert(!WATCHDOG_TEST_DropPet(WDT_TASK_TM_GEN));
        WATCHDOG_TEST_AfterCheck(); assert(strstr(output, "TEST ERROR:") && snapshots == 0 && stacks == 0);
    } else {
        WATCHDOG_TEST_InitRecovery(1);
        if (strcmp(scenario, "repeat_init") == 0) {
            if (setjmp(stopped) == 0) { WATCHDOG_TEST_InitRecovery(1); assert(!"repeat returned"); }
            assert(aborts == 1);
        } else if (strcmp(scenario, "initialized") == 0) {
            assert(strstr(output, "profile=task_recovery target=0"));
        } else if (strcmp(scenario, "drop_first_once") == 0) {
            for (unsigned i = 0; i < 10; ++i) assert(WATCHDOG_TEST_DropPet(WDT_TASK_TM_GEN));
            const char *mark = strstr(output, "drop=1 target=0");
            assert(mark && !strstr(mark + 1, "drop=1 target=0"));
        } else if (strcmp(scenario, "other_roles") == 0) {
            for (unsigned i = 1; i < WDT_TASK_COUNT; ++i) assert(!WATCHDOG_TEST_DropPet((WatchdogTaskID_t)i));
            assert(!WATCHDOG_TEST_DropPet((WatchdogTaskID_t)-1) && !WATCHDOG_TEST_DropPet(WDT_TASK_COUNT));
        } else if (strcmp(scenario, "first_resume") == 0) {
            active(11); WATCHDOG_TEST_AfterCheck();
            assert(!WATCHDOG_TEST_DropPet(WDT_TASK_TM_GEN) && strstr(output, "resume=1"));
            assert(ledger.count == 1 && ledger.records[0].revision == 11);
        } else if (strcmp(scenario, "first_wait_ground") == 0) {
            ready1(); for (unsigned i = 0; i < 8; ++i) WATCHDOG_TEST_AfterCheck();
            assert(mode == MODE_SAFE && !strstr(output, "operating=1"));
        } else if (strcmp(scenario, "second_dropout") == 0) {
            second_drop();
        } else if (strcmp(scenario, "second_resume") == 0 || strcmp(scenario, "complete") == 0 ||
                   strcmp(scenario, "complete_no_reinject") == 0) {
            second_drop(); active(12); WATCHDOG_TEST_AfterCheck();
            assert(!WATCHDOG_TEST_DropPet(WDT_TASK_TM_GEN) && strstr(output, "resume=2"));
            assert(ledger.records[0].revision == 12);
            if (strcmp(scenario, "second_resume") != 0) {
                ledger.count = 0; WATCHDOG_TEST_AfterCheck(); assert(strstr(output, "ready_ground=2"));
                mode = MODE_DEGRADED; WATCHDOG_TEST_AfterCheck();
                assert(strstr(output, "operating=2 mode=3") && strstr(output, "stage=6"));
                for (unsigned i = 0; i < 8; ++i) {
                    assert(!WATCHDOG_TEST_DropPet(WDT_TASK_TM_GEN)); WATCHDOG_TEST_AfterCheck();
                }
            }
        } else if (strcmp(scenario, "wrong_clear_mode") == 0) {
            active(11); WATCHDOG_TEST_AfterCheck(); ledger.count = 0;
            mode = MODE_NOMINAL; WATCHDOG_TEST_AfterCheck();
            assert(strstr(output, "changed mode without ground") && !strstr(output, "ready_ground=1"));
        } else {
            active(11);
            if (strcmp(scenario, "snapshot_failure") == 0) fail_snapshot = true;
            else if (strcmp(scenario, "other_cause") == 0) ledger.records[0].source = SRC_ARCHIVE;
            else if (strcmp(scenario, "fatal") == 0) ledger.records[0].fatal = true;
            else if (strcmp(scenario, "uncertain") == 0) ledger.recording_uncertain = true;
            else if (strcmp(scenario, "overflow") == 0) ledger.overflow = true;
            else assert(!"Unknown recovery hook case");
            WATCHDOG_TEST_AfterCheck();
            assert(strstr(output, "TEST ERROR:") && !strstr(output, "resume=1"));
        }
    }
    if (stacks) assert(strstr(output, "stack_free_min_bytes=1234"));
    fprintf(stdout, "PASS: recovery hook %s; no mode command, clear, reset, or helper feed.\n", scenario);
    return 0;
}

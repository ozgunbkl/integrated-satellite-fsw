/* Actual injection; host compiler-fence probe exits instead of spinning forever. */
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "watchdog_test.h"
#include "freertos/task.h"
#include "esp_system.h"

static const char *scenario;
static unsigned creations, aborts, flushes, masks;
static TaskFunction_t worker;
static jmp_buf stopped;
static char text[32768];
static size_t used;
static int is(const char *name) { return strcmp(scenario, name) == 0; }
int __wrap_printf(const char *format, ...) {
    va_list args; va_start(args, format);
    int n = vsnprintf(text + used, sizeof(text) - used, format, args); va_end(args);
    assert(n >= 0 && (size_t)n < sizeof(text) - used); used += (size_t)n; return n;
}
void __wrap_abort(void) { ++aborts; longjmp(stopped, 1); }
int __wrap_fflush(FILE *stream) { assert(stream == stdout && masks == 0); ++flushes; return is("flush_failure") ? -1 : 0; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { assert(task == NULL); return 1024; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t task, const char *name, uint32_t stack,
                                  void *argument, UBaseType_t priority, TaskHandle_t *handle, BaseType_t core) {
    assert(strcmp(name, "WDT_FAULT") == 0 && stack == 4096 && argument == NULL && priority == 1 && handle == NULL && core == FSW_TEST_WDT_CORE);
    assert(strstr(text, "supervisor check=6 feed=ok"));
    ++creations; worker = task; return is("create_failure") ? 0 : pdPASS;
}
void TEST_DisableInterrupts(void) { assert(flushes == 1); ++masks; }
void TEST_Nop(void) {
    assert(flushes == 1);
    char expected[96];
#ifdef FSW_TEST_WDT_IDLE_STARVATION
    snprintf(expected, sizeof(expected), "idle starvation injected core=%u interrupts=enabled", (unsigned)FSW_TEST_WDT_CORE);
    assert(masks == 0 && strstr(text, expected));
#else
    snprintf(expected, sizeof(expected), "interrupt masking injected core=%u timeout_ms=300", (unsigned)FSW_TEST_WDT_CORE);
    assert(masks == 1 && strstr(text, expected));
#endif
    longjmp(stopped, 1);
}
int main(int argc, char **argv) {
    assert(argc == 2); scenario = argv[1];
#ifdef FSW_TEST_WDT_IDLE_STARVATION
    const unsigned expected_reason = ESP_RST_TASK_WDT, wrong_reason = ESP_RST_INT_WDT;
#else
    const unsigned expected_reason = ESP_RST_INT_WDT, wrong_reason = ESP_RST_TASK_WDT;
#endif
    if (is("uninitialized")) {
        assert(!WATCHDOG_TEST_DropPet(WDT_TASK_TM_GEN)); WATCHDOG_TEST_AfterCheck();
        assert(creations == 0 && strstr(text, "not initialized")); return 0;
    }
    WATCHDOG_TEST_InitRecovery(is("recovery_suppressed") ? expected_reason : is("wrong_reason_armed") ? wrong_reason : 1);
    if (is("repeat_init")) {
        if (setjmp(stopped) == 0) WATCHDOG_TEST_InitRecovery(1);
        assert(aborts == 1 && strstr(text, "initialized twice"));
    } else if (is("initialized")) {
        char expected[96];
        snprintf(expected, sizeof(expected), "injection=armed reset_reason=1 core=%u", (unsigned)FSW_TEST_WDT_CORE);
        assert(strstr(text, expected));
    } else if (is("all_pets_preserved")) {
        for (unsigned i = 0; i < WDT_TASK_COUNT; ++i) assert(!WATCHDOG_TEST_DropPet((WatchdogTaskID_t)i));
    } else if (is("recovery_suppressed")) {
        for (unsigned i = 0; i < 20; ++i) WATCHDOG_TEST_AfterCheck();
        assert(creations == 0 && strstr(text, "injection=suppressed"));
    } else {
        for (unsigned i = 0; i < 5; ++i) WATCHDOG_TEST_AfterCheck();
        assert(creations == 0);
        if (setjmp(stopped) == 0) WATCHDOG_TEST_AfterCheck();
        assert(creations == 1 && worker != NULL);
        if (is("create_failure")) assert(aborts == 1 && strstr(text, "creation failed"));
        else {
            assert(aborts == 0);
            for (unsigned i = 0; i < 10; ++i) WATCHDOG_TEST_AfterCheck();
            assert(creations == 1); // No reinjection within the boot.
            if (is("worker_order") || is("flush_failure")) {
                if (setjmp(stopped) == 0) worker(NULL);
                assert(flushes == 1 && aborts == (unsigned)is("flush_failure"));
                if (is("flush_failure")) assert(masks == 0);
            }
        }
    }
    puts("PASS: test-only starvation hook; mocked core/IRQ/spin, no target reset.");
    return 0;
}

// Real test wrappers; no real scheduler, allocation failure, panic or reset.
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "esp_system.h"
#include "startup_fault_test.h"

extern bool __wrap_HK_Init(void);
#if FSW_TEST_STARTUP_FAILURE != 1
extern BaseType_t __wrap_xTaskCreatePinnedToCore(TaskFunction_t, const char *,
    configSTACK_DEPTH_TYPE, void *, UBaseType_t, TaskHandle_t *, BaseType_t);
#if FSW_TEST_STARTUP_FAILURE == 2
#define TARGET vCommandProcessorTask
#define TARGET_NAME "CMD_PROC"
#define TARGET_PRIORITY 5U
#else
#define TARGET vSolarHarvest_Task
#define TARGET_NAME "SOLAR_HARV"
#define TARGET_PRIORITY 1U
#endif
#endif

static esp_reset_reason_t reason = ESP_RST_POWERON;
static unsigned real_hk, real_create, aborts;
static BaseType_t real_result = pdPASS;
static bool hk_result = true;
static jmp_buf stopped;
static char text[4096];
static size_t used;
static int handle_slot;
#if FSW_TEST_STARTUP_FAILURE != 1
static int argument_slot;
static TaskHandle_t handle;
#endif
static TaskFunction_t last_function;
static const char *last_name;
static void *last_argument;
static configSTACK_DEPTH_TYPE last_stack;
static UBaseType_t last_priority;
static TaskHandle_t *last_handle;
static BaseType_t last_core;

void vCommandProcessorTask(void *arg) { assert(!"Task must not execute in this fixture"); }
void vSolarHarvest_Task(void *arg) { assert(!"Task must not execute in this fixture"); }
#if FSW_TEST_STARTUP_FAILURE != 1
static void other_task(void *arg) { assert(!"SDK/task bodies must not execute in this fixture"); }
#endif
esp_reset_reason_t esp_reset_reason(void) { return reason; }
bool __real_HK_Init(void) { ++real_hk; return hk_result; }
BaseType_t __real_xTaskCreatePinnedToCore(TaskFunction_t function, const char *name,
    configSTACK_DEPTH_TYPE stack, void *argument, UBaseType_t priority,
    TaskHandle_t *out_handle, BaseType_t core) {
    ++real_create;
    last_function = function; last_name = name; last_stack = stack;
    last_argument = argument; last_priority = priority; last_handle = out_handle; last_core = core;
    if (out_handle) *out_handle = &handle_slot;
    return real_result;
}
int __wrap_printf(const char *format, ...) {
    va_list args; va_start(args, format);
    int n = vsnprintf(text + used, sizeof(text) - used, format, args); va_end(args);
    assert(n >= 0 && (size_t)n < sizeof(text) - used); used += (size_t)n;
    return n;
}
int __wrap_fflush(FILE *stream) { assert(stream == stdout); return 0; }
void __wrap_abort(void) { ++aborts; longjmp(stopped, 1); }

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *scenario = argv[1];
    const bool recovery = strcmp(scenario, "panic_suppressed") == 0 || strcmp(scenario, "real_failure_preserved") == 0;
    if (recovery) reason = ESP_RST_PANIC;
    if (strcmp(scenario, "wrong_reset_armed") == 0) reason = ESP_RST_TASK_WDT;
    if (strcmp(scenario, "real_failure_preserved") == 0) hk_result = false;
#if FSW_TEST_STARTUP_FAILURE != 1
    if (strcmp(scenario, "before_init") == 0) {
        if (setjmp(stopped) == 0) {
            __wrap_xTaskCreatePinnedToCore(TARGET, TARGET_NAME, 4096, NULL, TARGET_PRIORITY, NULL, tskNO_AFFINITY);
            assert(!"Uninitialized injection returned");
        }
        assert(aborts == 1 && real_create == 0); return 0;
    }
#endif
    bool initialized = __wrap_HK_Init();
    assert(strstr(text, recovery ? "injection=suppressed reset_reason=4" : "injection=armed"));
#if FSW_TEST_STARTUP_FAILURE == 1
    assert(initialized == (recovery && hk_result));
    assert(real_hk == (unsigned)recovery);
    assert((strstr(text, "injected kind=hk_init") != NULL) == !recovery);
    if (strcmp(scenario, "repeat_init") == 0) {
        if (setjmp(stopped) == 0) { __wrap_HK_Init(); assert(!"Repeated HK init returned"); }
        assert(aborts == 1 && real_hk == 0);
    } else assert(strcmp(scenario, "armed") == 0 || recovery || strcmp(scenario, "wrong_reset_armed") == 0);
#else
    assert(initialized == hk_result && real_hk == 1);
    if (strcmp(scenario, "other_passthrough") == 0) {
        real_result = -7;
        assert(__wrap_xTaskCreatePinnedToCore(other_task, NULL, 123, &argument_slot, 9, &handle, 1) == -7);
        assert(last_function == other_task && last_name == NULL && last_stack == 123 && last_priority == 9);
        assert(last_argument == &argument_slot && last_handle == &handle && last_core == 1 && handle == &handle_slot);
        assert(real_create == 1 && strstr(text, "injected kind=task_create") == NULL);
    } else if (strcmp(scenario, "repeat_init") == 0) {
        if (setjmp(stopped) == 0) { __wrap_HK_Init(); assert(!"Repeated HK init returned"); }
        assert(aborts == 1 && real_hk == 1 && real_create == 0);
    } else {
        bool bad = strncmp(scenario, "bad_", 4) == 0;
        bool repeat = strcmp(scenario, "repeat_target") == 0;
        if (strcmp(scenario, "real_failure_preserved") == 0) real_result = -7;
        if (setjmp(stopped) == 0) {
            BaseType_t result = __wrap_xTaskCreatePinnedToCore(TARGET,
                strcmp(scenario, "bad_name") == 0 ? "OTHER" : strcmp(scenario, "bad_null_name") == 0 ? NULL : TARGET_NAME,
                strcmp(scenario, "bad_stack") == 0 ? 2048 : 4096,
                strcmp(scenario, "bad_argument") == 0 ? &argument_slot : NULL,
                strcmp(scenario, "bad_priority") == 0 ? 2 : TARGET_PRIORITY,
                strcmp(scenario, "bad_handle") == 0 ? &handle : NULL,
                strcmp(scenario, "bad_core") == 0 ? 0 : tskNO_AFFINITY);
            assert(!bad);
            assert(result == (recovery ? real_result : errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY));
            if (repeat) {
                __wrap_xTaskCreatePinnedToCore(TARGET, TARGET_NAME, 4096, NULL, TARGET_PRIORITY, NULL, tskNO_AFFINITY);
                assert(!"Repeated target create returned");
            }
        }
        assert(aborts == (unsigned)(bad || repeat));
        assert(real_create == (unsigned)(!bad && recovery));
        if (!bad && !repeat) {
            assert(strcmp(scenario, "armed") == 0 || strcmp(scenario, "wrong_reset_armed") == 0 || recovery);
            assert((strstr(text, "injected kind=task_create") != NULL) == !recovery);
        }
    }
#endif
    fprintf(stdout, "PASS: startup test wrapper profile=%u case=%s; no target execution.\n",
            (unsigned)FSW_TEST_STARTUP_FAILURE, scenario);
    return 0;
}

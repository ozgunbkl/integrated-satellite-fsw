#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "resource_monitor.h"
#include "watchdog.h"
#include "time_service.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

static const char *scenario;
static TaskFunction_t captured;
static TickType_t ticks;
static unsigned samples, creates, reports, stack_reads;
static jmp_buf done;
static char output[32768];
static size_t used;
static int tm_queue, tc_queue, task_marker;
static int is(const char *name) { return strcmp(scenario, name) == 0; }

BaseType_t xTaskCreate(TaskFunction_t task, const char *name, configSTACK_DEPTH_TYPE stack,
                       void *argument, UBaseType_t priority, TaskHandle_t *handle) {
    assert(strcmp(name, "RES_MON") == 0 && stack == 4096 && argument == NULL && priority == 1 && handle == NULL);
    ++creates; captured = task;
    return is("allocation_failure") && creates == 1 ? 0 : pdPASS;
}
TickType_t xTaskGetTickCount(void) { return ticks; }
void vTaskDelayUntil(TickType_t *wake, TickType_t period) {
    assert(period == pdMS_TO_TICKS(100)); *wake += period; ticks = *wake;
    if (++samples > 610) longjmp(done, 1);
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
    assert(queue == &tm_queue || queue == &tc_queue);
    return samples % 17 == 0 ? (queue == &tm_queue ? 3 : 2) : 0;
}
TaskHandle_t xTaskGetHandle(const char *name) {
    assert(strcmp(name, "DEPLOY") != 0 && strcmp(name, "CMD_INJECT") != 0);
    return is("missing") && strcmp(name, "YLOG_TX") == 0 ? NULL : &task_marker;
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) {
    assert(task == &task_marker); ++stack_reads; return 1000;
}
void heap_caps_get_info(multi_heap_info_t *info, uint32_t capabilities) {
    assert(capabilities == MALLOC_CAP_8BIT);
    *info = (multi_heap_info_t){ .total_free_bytes=150000, .minimum_free_bytes=140000, .largest_free_block=100000 };
}
uint64_t TIME_GetMilliseconds(void) { return (uint64_t)samples * 100; }
bool WATCHDOG_GetTimingSnapshot(WATCHDOG_TaskTiming_t out[WDT_TASK_COUNT]) {
    if (is("timing_unavailable")) return false;
    for (unsigned i = 0; i < WDT_TASK_COUNT; ++i) out[i] = (WATCHDOG_TaskTiming_t){.pet_count=30, .min_interval_ms=100, .max_interval_ms=110, .age_ms=10};
    return true;
}
int __wrap_fprintf(FILE *stream, const char *format, ...) {
    assert(stream == stdout);
    va_list args; va_start(args, format);
    int count = vsnprintf(output + used, sizeof(output) - used, format, args); va_end(args);
    assert(count >= 0 && (size_t)count < sizeof(output) - used); used += (size_t)count;
    if (strstr(format, "DIAG: end")) ++reports;
    return count;
}
int main(int argc, char **argv) {
    assert(argc == 2); scenario = argv[1];
    if (is("bad_queues")) {
        assert(!RESOURCE_MONITOR_Start(NULL, &tc_queue));
        assert(!RESOURCE_MONITOR_Start(&tm_queue, NULL)); assert(creates == 0);
    }
    if (is("allocation_failure")) assert(!RESOURCE_MONITOR_Start(&tm_queue, &tc_queue));
    assert(RESOURCE_MONITOR_Start(&tm_queue, &tc_queue));
    if (is("repeat_start")) assert(!RESOURCE_MONITOR_Start(&tc_queue, &tm_queue));
    ticks = is("tick_wrap") ? UINT32_MAX - 1000U : 0;
    if (setjmp(done) == 0) captured(NULL);
    assert(reports == 2 && stack_reads == (is("missing") ? 26U : 28U));
    assert(strstr(output, "heap free=150000 min=140000 largest=100000."));
    assert(strstr(output, "queue name=TM used=0 peak=3 capacity=10 sample_ms=100."));
    assert(strstr(output, "queue name=TC used=0 peak=2 capacity=5 sample_ms=100."));
    assert(strstr(output, "DIAG: end seq=2."));
    assert((strstr(output, "timing unavailable") != NULL) == is("timing_unavailable"));
    if (!is("timing_unavailable")) assert(strstr(output, "timing id=6 count=30 min_ms=100 max_ms=110 age_ms=10."));
    if (is("missing")) assert(strstr(output, "DIAG: missing name=YLOG_TX."));
    puts("PASS: resource monitor actual task; mocked RTOS/heap/timing, no board action.");
    return 0;
}

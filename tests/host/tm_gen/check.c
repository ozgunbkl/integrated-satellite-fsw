#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "freertos/queue.h"
#include "task_defs.h"
#include "satellite_types.h"
#include "system_mode.h"
#include "watchdog.h"
#include "hk_service.h"

static int queue_slot;
QueueHandle_t xTelemetryQueue = &queue_slot;
static jmp_buf task_exit;
static TickType_t current_ticks;
static SystemMode_t mode;
static bool switch_safe;
static unsigned iterations, limit, reports, pets, sends, mode_reads;

int __wrap_printf(const char *format, ...) { return 0; }
TickType_t xTaskGetTickCount(void) { return current_ticks; }
void HK_RunPeriodic(uint32_t now_ms) {
    assert(now_ms == (uint32_t)(current_ticks * portTICK_PERIOD_MS));
    ++reports;
}
SystemMode_t SYSTEM_MODE_Get(void) { ++mode_reads; return mode; }
void watchdog_pet(WatchdogTaskID_t id) { assert(id == WDT_TASK_TM_GEN); ++pets; }
BaseType_t xQueueSend(QueueHandle_t queue, const void *data, TickType_t wait) {
    assert(queue == xTelemetryQueue && data != NULL && wait == 0);
    assert(mode == MODE_NOMINAL && iterations % 5 == 0);
    HK_Telemetry_t expected;
    memset(&expected, 0, sizeof(expected));
    expected.timestamp = current_ticks;
    expected.sequence_count = (uint16_t)sends;
    expected.status_flags.system_mode = MODE_NOMINAL;
    expected.bus_voltage = 3.3f;
    // Exact bytes include unmeasured fields, bitfield storage and padding.
    assert(memcmp(data, &expected, sizeof(expected)) == 0);
    ++sends;
    return sends == 2 ? pdFAIL : pdPASS; // Full queue must not prevent future work/pets.
}
void vTaskDelay(TickType_t delay) {
    assert(delay == pdMS_TO_TICKS(1000));
    ++iterations;
    assert(reports == iterations && pets == iterations);
    current_ticks += delay;
    if (switch_safe && iterations == 10) mode = MODE_SAFE;
    if (iterations == limit) longjmp(task_exit, 1);
}
static void run(SystemMode_t initial, TickType_t start, bool change_mode) {
    mode = initial;
    current_ticks = start;
    switch_safe = change_mode;
    iterations = reports = pets = sends = mode_reads = 0;
    limit = 12;
    if (setjmp(task_exit) == 0) vTelemetryGeneratorTask(NULL);
    assert(reports == 12 && pets == 12 && mode_reads == 3);
    assert(sends == (initial == MODE_NOMINAL ? (change_mode ? 2U : 3U) : 0U));
}
int main(void) {
    run(MODE_NOMINAL, 123, false);
    run(MODE_NOMINAL, 123, true);
    run(MODE_INITIAL_BOOT, 0, false);
    run(MODE_SAFE, 0, false);
    run(MODE_DEGRADED, 0, false);
    run(MODE_EMERGENCY, 0, false);
    run(MODE_CRITICAL, 0, false);
    run(MODE_NOMINAL, UINT32_MAX / portTICK_PERIOD_MS - 200U, false);
    puts("PASS: TM generator 1 s polling/heartbeat, 5 s NOMINAL-only queue cadence,"
         " deterministic bytes/tick units, queue-full recovery and ms wrap.");
    return 0;
}

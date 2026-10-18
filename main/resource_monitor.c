#include "resource_monitor.h"
#include "startup_resources.h"
#include "watchdog.h"
#include "time_service.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdint.h>

#define MONITOR_STACK_BYTES 4096U
#define SAMPLE_MS 100U
#define REPORT_MS 30000U

static QueueHandle_t s_telemetry, s_commands;
static bool s_started;

// Only lifetime-stable tasks. DEPLOY/CMD_INJECT self-delete; never retain or
// inspect their handles. TC_RX may exit only during initial startup, before the
// first 30-second report. WIFI_MGR/YLOG_TX, when created, live for this boot.
static const struct { const char *name; unsigned budget; } s_tasks[] = {
    {"WDT_MON", 4096}, {"EPS_MGMT", 4096}, {"CMD_PROC", 4096},
    {"TC_RX", 4096}, {"EPS_MON", 2048}, {"FDIR", 4096},
    {"DATA_LOG", 4096}, {"ADCS_MGMT", 4096}, {"TM_GEN", 4096},
    {"SOLAR_HARV", 4096}, {"WIFI_MGR", 4096}, {"YLOG_TX", 3072},
    {"RECOVERY", 6144},
    {"RES_MON", MONITOR_STACK_BYTES},
};

static void resource_monitor_task(void *argument) {
    (void)argument;
    TickType_t wake = xTaskGetTickCount(), last_report = wake;
    unsigned sequence = 0, tm_peak = 0, tc_peak = 0;
    for (;;) {
        const unsigned tm_used = (unsigned)uxQueueMessagesWaiting(s_telemetry);
        const unsigned tc_used = (unsigned)uxQueueMessagesWaiting(s_commands);
        if (tm_used > tm_peak) tm_peak = tm_used;
        if (tc_used > tc_peak) tc_peak = tc_used;
        const TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - last_report) >= pdMS_TO_TICKS(REPORT_MS)) {
            last_report = now;
            const uint64_t report_started = TIME_GetMilliseconds();
            // fprintf intentionally bypasses the printf-to-event mirror. These
            // diagnostics stay on serial, avoiding 64-byte event truncation/load.
            fprintf(stdout, "DIAG: begin seq=%u uptime_ms=%lu tick_ms=%u.\n", ++sequence,
                    (unsigned long)(now * portTICK_PERIOD_MS), (unsigned)portTICK_PERIOD_MS);
            multi_heap_info_t heap;
            heap_caps_get_info(&heap, MALLOC_CAP_8BIT);
            fprintf(stdout, "DIAG: heap free=%u min=%u largest=%u.\n",
                    (unsigned)heap.total_free_bytes, (unsigned)heap.minimum_free_bytes,
                    (unsigned)heap.largest_free_block);
            fprintf(stdout, "DIAG: queue name=TM used=%u peak=%u capacity=%u sample_ms=%u.\n",
                    tm_used, tm_peak, STARTUP_TELEMETRY_QUEUE_DEPTH, SAMPLE_MS);
            fprintf(stdout, "DIAG: queue name=TC used=%u peak=%u capacity=%u sample_ms=%u.\n",
                    tc_used, tc_peak, STARTUP_COMMAND_QUEUE_DEPTH, SAMPLE_MS);
            for (unsigned i = 0; i < sizeof(s_tasks) / sizeof(s_tasks[0]); ++i) {
                const TaskHandle_t task = xTaskGetHandle(s_tasks[i].name);
                if (task != NULL) {
                    // Pinned ESP-IDF Xtensa StackType_t is uint8_t. Express the
                    // watermark in bytes, not upstream FreeRTOS 32-bit words.
                    fprintf(stdout, "DIAG: stack name=%s free_min=%u budget=%u.\n",
                            s_tasks[i].name, (unsigned)(uxTaskGetStackHighWaterMark(task) * sizeof(StackType_t)),
                            s_tasks[i].budget);
                } else {
                    fprintf(stdout, "DIAG: missing name=%s.\n", s_tasks[i].name);
                }
            }
            WATCHDOG_TaskTiming_t timings[WDT_TASK_COUNT];
            if (WATCHDOG_GetTimingSnapshot(timings)) {
                for (unsigned i = 0; i < WDT_TASK_COUNT; ++i) {
                    fprintf(stdout, "DIAG: timing id=%u count=%lu min_ms=%lu max_ms=%lu age_ms=%lu.\n", i,
                            (unsigned long)timings[i].pet_count, (unsigned long)timings[i].min_interval_ms,
                            (unsigned long)timings[i].max_interval_ms, (unsigned long)timings[i].age_ms);
                }
            } else {
                fprintf(stdout, "DIAG: timing unavailable.\n");
            }
            fprintf(stdout, "DIAG: observer_ms=%lu.\n", (unsigned long)(TIME_GetMilliseconds() - report_started));
            fprintf(stdout, "DIAG: end seq=%u.\n", sequence);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_MS));
    }
}

bool RESOURCE_MONITOR_Start(QueueHandle_t telemetry, QueueHandle_t commands) {
    // Serialized app_main startup only. A failed creation may be retried by a
    // caller before any monitor exists; never mutate a running monitor's queues.
    if (s_started || telemetry == NULL || commands == NULL) return false;
    s_telemetry = telemetry;
    s_commands = commands;
    if (xTaskCreate(resource_monitor_task, "RES_MON", MONITOR_STACK_BYTES, NULL, 1, NULL) != pdPASS) return false;
    s_started = true;
    return true;
}

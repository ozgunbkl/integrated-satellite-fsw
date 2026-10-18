// Real app_main + core/resource helpers. Deterministic task probes, no real RTOS tasks.
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "startup_core.h"
#include "startup_resources.h"
#include "resource_monitor.h"
#include "recovery_app.h"
#include "time_service.h"
#include "system_mode.h"
#include "fdir_service.h"
#include "yamcs_interface.h"
#include "archive_service.h"
#include "hk_service.h"
#include "payload_service.h"
#include "adcs_service.h"
#include "adcs_sim.h"
#include "wifi_service.h"
#include "satellite_types.h"
#include "eps_telemetry.h"
#include "watchdog.h"
#include "include/esp_log.h"
#include "include/freertos/task.h"

extern void app_main(void);
extern QueueHandle_t xTelemetryQueue, xCommandQueue;

typedef enum {
    RECOVERY_PREPARE, TIME, MODE, FDIR, WATCHDOG_INIT, SENDER, ARCHIVE, HK, PAYLOAD, PAYLOAD_SNAPSHOT,
    ADCS, SPIN, ADCS_SNAPSHOT, TM_QUEUE, TC_QUEUE, EPS_MUTEX, NETWORK,
    LOG_SNAPSHOT, WATCHDOG_ARM, TASK_START, CALLER_CONTINUATION
} Step;
static const Step expected_prefix[] = {RECOVERY_PREPARE, TIME, MODE, FDIR, WATCHDOG_INIT, SENDER, ARCHIVE, HK, PAYLOAD,
    PAYLOAD_SNAPSHOT, ADCS, SPIN, ADCS_SNAPSHOT, TM_QUEUE, TC_QUEUE, EPS_MUTEX, NETWORK,
    LOG_SNAPSHOT, WATCHDOG_ARM};
static Step trace[40];
static unsigned trace_length, task_calls, errors, warnings, aborts, continued;
static unsigned diagnostics_calls;
static unsigned hk_ready_messages;
static unsigned core_ready_messages;
static unsigned created_mask, probe_calls;
static uint32_t shared_task_canary = 0x12340000U;
static unsigned archive_records = 5;
static int tm_slot, tc_slot, eps_slot;
static const char *scenario;
static char last_error[256];
static jmp_buf abort_exit;
static bool abort_expected;

// Probes model work before xTaskCreate returns and during the failure log.
// They never run the real task bodies or pretend to implement a scheduler.
static void run_created_task_probe(TaskFunction_t function, void *argument);
#define MOCK_TASK(name) void name(void *argument) { run_created_task_probe(name, argument); }
MOCK_TASK(vSoftwareWatchdogTask)
MOCK_TASK(vDeploymentTask)
MOCK_TASK(vEPS_Manager_Task)
MOCK_TASK(vCommandProcessorTask)
MOCK_TASK(vYamcsTcReceiverTask)
MOCK_TASK(vEPSMonitoringTask)
MOCK_TASK(vFDIRTask)
MOCK_TASK(vDataLoggerTask)
MOCK_TASK(vADCS_Manager_Task)
MOCK_TASK(vTelemetryGeneratorTask)
MOCK_TASK(vCommandInjectionTask)
MOCK_TASK(vSolarHarvest_Task)

static const struct {
    const char *name;
    TaskFunction_t function;
    configSTACK_DEPTH_TYPE stack;
    UBaseType_t priority;
} tasks[] = {
    {"WDT_MON", vSoftwareWatchdogTask, 4096, 6},
    {"DEPLOY", vDeploymentTask, 4096, 5},
    {"EPS_MGMT", vEPS_Manager_Task, 4096, configMAX_PRIORITIES - 1},
    {"CMD_PROC", vCommandProcessorTask, 4096, 5},
    {"TC_RX", vYamcsTcReceiverTask, 4096, 4},
    {"EPS_MON", vEPSMonitoringTask, 2048, 4},
    {"FDIR", vFDIRTask, 4096, 4},
    {"DATA_LOG", vDataLoggerTask, 4096, 4},
    {"ADCS_MGMT", vADCS_Manager_Task, 4096, 4},
    {"TM_GEN", vTelemetryGeneratorTask, 4096, 3},
    {"CMD_INJECT", vCommandInjectionTask, 4096, 1},
    {"SOLAR_HARV", vSolarHarvest_Task, 4096, 1},
};
static unsigned task_probe_runs[sizeof(tasks) / sizeof(tasks[0])];
_Static_assert(sizeof(tasks) / sizeof(tasks[0]) <= 32, "Created mask must cover all task probes");

static bool is(const char *name) { return strcmp(scenario, name) == 0; }
static bool fails(const char *name) {
    return is(name) || (strncmp(scenario, "repeat_", 7) == 0 && strcmp(scenario + 7, name) == 0);
}
static const char *failed_task_name(void) {
    const char *name = scenario;
    if (strncmp(name, "repeat_", 7) == 0) name += 7;
    if (strncmp(name, "partial_", 8) == 0) name += 8;
    return strncmp(name, "task_", 5) == 0 ? name + 5 : NULL;
}
static bool schedule_probes(void) {
    const char *name = scenario;
    if (strncmp(name, "repeat_", 7) == 0) name += 7;
    return is("early_success") || strncmp(name, "partial_task_", 13) == 0;
}
static void run_created_task_probe(TaskFunction_t function, void *argument) {
    assert(schedule_probes() && argument == NULL);
    unsigned index = 0;
    while (index < sizeof(tasks) / sizeof(tasks[0]) && tasks[index].function != function) ++index;
    assert(index < sizeof(tasks) / sizeof(tasks[0]) && (created_mask & (1U << index)) != 0);
    assert(task_calls >= index + 1 && errors <= 1);
    ++task_probe_runs[index]; ++probe_calls;
    shared_task_canary += 0x101U * (index + 1);
}
static void record(Step step) {
    assert(trace_length < sizeof(trace) / sizeof(trace[0]));
    if (trace_length < sizeof(expected_prefix) / sizeof(expected_prefix[0])) {
        assert(expected_prefix[trace_length] == step);
    }
    trace[trace_length++] = step;
}
void RECOVERY_APP_Prepare(void) { record(RECOVERY_PREPARE); }
bool RECOVERY_APP_Start(void) { assert(task_calls == 0 && trace_length == sizeof(expected_prefix) / sizeof(expected_prefix[0])); return true; }
void TIME_Init(void) { record(TIME); }
bool SYSTEM_MODE_Init(SystemMode_t mode) { assert(mode == MODE_INITIAL_BOOT); record(MODE); return !fails("mode"); }
bool FDIR_Init(void) { record(FDIR); return !fails("fdir"); }
bool WATCHDOG_Init(void) { record(WATCHDOG_INIT); return !fails("watchdog"); }
bool WATCHDOG_Arm(void) {
    assert(task_calls == 0 && xTelemetryQueue == &tm_slot && xCommandQueue == &tc_slot && xEPSDataMutex == &eps_slot);
    record(WATCHDOG_ARM); return !is("watchdog_arm");
}
bool YAMCS_Init(void) { record(SENDER); return !fails("sender"); }
bool Archive_Init(void) { record(ARCHIVE); if (fails("archive")) return false; archive_records = 0; return true; }
bool HK_Init(void) { record(HK); return !fails("hk"); }
void Payload_Init(void) { record(PAYLOAD); }
PayloadTelemetry_t Payload_GetTelemetry(void) {
    record(PAYLOAD_SNAPSHOT); return (PayloadTelemetry_t){.current_state = PL_STATE_OFF};
}
void ADCS_Init(void) { record(ADCS); }
void SIM_SetTrueAngularVelocity(float x, float y, float z) {
    assert(x == 10.0f && y == -5.0f && z == 0.5f); record(SPIN);
}
ADCSMode_t ADCS_GetMode(void) { record(ADCS_SNAPSHOT); return ADCS_MODE_DETUMBLE; }
QueueHandle_t xQueueCreate(UBaseType_t depth, UBaseType_t item_size) {
    if (trace_length == TM_QUEUE) {
        assert(depth == 10 && item_size == sizeof(HK_Telemetry_t)); record(TM_QUEUE);
        return fails("tm_queue") ? NULL : &tm_slot;
    }
    assert(depth == 5 && item_size == sizeof(TelecommandPacket_t)); record(TC_QUEUE);
    return fails("tc_queue") ? NULL : &tc_slot;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { record(EPS_MUTEX); return fails("eps_mutex") ? NULL : &eps_slot; }
esp_err_t WIFI_Start(void) {
    assert(xTelemetryQueue == &tm_slot && xCommandQueue == &tc_slot && xEPSDataMutex == &eps_slot);
    record(NETWORK); return is("offline") ? ESP_FAIL : ESP_OK;
}
bool WIFI_IsLogMirrorStarted(void) { record(LOG_SNAPSHOT); return !is("offline") && !is("no_log"); }
bool RESOURCE_MONITOR_Start(QueueHandle_t telemetry, QueueHandle_t commands) {
    assert(task_calls == 12 && telemetry == &tm_slot && commands == &tc_slot);
    ++diagnostics_calls; return !is("resource_monitor_failure");
}
const char *esp_err_to_name(esp_err_t error) { assert(error == ESP_FAIL); return "ESP_FAIL"; }
void esp_log_level_set(const char *tag, int level) {
    assert((strcmp(tag, "*") == 0 && level == ESP_LOG_WARN) ||
           (strcmp(tag, "MAIN") == 0 && level == ESP_LOG_INFO));
}
void test_log_error(const char *tag, const char *format, ...) {
    assert(strcmp(tag, "MAIN") == 0); errors++;
    va_list args; va_start(args, format);
    const int written = vsnprintf(last_error, sizeof(last_error), format, args);
    va_end(args);
    assert(written > 0 && (unsigned)written < sizeof(last_error));
    if (errors == 1 && schedule_probes() && failed_task_name() != NULL) {
        // An earlier task can run even after a later allocation failed, before abort.
        for (unsigned i = 0; i < sizeof(tasks) / sizeof(tasks[0]); ++i)
            if (created_mask & (1U << i)) tasks[i].function(NULL);
    }
}
void test_log_warning(const char *tag, const char *format, ...) {
    (void)format; assert(strcmp(tag, "MAIN") == 0); warnings++;
}
int __wrap_printf(const char *format, ...) {
    char message[256];
    va_list args; va_start(args, format);
    const int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    assert(written >= 0 && (unsigned)written < sizeof(message));
    if (strstr(message, "MAIN: HK and payload initialized before task creation") != NULL)
        ++hk_ready_messages;
    if (strstr(message, "MAIN: Core tasks created.") != NULL) {
        assert(task_calls == sizeof(tasks) / sizeof(tasks[0]) && diagnostics_calls == 1);
        ++core_ready_messages;
    }
    return fputs(message, stdout) < 0 ? -1 : written;
}
BaseType_t xTaskCreate(TaskFunction_t function, const char *name, configSTACK_DEPTH_TYPE stack,
                      void *argument, UBaseType_t priority, TaskHandle_t *handle) {
    assert(task_calls < sizeof(tasks) / sizeof(tasks[0]));
    assert(trace_length == sizeof(expected_prefix) / sizeof(expected_prefix[0]) + task_calls);
    assert(function == tasks[task_calls].function && strcmp(name, tasks[task_calls].name) == 0);
    assert(stack == tasks[task_calls].stack && priority == tasks[task_calls].priority);
    assert(argument == NULL && handle == NULL);
    const unsigned index = task_calls;
    record(TASK_START); task_calls++;
    if (failed_task_name() != NULL && strcmp(failed_task_name(), name) == 0)
        return (BaseType_t)-1; // Allocation failure need not be zero.
    created_mask |= 1U << index;
    if (schedule_probes()) function(argument); // Before the SDK call returns.
    return pdPASS;
}
_Noreturn void __wrap_abort(void) {
    assert(abort_expected);
    if (continued == 0) assert(diagnostics_calls == 0 && core_ready_messages == 0);
    if (failed_task_name() != NULL && aborts == 0) assert(errors == 1 && task_calls > 0);
    aborts++; longjmp(abort_exit, 1);
}
static void caller_probe(void) { app_main(); continued++; record(CALLER_CONTINUATION); }

int main(int argc, char **argv) {
    assert(argc == 2); scenario = argv[1];
    const bool failure = fails("mode") || fails("fdir") || fails("watchdog") || fails("sender") || fails("archive") || fails("hk") || fails("tm_queue") ||
                         fails("tc_queue") || fails("eps_mutex") || is("watchdog_arm") || failed_task_name() != NULL;
    abort_expected = failure;
    if (setjmp(abort_exit) == 0) { caller_probe(); assert(!failure); }
    assert(aborts == (failure ? 1u : 0u) && errors == (failure ? 1u : 0u));
    assert(continued == (failure ? 0u : 1u));
    assert(diagnostics_calls == (failure ? 0U : 1U));
    assert(core_ready_messages == (failure ? 0U : 1U));
    assert(hk_ready_messages == (trace_length > (unsigned)PAYLOAD_SNAPSHOT ? 1U : 0U));
    if (fails("mode") || fails("fdir") || fails("watchdog") || fails("sender")) {
        const Step failed_step = fails("mode") ? MODE : (fails("fdir") ? FDIR : (fails("watchdog") ? WATCHDOG_INIT : SENDER));
        const char *error_text = fails("mode") ? "system mode" : (fails("fdir") ? "FDIR" :
            (fails("watchdog") ? "software watchdog" : "Yamcs telemetry sender"));
        assert(trace_length == (unsigned)failed_step + 1 && trace[trace_length - 1] == failed_step);
        assert(task_calls == 0 && archive_records == 5 && strstr(last_error, error_text) != NULL);
        assert(xTelemetryQueue == NULL && xCommandQueue == NULL && xEPSDataMutex == NULL);
    } else if (fails("archive")) {
        assert(trace_length == (unsigned)ARCHIVE + 1 && task_calls == 0 && archive_records == 5);
        assert(strstr(last_error, "RAM archive service") != NULL);
        assert(xTelemetryQueue == NULL && xCommandQueue == NULL && xEPSDataMutex == NULL);
    } else if (fails("hk")) {
        assert(trace_length == (unsigned)HK + 1 && trace[trace_length - 1] == HK);
        assert(task_calls == 0 && archive_records == 0 && hk_ready_messages == 0);
        assert(strstr(last_error, "HK service") != NULL);
        assert(xTelemetryQueue == NULL && xCommandQueue == NULL && xEPSDataMutex == NULL);
    } else if (fails("tm_queue") || fails("tc_queue") || fails("eps_mutex")) {
        const Step failed_step = fails("tm_queue") ? TM_QUEUE : (fails("tc_queue") ? TC_QUEUE : EPS_MUTEX);
        assert(trace_length == (unsigned)failed_step + 1 && task_calls == 0 && archive_records == 0);
        assert(xTelemetryQueue == NULL && xCommandQueue == NULL && xEPSDataMutex == NULL);
    } else if (is("watchdog_arm")) {
        assert(trace_length == (unsigned)WATCHDOG_ARM + 1 && task_calls == 0 && archive_records == 0);
        assert(strstr(last_error, "arm software watchdog") != NULL);
    } else if (failed_task_name() != NULL) {
        assert(task_calls > 0 && strcmp(failed_task_name(), tasks[task_calls - 1].name) == 0);
        assert(trace_length == sizeof(expected_prefix) / sizeof(expected_prefix[0]) + task_calls);
        char expected_error[256];
        const unsigned index = task_calls - 1;
        snprintf(expected_error, sizeof(expected_error),
                 "Required task creation failed: %s (stack=%lu, priority=%lu, required_created=%u/%u)",
                 tasks[index].name, (unsigned long)tasks[index].stack,
                 (unsigned long)tasks[index].priority, index, (unsigned)(sizeof(tasks) / sizeof(tasks[0])));
        assert(strcmp(last_error, expected_error) == 0);
        assert(created_mask == ((1U << index) - 1U) && archive_records == 0);
        assert(xTelemetryQueue == &tm_slot && xCommandQueue == &tc_slot && xEPSDataMutex == &eps_slot);
    } else {
        assert(task_calls == 12 && trace_length == sizeof(expected_prefix) / sizeof(expected_prefix[0]) + 13 && archive_records == 0);
        assert(warnings == ((is("offline") || is("no_log") || is("resource_monitor_failure")) ? 1u : 0u));
    }
    unsigned expected_probe_calls = 0;
    uint32_t expected_canary = 0x12340000U;
    for (unsigned i = 0; i < sizeof(tasks) / sizeof(tasks[0]); ++i) {
        const unsigned runs = schedule_probes() && (created_mask & (1U << i)) ? (failure ? 2U : 1U) : 0U;
        assert(task_probe_runs[i] == runs);
        expected_probe_calls += runs;
        expected_canary += 0x101U * (i + 1) * runs;
    }
    assert(probe_calls == expected_probe_calls && shared_task_canary == expected_canary);
    if (strncmp(scenario, "repeat_", 7) == 0) {
        const unsigned previous_trace = trace_length, previous_aborts = aborts, previous_errors = errors;
        const unsigned previous_ready_messages = hk_ready_messages;
        archive_records = 7; abort_expected = true;
        if (setjmp(abort_exit) == 0) { STARTUP_InitCoreServices(); assert(!"Repeated core startup returned"); }
        assert(trace_length == previous_trace && archive_records == 7);
        assert(probe_calls == expected_probe_calls && shared_task_canary == expected_canary);
        assert(hk_ready_messages == previous_ready_messages);
        assert(aborts == previous_aborts + 1 && errors == previous_errors + 1);
        assert(strstr(last_error, "already attempted") != NULL);
    }
    printf("PASS: real app_main startup case %s; ordered service/resource/task gate and error policy preserved.\n", scenario);
    return 0;
}

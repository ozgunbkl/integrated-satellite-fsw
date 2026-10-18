/* Test-driver orchestration only. Real-service behavior has its own integration
 * fixture; this scheduler/API model verifies that the driver cannot self-clear,
 * self-authorize mode, skip a required ground request or erase archive history. */
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/task.h"
#include "eps_telemetry.h"
#include "system_mode.h"
#include "fdir_service.h"
#include "adcs_service.h"
#include "payload_service.h"
#include "archive_service.h"
#include "commands.h"

void __wrap_vSolarHarvest_Task(void *argument);
void __wrap_vCommandInjectionTask(void *argument);
void __wrap_FDIR_ProcessCommand(const uint8_t *payload, uint16_t length);

EPS_Telemetry_t g_eps_telemetry;
SemaphoreHandle_t xEPSDataMutex = (void *)1;
static TickType_t tick;
static bool held, deployed, fault;
static SystemMode_t mode;
static eps_mode_t eps;
static ADCSMode_t adcs = ADCS_MODE_DETUMBLE;
static PayloadTelemetry_t payload;
static unsigned pending, ground_calls, samples, last_sample_tick, deletes;
static char stage[32], logs[16384];
static size_t log_length;
static const char *scenario;
static jmp_buf finished;

static bool is(const char *name) { return strcmp(scenario, name) == 0; }
int __wrap_printf(const char *format, ...) {
    assert(!held);
    va_list args;
    va_start(args, format);
    int length = vsnprintf(logs + log_length, sizeof(logs) - log_length, format, args);
    va_end(args);
    assert(length >= 0 && (size_t)length < sizeof(logs) - log_length);
    const char *line = logs + log_length;
    (void)sscanf(line, "TEST: LIFECYCLE state=%31s", stage);
    (void)sscanf(line, "TEST: LIFECYCLE request=%u.", &pending);
    log_length += (size_t)length;
    return length;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks) {
    assert(handle == xEPSDataMutex && ticks == pdMS_TO_TICKS(10) && !held);
    if (is("eps_lock")) return pdFALSE;
    held = true;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
    assert(handle == xEPSDataMutex && held);
    held = false;
    return pdTRUE;
}
TickType_t xTaskGetTickCount(void) { return tick; }
unsigned uxTaskGetStackHighWaterMark(void *task) { assert(task == NULL); return is("stack_low") ? 767 : 2048; }
void vTaskDelete(void *task) { assert(task == NULL && !held); ++deletes; longjmp(finished, 1); }
void vTaskSuspend(void *task) { assert(task == NULL && !held); ++deletes; longjmp(finished, 1); }

void __real_FDIR_ProcessCommand(const uint8_t *data, uint16_t length) {
    assert(!held && length == 2 && data[0] == FDIR_CMD_RESET_MODE && data[1] == MODE_NOMINAL);
    ++ground_calls;
    if ((deployed && !fault) || is("boot_accept")) mode = MODE_NOMINAL;
}
void vTaskDelay(TickType_t ticks) {
    assert(!held && ticks > 0 && ticks <= 1000);
    tick += ticks;
    if (pending && !is("missing_ground")) {
        const uint8_t data[] = {FDIR_CMD_RESET_MODE, MODE_NOMINAL};
        pending = 0;
        __wrap_FDIR_ProcessCommand(data, sizeof(data));
    }
    if (tick >= 12000 && !is("no_deployment")) {
        deployed = true;
        if (mode == MODE_INITIAL_BOOT) mode = MODE_NOMINAL;
    }
    if (g_eps_telemetry.f_BusVoltage < 5) {
        fault = true;
        mode = MODE_SAFE;
        eps = EPS_MODE_SAFE;
        g_eps_telemetry.ul_PDU_Register = is("comms_shed") ? 0 : 4;
        g_eps_telemetry.xFaultFlags.safe_mode_active = 1;
        if (!is("adcs_not_inhibited")) adcs = ADCS_MODE_SAFE;
        payload.current_state = PL_STATE_SAFE;
    } else if (fault && !is("no_owner_clear")) {
        fault = false;
        if (is("automatic_resume")) mode = MODE_NOMINAL;
    }
    if (mode == MODE_NOMINAL && !fault) {
        eps = EPS_MODE_NOMINAL;
        g_eps_telemetry.ul_PDU_Register = 7;
        g_eps_telemetry.xFaultFlags.safe_mode_active = 0;
        if (is("implicit_rearm") && strcmp(stage, "cleared") == 0) adcs = ADCS_MODE_DETUMBLE;
    }
    if (payload.current_state == PL_STATE_ACTIVE && tick - last_sample_tick >= 100 && !is("no_data")) {
        last_sample_tick = tick;
        payload.bytes_generated += 64;
        ++samples;
    }
    if (is("safe_data_leak") && strcmp(stage, "faulted") == 0) ++payload.bytes_generated;
}
SystemMode_t SYSTEM_MODE_Get(void) { assert(!held); return mode; }
bool SYSTEM_MODE_IsDeploymentComplete(void) { assert(!held); return deployed; }
eps_mode_t EPS_GetMode(void) { assert(!held); return eps; }
bool FDIR_GetActiveFaults(FDIR_ActiveFaultSnapshot_t *out) {
    assert(!held);
    if (is("snapshot_failure")) return false;
    memset(out, 0, sizeof(*out));
    out->count = fault;
    out->overflow = is("overflow");
    out->recording_uncertain = is("uncertain");
    if (fault) out->records[0] = (SystemFaultRecord_t){.source=SRC_EPS, .code=0xE102, .revision=7,
                                                      .fatal=is("fatal")};
    if (is("other_cause") && fault) out->records[0].source = SRC_ADCS;
    return true;
}
ADCSMode_t ADCS_GetMode(void) { assert(!held); return adcs; }
bool ADCS_SetMode(ADCSMode_t requested) {
    assert(!held);
    if (requested == ADCS_MODE_NOMINAL && (mode == MODE_INITIAL_BOOT || adcs == ADCS_MODE_SAFE)) {
        adcs = ADCS_MODE_SAFE;
        return is("direct_rearm");
    }
    adcs = requested;
    return true;
}
PayloadTelemetry_t Payload_GetTelemetry(void) { assert(!held); return payload; }
PayloadStatus_t Payload_ProcessCommand(PayloadCmd_t command, uint8_t param) {
    assert(!held && param == 0);
    if (command == PL_CMD_INIT) {
        assert(payload.current_state == PL_STATE_OFF || payload.current_state == PL_STATE_SAFE);
    payload.current_state = PL_STATE_STANDBY;
    } else if (command == PL_CMD_START) {
        assert(payload.current_state == PL_STATE_STANDBY);
        if (mode == MODE_INITIAL_BOOT) return is("boot_payload_allowed") ? PL_OK : PL_ERR_INHIBITED;
        assert(mode == MODE_NOMINAL && eps == EPS_MODE_NOMINAL && !fault);
        payload.current_state = PL_STATE_ACTIVE;
    } else {
        assert(command == PL_CMD_STOP && payload.current_state == PL_STATE_ACTIVE);
        payload.current_state = PL_STATE_STANDBY;
    }
    return PL_OK;
}
uint16_t Archive_GetUsage(void) {
    assert(!held);
    return is("usage_unavailable") ? ARCHIVE_USAGE_UNAVAILABLE : (uint16_t)((samples + 1) * sizeof(ArchiveRecord_t));
}
ArchiveStatus_t Archive_ReadRecord(uint16_t index, ArchiveRecord_t *out) {
    assert(!held);
    memset(out, 0, sizeof(*out));
    if (index == 0) { out->record_id = REC_ID_HK; return ARCHIVE_OK; }
    if (index > samples) return ARCHIVE_ERR_NOT_FOUND;
    out->record_id = REC_ID_PAYLOAD;
    out->length = 64;
    out->crc = 123;
    if (is("retention_corruption") && strcmp(stage, "ground") == 0) out->payload[0] = 1;
    return ARCHIVE_OK;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    scenario = argv[1];
    g_eps_telemetry.ul_PDU_Register = 7;
    if (setjmp(finished) == 0) {
        if (is("demo_disabled")) __wrap_vCommandInjectionTask(NULL);
        else if (is("bad_argument")) __wrap_vSolarHarvest_Task((void *)1);
        else {
            if (is("unexpected_command")) { const uint8_t wrong[] = {0x51}; __wrap_FDIR_ProcessCommand(wrong, 1); }
            __wrap_vSolarHarvest_Task(NULL);
        }
        assert(!"Task must terminate through vTaskDelete");
    }
    assert(deletes == 1 && !held);
    if (is("success")) {
        assert(strstr(logs, "TEST: LIFECYCLE finished.") && !strstr(logs, "TEST ERROR:"));
        assert(ground_calls == 3 && deployed && mode == MODE_NOMINAL && !fault && samples >= 2);
    } else if (is("demo_disabled")) {
        assert(strstr(logs, "demo_commands=disabled") && ground_calls == 0 && tick == 0);
    } else {
        assert(strstr(logs, "TEST ERROR:") && !strstr(logs, "TEST: LIFECYCLE finished."));
    }
    puts("PASS: lifecycle driver selected case; no hardware or ground I/O.");
    return 0;
}

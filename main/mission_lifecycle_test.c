#include "mission_lifecycle_test.h"
#if defined(FSW_TEST_MISSION_LIFECYCLE)
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "eps_telemetry.h"
#include "eps_commands.h"
#include "system_mode.h"
#include "fdir_service.h"
#include "adcs_service.h"
#include "payload_service.h"
#include "archive_service.h"
#include "commands.h"

// One driver task (the existing SOLAR_HARV slot), one command-processor task.
// Release/acquire synchronizes the test handshake, not the production services.
static atomic_uint s_expected_request, s_completed_requests;
static atomic_bool s_bad_command;
_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_BOOL_LOCK_FREE == 2,
               "Test handshake requires lock-free target atomics");

typedef struct {
    SystemMode_t mode;
    eps_mode_t eps;
    ADCSMode_t adcs;
    PayloadTelemetry_t payload;
    EPS_Telemetry_t power;
    FDIR_ActiveFaultSnapshot_t faults;
    bool deployed;
    uint16_t usage;
    uint32_t revision;
} Observation;

static bool failed(unsigned check) {
    printf("TEST ERROR: LIFECYCLE check=%u.\n", check);
    return false;
}
#define REQUIRE(condition) do { if (!(condition)) return failed(__LINE__); } while (0)

static bool observe(Observation *out) {
    if (atomic_load(&s_bad_command) || !FDIR_GetActiveFaults(&out->faults)) return false;
    if (out->faults.overflow || out->faults.recording_uncertain || out->faults.count > 1) return false;
    out->revision = 0;
    if (out->faults.count) {
        const SystemFaultRecord_t *record = &out->faults.records[0];
        if (record->source != SRC_EPS || record->code != FAULT_EPS_LOW_VOLTAGE ||
                record->fatal || !record->revision) return false;
        out->revision = record->revision;
    }
    out->mode = SYSTEM_MODE_Get();
    out->deployed = SYSTEM_MODE_IsDeploymentComplete();
    out->eps = EPS_GetMode();
    // Unlike ADCS_GetTelemetry/GetLastCommand, GetMode does not force inhibition.
    // Allow the real manager to observe the permission loss itself.
    out->adcs = ADCS_GetMode();
    out->payload = Payload_GetTelemetry();
    out->usage = Archive_GetUsage();
    if (out->usage == ARCHIVE_USAGE_UNAVAILABLE || out->usage >= ARCHIVE_SIZE_BYTES) return false;
    if (xSemaphoreTake(xEPSDataMutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    out->power = g_eps_telemetry;
    return xSemaphoreGive(xEPSDataMutex) == pdTRUE;
}

static bool voltage(float value) {
    if (xSemaphoreTake(xEPSDataMutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    // Replace only simulated environment inputs. Do not write PDU, EPS mode,
    // global mode, faults, watchdog state or archive contents.
    g_eps_telemetry.f_BusVoltage = value;
    g_eps_telemetry.f_SolarInputPower = 0.0f;
    return xSemaphoreGive(xEPSDataMutex) == pdTRUE;
}

static void state(const char *stage, const Observation *out) {
    printf("TEST: LIFECYCLE state=%s mode=%u deployment=%u eps=%u adcs=%u payload=%u bytes=%lu faults=%u revision=%lu pdu=%lu archive=%u.\n",
           stage, (unsigned)out->mode, (unsigned)out->deployed, (unsigned)out->eps,
           (unsigned)out->adcs, (unsigned)out->payload.current_state,
           (unsigned long)out->payload.bytes_generated, (unsigned)out->faults.count,
           (unsigned long)out->revision, (unsigned long)out->power.ul_PDU_Register, (unsigned)out->usage);
}

extern void __real_FDIR_ProcessCommand(const uint8_t *payload, uint16_t len);
void __wrap_FDIR_ProcessCommand(const uint8_t *payload, uint16_t len) {
    // Observe the REAL router/FDIR command. Never fabricate ACK/NACK or mode.
    const unsigned request = atomic_exchange(&s_expected_request, 0);
    if (!payload || len != 2 || payload[0] != FDIR_CMD_RESET_MODE || payload[1] != MODE_NOMINAL ||
            request == 0 || request > 3 || request != atomic_load(&s_completed_requests) + 1) {
        atomic_store(&s_bad_command, true);
        printf("TEST ERROR: LIFECYCLE unexpected command.\n");
        return; // Test-only quarantine; never forward an unapproved command.
    }
    __real_FDIR_ProcessCommand(payload, len);
    const SystemMode_t mode = SYSTEM_MODE_Get();
    if ((request == 1 && (mode != MODE_INITIAL_BOOT || SYSTEM_MODE_IsDeploymentComplete())) ||
        (request == 2 && mode != MODE_SAFE) || (request == 3 && mode != MODE_NOMINAL)) {
        atomic_store(&s_bad_command, true);
        printf("TEST ERROR: LIFECYCLE command outcome.\n");
        return;
    }
    printf("TEST: LIFECYCLE command=%u mode=%u.\n", request, (unsigned)mode);
    atomic_store(&s_completed_requests, request);
}

static bool request(unsigned number) {
    REQUIRE(atomic_load(&s_completed_requests) == number - 1 && !atomic_load(&s_expected_request));
    atomic_store(&s_expected_request, number);
    printf("TEST: LIFECYCLE request=%u.\n", number);
    const TickType_t start = xTaskGetTickCount();
    while (atomic_load(&s_completed_requests) != number) {
        REQUIRE(!atomic_load(&s_bad_command));
        REQUIRE((TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(10000));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return !atomic_load(&s_bad_command);
}

static bool quiet_safe(const Observation *out, uint32_t bytes, bool active) {
    return out->mode == MODE_SAFE && out->eps == EPS_MODE_SAFE && out->deployed &&
        out->adcs == ADCS_MODE_SAFE && out->payload.current_state == PL_STATE_SAFE &&
        out->payload.bytes_generated == bytes && out->faults.count == (unsigned)active &&
        (out->power.ul_PDU_Register & 7U) == PDU_LOAD_COMMS_BIT && out->power.xFaultFlags.safe_mode_active;
}

static bool run_scenario(void) {
    Observation out;
    REQUIRE(voltage(EPS_VOLTAGE_RECOVERY));
    REQUIRE(observe(&out) && !out.deployed && out.mode == MODE_INITIAL_BOOT && !out.faults.count);
    REQUIRE(Payload_ProcessCommand(PL_CMD_INIT, 0) == PL_OK);
    REQUIRE(Payload_ProcessCommand(PL_CMD_START, 0) == PL_ERR_INHIBITED);
    REQUIRE(!ADCS_SetMode(ADCS_MODE_NOMINAL));
    REQUIRE(ADCS_SetMode(ADCS_MODE_DETUMBLE));
    REQUIRE(observe(&out) && out.adcs == ADCS_MODE_DETUMBLE && out.payload.current_state == PL_STATE_STANDBY && !out.payload.bytes_generated);
    state("boot", &out);
    REQUIRE(request(1));

    TickType_t start = xTaskGetTickCount();
    do {
        vTaskDelay(pdMS_TO_TICKS(20));
        REQUIRE(observe(&out) && !out.faults.count);
        REQUIRE((TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(15000));
    } while (!out.deployed || out.mode != MODE_NOMINAL || out.eps != EPS_MODE_NOMINAL);
    REQUIRE(ADCS_SetMode(ADCS_MODE_NOMINAL));
    REQUIRE(Payload_ProcessCommand(PL_CMD_START, 0) == PL_OK);
    start = xTaskGetTickCount();
    do {
        vTaskDelay(pdMS_TO_TICKS(20));
        REQUIRE(observe(&out) && !out.faults.count && out.mode == MODE_NOMINAL);
        REQUIRE((TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(1000));
    } while (!out.payload.bytes_generated);
    state("operating", &out);

    // Save one actual DATA_LOG-generated payload record, not a test write.
    ArchiveRecord_t original, retained;
    uint16_t payload_index;
    for (payload_index = 0; payload_index < 30; ++payload_index) {
        REQUIRE(Archive_ReadRecord(payload_index, &original) == ARCHIVE_OK);
        if (original.record_id == REC_ID_PAYLOAD) break;
    }
    REQUIRE(payload_index < 30 && original.length == 64);
    REQUIRE(voltage(4.9f)); // <5 V EPS manager path, but >3.3/2.5 V duplicate monitors.
    start = xTaskGetTickCount();
    do {
        vTaskDelay(pdMS_TO_TICKS(100));
        REQUIRE(observe(&out));
        REQUIRE((TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(5000));
    } while (!quiet_safe(&out, out.payload.bytes_generated, true));
    const uint32_t stopped_bytes = out.payload.bytes_generated, revision = out.revision;
    // One in-flight authorized transaction may finish. Establish quiescence
    // AFTER both real periodic services enter SAFE, not at the voltage write.
    for (unsigned sample = 0; sample < 3; ++sample) {
        vTaskDelay(pdMS_TO_TICKS(100));
        REQUIRE(observe(&out) && quiet_safe(&out, stopped_bytes, true) && out.revision == revision);
    }
    state("faulted", &out);
    REQUIRE(request(2));
    REQUIRE(observe(&out) && quiet_safe(&out, stopped_bytes, true) && out.revision == revision);

    REQUIRE(voltage(EPS_VOLTAGE_RECOVERY));
    start = xTaskGetTickCount();
    do {
        vTaskDelay(pdMS_TO_TICKS(100));
        REQUIRE(observe(&out));
        REQUIRE((TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(5000));
    } while (out.faults.count);
    // Only the real EPS owner can resolve its critical record. No ground mode
    // request until two seconds of healthy-but-still-inhibited observations.
    for (unsigned sample = 0; sample < 20; ++sample) {
        REQUIRE(quiet_safe(&out, stopped_bytes, false));
        vTaskDelay(pdMS_TO_TICKS(100));
        REQUIRE(observe(&out));
    }
    REQUIRE(quiet_safe(&out, stopped_bytes, false));
    state("cleared", &out);
    REQUIRE(request(3));
    start = xTaskGetTickCount();
    do {
        vTaskDelay(pdMS_TO_TICKS(100));
        REQUIRE(observe(&out) && out.mode == MODE_NOMINAL && !out.faults.count &&
                out.adcs == ADCS_MODE_SAFE && out.payload.current_state == PL_STATE_SAFE &&
                out.payload.bytes_generated == stopped_bytes);
        REQUIRE((TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(5000));
    } while (out.eps != EPS_MODE_NOMINAL);
    state("ground", &out);
    REQUIRE(!ADCS_SetMode(ADCS_MODE_NOMINAL)); // Explicit DETUMBLE rearm required.
    REQUIRE(ADCS_SetMode(ADCS_MODE_DETUMBLE));
    REQUIRE(Payload_ProcessCommand(PL_CMD_INIT, 0) == PL_OK);
    REQUIRE(Payload_ProcessCommand(PL_CMD_START, 0) == PL_OK);
    start = xTaskGetTickCount();
    do {
        vTaskDelay(pdMS_TO_TICKS(20));
        REQUIRE(observe(&out) && out.mode == MODE_NOMINAL && !out.faults.count);
        REQUIRE((TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(1000));
    } while (out.payload.bytes_generated <= stopped_bytes);
    REQUIRE(Payload_ProcessCommand(PL_CMD_STOP, 0) == PL_OK); // Bound RAM use.
    REQUIRE(observe(&out) && out.payload.current_state == PL_STATE_STANDBY);
    REQUIRE(Archive_ReadRecord(payload_index, &retained) == ARCHIVE_OK &&
            retained.record_id == original.record_id && retained.length == original.length &&
            retained.crc == original.crc && memcmp(retained.payload, original.payload, original.length) == 0);
    printf("TEST: LIFECYCLE retained index=%u crc=%u length=%u.\n", (unsigned)payload_index, (unsigned)retained.crc, (unsigned)retained.length);
    const uint32_t final_bytes = out.payload.bytes_generated;
    for (unsigned sample = 0; sample < 3; ++sample) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        REQUIRE(observe(&out) && out.mode == MODE_NOMINAL && out.eps == EPS_MODE_NOMINAL &&
                out.deployed && !out.faults.count && (out.adcs == ADCS_MODE_DETUMBLE || out.adcs == ADCS_MODE_NOMINAL) &&
                out.payload.current_state == PL_STATE_STANDBY && out.payload.bytes_generated == final_bytes &&
                (out.power.ul_PDU_Register & 7U) == 7U && !out.power.xFaultFlags.safe_mode_active);
        state("complete", &out);
    }
    const unsigned stack_min = (unsigned)uxTaskGetStackHighWaterMark(NULL);
    REQUIRE(stack_min >= 768U && stack_min <= 4096U); // IDF reports bytes, not words.
    printf("TEST: LIFECYCLE stack_free_min_bytes=%u.\n", stack_min);
    printf("TEST: LIFECYCLE finished.\n");
    return true;
}

void __wrap_vSolarHarvest_Task(void *argument) {
    if (argument != NULL) failed(__LINE__);
    else {
        printf("TEST: LIFECYCLE profile=eps_service_chain.\n");
        (void)run_scenario();
    }
    // Keep the named SOLAR_HARV handle lifetime-stable for RES_MON. No test
    // recovery/reset/clear, even after failure; outer runner owns restoration.
    for (;;) vTaskSuspend(NULL);
}

void __wrap_vCommandInjectionTask(void *argument) {
    (void)argument;
    printf("TEST: LIFECYCLE demo_commands=disabled.\n");
    vTaskDelete(NULL);
}
#endif

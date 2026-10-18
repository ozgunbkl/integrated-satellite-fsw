#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hk_service.h"
#include "payload_service.h"
#include "payload_sim.h"
#include "archive_service.h"
#include "adcs_service.h"
#include "eps_telemetry.h"
#include "fdir_service.h"
#include "time_service.h"
#include "yamcs_interface.h"
#include "freertos/task.h"
#include "eps_service.h"

static int mutex_slots[2];
static bool mutex_locked[2];
static unsigned mutex_count;
#define payload_locked mutex_locked[1]
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    assert(mutex_count < 2);
    return &mutex_slots[mutex_count++];
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks) {
    const unsigned index = mutex == &mutex_slots[0] ? 0 : 1;
    assert(mutex == &mutex_slots[index] && !mutex_locked[index]);
    assert(ticks == portMAX_DELAY || ticks == pdMS_TO_TICKS(10));
    mutex_locked[index] = true;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
    const unsigned index = mutex == &mutex_slots[0] ? 0 : 1;
    assert(mutex == &mutex_slots[index] && mutex_locked[index]);
    mutex_locked[index] = false;
    return pdTRUE;
}
bool EPS_RunIfPermitted(EpsOperation_t operation, SystemModeAction_t action, void *context) {
    assert(payload_locked && operation == EPS_OP_PAYLOAD);
    action(MODE_NOMINAL, context); // Startup reset test; live permission tested separately.
    return true;
}

EPS_Telemetry_t g_eps_telemetry;
SemaphoreHandle_t xEPSDataMutex;
static unsigned tick_reads, telemetry_sends, event_sends, simulation_calls;
static unsigned archive_writes, fault_reports;
static ArchiveStatus_t write_result = ARCHIVE_OK;

TickType_t xTaskGetTickCount(void) { ++tick_reads; return 0; }
uint32_t TIME_GetSeconds(void) { return 42; }
uint64_t TIME_GetMilliseconds(void) { static uint64_t now; now += 200; return now; }

void ADCS_GetTelemetry(ADCSTelemetry_t *telemetry) {
    memset(telemetry, 0, sizeof(*telemetry));
}

bool YAMCS_SendTelemetry(uint16_t apid, void *data, uint16_t len) {
    ++telemetry_sends;
    return true;
}

bool YAMCS_SendEvent(const char *format, ...) {
    ++event_sends;
    return true;
}

void PayloadSim_GenerateData(uint8_t *buffer, uint16_t length) {
    ++simulation_calls;
    memset(buffer, 0xA5, length);
}

ArchiveStatus_t Archive_WriteRecord(uint16_t id, const uint8_t *data, uint16_t len) {
    assert(id == REC_ID_PAYLOAD && len == 64 && data != NULL);
    for (uint16_t i = 0; i < len; ++i) assert(data[i] == 0xA5);
    ++archive_writes;
    return write_result;
}

void FDIR_ReportFault(FaultReport_t report) {
    assert(report.source == SRC_PAYLOAD && report.severity == FAULT_WARNING);
    assert(report.fault_code == FAULT_PAY_SENSOR_ERROR && report.timestamp == 42);
    ++fault_reports;
}

static void check_hk_table_empty(void) {
    for (int i = 0; i < HK_MAX_PARAMS; ++i) {
        hk_entry_t entry;
        memset(&entry, 0xFF, sizeof(entry));
        assert(HK_ReadParam((hk_param_id_t)i, &entry) == 0);
        assert(entry.value == 0 && entry.timestamp == 0 && entry.is_valid == 0);
        assert(HK_CheckLimits((hk_param_id_t)i) == -1);
    }
    uint8_t packet[HK_PACKET_SIZE];
    memset(packet, 0xFF, sizeof(packet));
    assert(HK_Serialize(packet, sizeof(packet)) == HK_PACKET_SIZE);
    for (unsigned i = 0; i < sizeof(packet); ++i) assert(packet[i] == 0);
}

static void check_payload_off(void) {
    PayloadTelemetry_t telemetry = Payload_GetTelemetry();
    assert(telemetry.current_state == PL_STATE_OFF);
    assert(telemetry.bytes_generated == 0 && telemetry.error_counter == 0);
    assert(telemetry.last_cmd_received == 0 && telemetry.data_rate == 0);
}

int main(void) {
    assert(HK_Init());
    // Seed real HK state first: zero-initialized C storage cannot make this pass.
    for (int i = 0; i < HK_MAX_PARAMS; ++i) {
        assert(HK_UpdateParam((hk_param_id_t)i, 1000U + i, 1234U + i) == 0);
        hk_entry_t entry;
        assert(HK_ReadParam((hk_param_id_t)i, &entry) == 0);
        assert(entry.value == 1000U + i && entry.timestamp == 1234U + i);
        assert(entry.is_valid == 1);
    }
    assert(HK_Init());
    check_hk_table_empty();
    assert(HK_Init());
    check_hk_table_empty();
    assert(tick_reads == 0 && telemetry_sends == 0 && event_sends == 0);

    Payload_Init();
    check_payload_off();
    Payload_Update();
    assert(simulation_calls == 0 && archive_writes == 0 && fault_reports == 0);

    // Test-only commands dirty each telemetry field; nothing reaches a real board.
    assert(Payload_ProcessCommand(PL_CMD_INIT, 0) == PL_OK);
    assert(Payload_ProcessCommand(PL_CMD_START, 0) == PL_OK);
    assert(Payload_ProcessCommand(PL_CMD_SET_RATE, 7) == PL_OK);
    Payload_Update();
    PayloadTelemetry_t dirty = Payload_GetTelemetry();
    assert(dirty.current_state == PL_STATE_ACTIVE && dirty.bytes_generated == 64);
    assert(dirty.last_cmd_received == PL_CMD_SET_RATE && dirty.data_rate == 7);

    write_result = ARCHIVE_ERR_FULL;
    Payload_Update();
    dirty = Payload_GetTelemetry();
    assert(dirty.current_state == PL_STATE_ERROR && dirty.error_counter == 1);
    assert(simulation_calls == 2 && archive_writes == 2 && fault_reports == 1);

    Payload_Init();
    check_payload_off();
    Payload_Update();
    assert(simulation_calls == 2 && archive_writes == 2 && fault_reports == 1);
    assert(tick_reads == 0 && telemetry_sends == 0 && event_sends == 0);

    puts("PASS: HK init clears seeded values/timestamps/validity and serialized data;\n"
         "      payload init clears seeded telemetry, stays OFF with no generation/write/fault/send.");
    return 0;
}

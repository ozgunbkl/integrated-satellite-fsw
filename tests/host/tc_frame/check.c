/* Host checks for the real command processor/router, with mocked RTOS/services. */
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "satellite_types.h"
#include "tc_proc.h"
#include "cdhs_router.h"
#include "ccsds_packet.h"
#include "fdir_service.h"
#include "watchdog.h"
#include "commands.h"

typedef struct {
    TelecommandPacket_t packet;
    bool calculate_crc;
    bool route;
} CheckCase;

static CheckCase cases[16];
static unsigned case_count, case_index, crc_calls, route_calls, apid_calls;
static unsigned expected_crc_calls, expected_route_calls;
static uint16_t last_payload_length;
static jmp_buf done;
QueueHandle_t xCommandQueue;

uint16_t __real_crc16_ccitt(const uint8_t *data, size_t length);
uint16_t __wrap_crc16_ccitt(const uint8_t *data, size_t length) {
    assert(length >= CCSDS_HEADER_SIZE + 1U);
    assert(length <= TELECOMMAND_MAX_SIZE - CCSDS_CRC_SIZE);
    crc_calls++;
    return __real_crc16_ccitt(data, length);
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *out, TickType_t wait) {
    (void)queue;
    (void)wait;
    assert(case_index < case_count);
    memcpy(out, &cases[case_index++].packet, sizeof(TelecommandPacket_t));
    return pdPASS;
}

void watchdog_pet(WatchdogTaskID_t task_id) {
    assert(task_id == WDT_TASK_CMD_PROC);
    CheckCase *item = &cases[case_index - 1U];
    expected_crc_calls += item->calculate_crc;
    expected_route_calls += item->route;
    assert(crc_calls == expected_crc_calls);
    assert(route_calls == expected_route_calls);
    assert(apid_calls == expected_route_calls);
    if (item->route) {
        assert(last_payload_length == item->packet.length - CCSDS_HEADER_SIZE - CCSDS_CRC_SIZE);
    }
    if (case_index == case_count) longjmp(done, 1);
}

uint16_t CCSDS_GetAPID(const uint8_t *buffer) {
    apid_calls++;
    return (((uint16_t)buffer[0] << 8) | buffer[1]) & 0x07FFU;
}

void FDIR_ReportFault(FaultReport_t fault) { (void)fault; }
uint32_t TIME_GetSeconds(void) { return 0; }
uint64_t TIME_GetMilliseconds(void) { return 1234; }
static unsigned recovery_commands;
void RECOVERY_ProcessCommand(const uint8_t *payload, uint16_t length, uint32_t now) {
    assert(payload[0] == FDIR_CMD_REARM_BOOT && length == 6 && now == 1234);
    ++recovery_commands;
}

#define MOCK_HANDLER(name) \
    void name(const uint8_t *payload, uint16_t length) { \
        (void)payload; route_calls++; last_payload_length = length; \
    }
MOCK_HANDLER(ADCS_ProcessCommand)
MOCK_HANDLER(EPS_ProcessCommand)
MOCK_HANDLER(HK_ProcessCommand)
MOCK_HANDLER(ARCHIVE_ProcessCommand)
MOCK_HANDLER(FDIR_ProcessCommand)
MOCK_HANDLER(PAYLOAD_ProcessCommandWrapper)

static void add_bad_length(uint16_t length) {
    cases[case_count++].packet.length = length;
}

static void add_frame(uint16_t apid, uint16_t length, bool bad_crc) {
    CheckCase *item = &cases[case_count++];
    item->packet.length = length;
    item->packet.raw_data[0] = (uint8_t)(apid >> 8);
    item->packet.raw_data[1] = (uint8_t)apid;
    item->packet.raw_data[CCSDS_HEADER_SIZE] = 0x30;
    item->packet.packet_crc = __real_crc16_ccitt(item->packet.raw_data, length - CCSDS_CRC_SIZE);
    if (bad_crc) item->packet.packet_crc ^= 1U;
    item->packet.raw_data[length - 2U] = (uint8_t)(item->packet.packet_crc >> 8);
    item->packet.raw_data[length - 1U] = (uint8_t)item->packet.packet_crc;
    item->calculate_crc = true;
    item->route = !bad_crc;
}

int main(void) {
    const uint16_t bad_lengths[] = {0, 1, 2, 14, 15, 16, 65, UINT16_MAX};
    for (unsigned i = 0; i < sizeof(bad_lengths) / sizeof(bad_lengths[0]); i++) {
        add_bad_length(bad_lengths[i]);
    }
    add_frame(APID_FDIR, TELECOMMAND_MIN_SIZE, false);
    add_frame(APID_ARCHIVE, 19, false);
    add_frame(APID_ADCS, TELECOMMAND_MAX_SIZE, false);
    add_frame(APID_EPS, 18, true);
    add_frame(APID_EPS, 18, false);
    cases[case_count - 1].packet.raw_data[17] ^= 1U;
    cases[case_count - 1].route = false;
    add_frame(APID_EPS, 18, false);
    cases[case_count - 1].packet.packet_crc ^= 1U;
    cases[case_count - 1].route = false;
    add_frame(APID_EPS, 18, false);
    cases[case_count - 1].packet.raw_data[CCSDS_HEADER_SIZE] ^= 1U;
    cases[case_count - 1].route = false;
    if (setjmp(done) == 0) vCommandProcessorTask(NULL);

    unsigned prior_routes = route_calls;
    unsigned prior_apids = apid_calls;
    CDHS_RoutePacket(NULL, 19);
    for (unsigned i = 0; i < sizeof(bad_lengths) / sizeof(bad_lengths[0]); i++) {
        CDHS_RoutePacket(cases[0].packet.raw_data, bad_lengths[i]);
    }
    assert(route_calls == prior_routes);
    assert(apid_calls == prior_apids);
    uint8_t recovery_frame[CCSDS_HEADER_SIZE + 6 + CCSDS_CRC_SIZE] = {0};
    recovery_frame[1] = APID_FDIR;
    recovery_frame[CCSDS_HEADER_SIZE] = FDIR_CMD_REARM_BOOT;
    CDHS_RoutePacket(recovery_frame, sizeof(recovery_frame));
    assert(recovery_commands == 1 && route_calls == prior_routes && apid_calls == prior_apids + 1);
    puts("PASS: 8 invalid lengths rejected before CRC, min/max frames accepted, bad CRC rejected,");
    puts("      CRC excluded from subsystem data, watchdog runs, invalid direct routes rejected.");
    return 0;
}

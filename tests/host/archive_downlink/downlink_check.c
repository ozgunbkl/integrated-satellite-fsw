/* Real archive/CRC code; UDP is replaced by a synchronous packet capture. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "archive_service.h"
#include "commands.h"
#include "ccsds_packet.h"
#include "fdir_service.h"
#include "yamcs_interface.h"

static uint8_t captured[ARCHIVE_TM_HEADER_SIZE + MAX_RECORD_PAYLOAD];
static uint16_t captured_length, captured_apid;
static unsigned send_calls, fault_calls;
static bool send_succeeds = true;
static FaultReport_t last_fault;

bool YAMCS_SendTelemetry(uint16_t apid, void *data, uint16_t length) {
    assert(data != NULL && length <= sizeof(captured));
    captured_apid = apid;
    captured_length = length;
    memcpy(captured, data, length);
    send_calls++;
    return send_succeeds;
}

void FDIR_ReportFault(FaultReport_t fault) {
    last_fault = fault;
    fault_calls++;
}
uint32_t TIME_GetSeconds(void) { return 123; }

static void fixture(void) {
    assert(Archive_Init());
    assert(Archive_Reset() == ARCHIVE_OK);
    send_calls = fault_calls = 0;
    send_succeeds = true;
    captured_length = 0;
}

static void request(uint16_t index) {
    const uint8_t command[] = {ARCHIVE_CMD_GET_BY_INDEX,
                               (uint8_t)(index >> 8), (uint8_t)index};
    ARCHIVE_ProcessCommand(command, sizeof(command));
}

static uint16_t read_be(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void check_packet(uint16_t index, uint16_t id, const uint8_t *payload,
                         uint16_t length, uint16_t crc) {
    assert(captured_apid == APID_ARCHIVE);
    assert(captured_length == 9U + length);
    assert(captured[0] == 1);
    assert(read_be(&captured[1]) == index);
    assert(read_be(&captured[3]) == id);
    assert(read_be(&captured[5]) == length);
    assert(read_be(&captured[7]) == crc);
    assert(memcmp(&captured[9], payload, length) == 0);
}

int main(void) {
    const uint8_t vector[] = "123456789";
    const uint8_t empty[] = {0};
    ArchiveRecord_t record;
    assert(utils_crc16(vector, 9) == 0x29B1);
    assert(utils_crc16(NULL, 0) == 0xFFFF);
    assert(utils_crc16(NULL, 1) == 0);

    fixture();
    request(0);
    assert(send_calls == 0 && Archive_GetUsage() == 0);
    assert(Archive_WriteRecord(REC_ID_HK, empty, 0) == ARCHIVE_OK);
    assert(Archive_WriteRecord(0x1234, vector, 9) == ARCHIVE_OK);
    const uint16_t usage = Archive_GetUsage();
    request(1);
    // Golden packet checks BE encoding and excludes struct padding/unused bytes.
    const uint8_t expected[] = {1, 0, 1, 0x12, 0x34, 0, 9, 0x29, 0xB1,
                               '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    assert(send_calls == 1 && captured_length == sizeof(expected));
    assert(memcmp(captured, expected, sizeof(expected)) == 0);
    check_packet(1, 0x1234, vector, 9, 0x29B1);
    assert(Archive_GetUsage() == usage);
    assert(Archive_ReadRecord(1, &record) == ARCHIVE_OK);
    assert(record.record_id == 0x1234 && record.length == 9);
    assert(memcmp(record.payload, vector, 9) == 0);

    send_succeeds = false;
    request(1);
    assert(send_calls == 2 && Archive_GetUsage() == usage);
    assert(Archive_ReadRecord(1, &record) == ARCHIVE_OK);
    assert(memcmp(record.payload, vector, 9) == 0);
    send_succeeds = true;
    request(1);
    assert(send_calls == 3 && Archive_GetUsage() == usage);
    assert(memcmp(captured, expected, sizeof(expected)) == 0);

    request(0);
    check_packet(0, REC_ID_HK, empty, 0, 0xFFFF);

    unsigned before_sends = send_calls;
    const uint8_t short_cmd[] = {ARCHIVE_CMD_GET_BY_INDEX, 0};
    const uint8_t long_cmd[] = {ARCHIVE_CMD_GET_BY_INDEX, 0, 0, 0};
    ARCHIVE_ProcessCommand(NULL, 3);
    ARCHIVE_ProcessCommand(empty, 0);
    ARCHIVE_ProcessCommand(short_cmd, sizeof(short_cmd));
    ARCHIVE_ProcessCommand(long_cmd, sizeof(long_cmd));
    request(2);
    request(32768);
    request(65535);
    assert(send_calls == before_sends && Archive_GetUsage() == usage);

    fixture();
    uint8_t maximum[MAX_RECORD_PAYLOAD];
    for (size_t i = 0; i < sizeof(maximum); i++) maximum[i] = (uint8_t)i;
    const size_t capacity = ARCHIVE_SIZE_BYTES / sizeof(ArchiveRecord_t);
    for (size_t index = 0; index < capacity; index++) {
        assert(Archive_WriteRecord(0xABCD, maximum, sizeof(maximum)) == ARCHIVE_OK);
    }
    request((uint16_t)(capacity - 1));
    check_packet((uint16_t)(capacity - 1), 0xABCD, maximum, sizeof(maximum),
                 utils_crc16(maximum, sizeof(maximum)));
    assert(captured_length == 137);
    assert(Archive_GetUsage() == capacity * sizeof(ArchiveRecord_t));

    fixture();
    assert(Archive_WriteRecord(REC_ID_HK, vector, 9) == ARCHIVE_OK);
    Archive_CorruptMemoryForTest((uint16_t)offsetof(ArchiveRecord_t, payload));
    request(0);
    assert(send_calls == 0 && fault_calls == 1);
    assert(last_fault.fault_code == FAULT_ARC_CRC_FAIL);
    assert(Archive_GetUsage() == sizeof(ArchiveRecord_t));

    fixture();
    assert(Archive_WriteRecord(REC_ID_HK, maximum, 126) == ARCHIVE_OK);
    Archive_CorruptMemoryForTest((uint16_t)offsetof(ArchiveRecord_t, length));
    request(0); // 126 XOR 255 = 129: must not read/send beyond the payload.
    assert(send_calls == 0 && fault_calls == 1);
    assert(last_fault.fault_code == FAULT_ARC_CRC_FAIL);

    fixture();
    assert(Archive_WriteRecord(REC_ID_HK, vector, 9) == ARCHIVE_OK);
    Archive_CorruptMemoryForTest((uint16_t)offsetof(ArchiveRecord_t, crc));
    request(0);
    assert(send_calls == 0 && fault_calls == 1);

    puts("PASS: archive CRC golden vector, portable TM bytes, lengths 0/128, last index,");
    puts("      invalid/corrupt records rejected, success/failure/retry retain stored records.");
    return 0;
}

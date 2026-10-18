/* Host checks for archive reads; corruption is injected into local test RAM. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "archive_service.h"
#include "fdir_service.h"
#include "yamcs_interface.h"

static unsigned crc_calls, fault_calls;
static FaultReport_t last_fault;

uint16_t __real_utils_crc16(const uint8_t *data, size_t length);
uint16_t __wrap_utils_crc16(const uint8_t *data, size_t length) {
    assert(length <= MAX_RECORD_PAYLOAD);
    crc_calls++;
    return __real_utils_crc16(data, length);
}

void FDIR_ReportFault(FaultReport_t fault) {
    fault_calls++;
    last_fault = fault;
}
uint32_t TIME_GetSeconds(void) { return 123; }

bool YAMCS_SendTelemetry(uint16_t apid, void *data, uint16_t length) {
    (void)apid;
    (void)data;
    (void)length;
    assert(!"Read-only checks must not send telemetry");
    return false;
}

static void seed_record(uint16_t length) {
    const uint8_t payload[MAX_RECORD_PAYLOAD] = {0xA5, 0x12, 0x34};
    assert(Archive_Reset() == ARCHIVE_OK);
    assert(Archive_WriteRecord(REC_ID_HK, payload, length) == ARCHIVE_OK);
}

static void expect_bad_length(void) {
    ArchiveRecord_t record;
    unsigned before_crc = crc_calls;
    unsigned before_faults = fault_calls;
    uint16_t before_usage = Archive_GetUsage();
    assert(Archive_ReadRecord(0, &record) == ARCHIVE_ERR_CRC);
    assert(crc_calls == before_crc);
    assert(fault_calls == before_faults + 1U);
    assert(last_fault.source == SRC_ARCHIVE);
    assert(last_fault.severity == FAULT_CRITICAL);
    assert(last_fault.fault_code == FAULT_ARC_CRC_FAIL);
    assert(Archive_GetUsage() == before_usage);
}

int main(void) {
    ArchiveRecord_t record;
    assert(Archive_Init());
    assert(Archive_ReadRecord(0, &record) == ARCHIVE_ERR_NOT_FOUND);
    assert(Archive_ReadRecord(0, NULL) == ARCHIVE_ERR_PARAM);

    seed_record(3);
    assert(Archive_ReadRecord(0, &record) == ARCHIVE_OK);
    assert(record.length == 3);
    assert(record.payload[0] == 0xA5 && record.payload[1] == 0x12 && record.payload[2] == 0x34);
    unsigned before_crc = crc_calls;
    unsigned before_faults = fault_calls;
    for (uint32_t index = 1; index <= UINT16_MAX; index++) {
        assert(Archive_ReadRecord((uint16_t)index, &record) == ARCHIVE_ERR_NOT_FOUND);
    }
    assert(crc_calls == before_crc);
    assert(fault_calls == before_faults);

    assert(Archive_Reset() == ARCHIVE_OK);
    const size_t capacity = ARCHIVE_SIZE_BYTES / sizeof(ArchiveRecord_t);
    const uint8_t sample[] = {1, 2, 3};
    for (size_t index = 0; index < capacity; index++) {
        assert(Archive_WriteRecord((uint16_t)index, sample, sizeof(sample)) == ARCHIVE_OK);
    }
    assert(Archive_ReadRecord((uint16_t)(capacity - 1U), &record) == ARCHIVE_OK);
    assert(record.record_id == capacity - 1U);
    assert(Archive_ReadRecord((uint16_t)capacity, &record) == ARCHIVE_ERR_NOT_FOUND);
    assert(Archive_WriteRecord(REC_ID_HK, sample, sizeof(sample)) == ARCHIVE_ERR_FULL);

    seed_record(0);
    assert(Archive_ReadRecord(0, &record) == ARCHIVE_OK);
    seed_record(MAX_RECORD_PAYLOAD);
    assert(Archive_ReadRecord(0, &record) == ARCHIVE_OK);
    assert(record.length == MAX_RECORD_PAYLOAD);

    // Flipping 126's low byte produces 129: just beyond the payload limit.
    seed_record(126);
    Archive_CorruptMemoryForTest((uint16_t)offsetof(ArchiveRecord_t, length));
    expect_bad_length();

    seed_record(0);
    Archive_CorruptMemoryForTest((uint16_t)offsetof(ArchiveRecord_t, length));
    Archive_CorruptMemoryForTest((uint16_t)(offsetof(ArchiveRecord_t, length) + 1U));
    expect_bad_length(); // 65535

    seed_record(MAX_RECORD_PAYLOAD);
    Archive_CorruptMemoryForTest((uint16_t)(offsetof(ArchiveRecord_t, length) + 1U));
    expect_bad_length(); // 65408

    seed_record(3);
    Archive_CorruptMemoryForTest((uint16_t)offsetof(ArchiveRecord_t, payload));
    before_crc = crc_calls;
    before_faults = fault_calls;
    assert(Archive_ReadRecord(0, &record) == ARCHIVE_ERR_CRC);
    assert(crc_calls == before_crc + 1U);
    assert(fault_calls == before_faults + 1U);

    puts("PASS: empty/null reads, all 65535 absent indices, last/full capacity boundaries,");
    puts("      payload lengths 0/128, corrupt lengths rejected before checksum, corrupt payload detected.");
    return 0;
}

/* Actual archive code: deterministic failures plus parallel producers/readers/wipes. */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "archive_service.h"
#include "commands.h"
#include "ccsds_packet.h"
#include "fdir_service.h"
#include "yamcs_interface.h"
#include "mutex_mock.h"

static atomic_uint full_faults, crc_faults, saved, dropped, reads;
static bool reset_on_fault, reset_on_send;
static uint8_t sent[ARCHIVE_TM_HEADER_SIZE + MAX_RECORD_PAYLOAD];
static uint16_t sent_length;
static pthread_barrier_t start;

int __wrap_printf(const char *format, ...) { assert(!ArchiveMock_LockHeld()); return 0; }
uint32_t TIME_GetSeconds(void) { assert(!ArchiveMock_LockHeld()); return 123; }
void FDIR_ReportFault(FaultReport_t fault) {
    assert(!ArchiveMock_LockHeld() && fault.source == SRC_ARCHIVE);
    if (fault.fault_code == FAULT_ARC_FULL) {
        assert(fault.severity == FAULT_WARNING); ++full_faults;
    } else if (fault.fault_code == FAULT_ARC_CRC_FAIL) {
        assert(fault.severity == FAULT_CRITICAL);
        ++crc_faults;
    } else {
        assert(fault.fault_code == FAULT_ARC_BAD_KEY && fault.severity == FAULT_WARNING);
    }
    if (reset_on_fault) { reset_on_fault = false; assert(Archive_Reset() == ARCHIVE_OK); }
}
bool YAMCS_SendTelemetry(uint16_t apid, void *data, uint16_t length) {
    assert(!ArchiveMock_LockHeld() && apid == APID_ARCHIVE && length <= sizeof(sent));
    if (reset_on_send) {
        reset_on_send = false;
        assert(Archive_Reset() == ARCHIVE_OK);
        const uint8_t replacement[] = {0xEE};
        assert(Archive_WriteRecord(REC_ID_PAYLOAD, replacement, sizeof(replacement)) == ARCHIVE_OK);
    }
    memcpy(sent, data, length); sent_length = length;
    return true;
}
static void get_zero(void) {
    const uint8_t command[] = {ARCHIVE_CMD_GET_BY_INDEX, 0, 0};
    ARCHIVE_ProcessCommand(command, sizeof(command));
}
static void wipe_command(uint8_t key, uint16_t length) {
    const uint8_t command[] = {ARCHIVE_CMD_WIPE_ALL, key, 0};
    ARCHIVE_ProcessCommand(command, length);
}
static void unchanged(ArchiveRecord_t *record, const ArchiveRecord_t *before) {
    assert(memcmp(record, before, sizeof(*record)) == 0);
}
static void *contended(void *unused) {
    const uint8_t sample[] = {1};
    ArchiveRecord_t record;
    assert(Archive_WriteRecord(1, sample, 1) == ARCHIVE_ERR_UNAVAILABLE);
    assert(Archive_ReadRecord(0, &record) == ARCHIVE_ERR_UNAVAILABLE);
    assert(Archive_Reset() == ARCHIVE_ERR_UNAVAILABLE);
    assert(Archive_GetUsage() == ARCHIVE_USAGE_UNAVAILABLE);
    return NULL;
}
static void barrier(void) {
    const int result = pthread_barrier_wait(&start);
    assert(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
}
static void *producer(void *argument) {
    const unsigned producer_id = *(unsigned *)argument;
    barrier();
    for (unsigned i = 0; i < 15; ++i) {
        const uint16_t id = (uint16_t)(producer_id * 15 + i);
        uint8_t data[MAX_RECORD_PAYLOAD]; memset(data, (uint8_t)id, sizeof(data));
        ArchiveStatus_t result;
        do { result = Archive_WriteRecord(id, data, sizeof(data)); } while (result == ARCHIVE_ERR_UNAVAILABLE);
        if (result == ARCHIVE_OK) ++saved;
        else { assert(result == ARCHIVE_ERR_FULL); ++dropped; }
    }
    return NULL;
}
static void *mixed(void *argument) {
    const unsigned role = *(unsigned *)argument;
    barrier();
    for (unsigned i = 0; i < 1000; ++i) {
        const uint8_t pattern = (uint8_t)i;
        uint8_t data[MAX_RECORD_PAYLOAD]; memset(data, pattern, sizeof(data));
        ArchiveStatus_t result;
        if (role == 0) {
            result = Archive_WriteRecord(pattern, data, sizeof(data));
            assert(result == ARCHIVE_OK || result == ARCHIVE_ERR_FULL || result == ARCHIVE_ERR_UNAVAILABLE);
        } else if (role == 1) {
            ArchiveRecord_t record;
            result = Archive_ReadRecord((uint16_t)(i % 31), &record);
            assert(result == ARCHIVE_OK || result == ARCHIVE_ERR_NOT_FOUND || result == ARCHIVE_ERR_UNAVAILABLE);
            if (result == ARCHIVE_OK) {
                ++reads;
                assert(record.length == MAX_RECORD_PAYLOAD);
                for (unsigned j = 0; j < record.length; ++j) assert(record.payload[j] == (uint8_t)record.record_id);
            }
        } else {
            result = Archive_Reset();
            assert(result == ARCHIVE_OK || result == ARCHIVE_ERR_UNAVAILABLE);
        }
        sched_yield();
    }
    return NULL;
}
int main(void) {
    const uint8_t sample[] = {1, 2, 3};
    ArchiveRecord_t record, before;
    memset(&record, 0xA5, sizeof(record)); before = record;
    assert(Archive_WriteRecord(1, sample, sizeof(sample)) == ARCHIVE_ERR_UNAVAILABLE);
    assert(Archive_ReadRecord(0, &record) == ARCHIVE_ERR_UNAVAILABLE); unchanged(&record, &before);
    assert(Archive_Reset() == ARCHIVE_ERR_UNAVAILABLE);
    assert(Archive_GetUsage() == ARCHIVE_USAGE_UNAVAILABLE);
    ArchiveMock_FailCreate(); assert(!Archive_Init()); assert(Archive_Init());
    assert(Archive_WriteRecord(REC_ID_HK, sample, sizeof(sample)) == ARCHIVE_OK);
    assert(Archive_Init() && Archive_GetUsage() == sizeof(record)); // Never an implicit wipe.

    ArchiveMock_FailTake(); assert(Archive_WriteRecord(1, sample, 3) == ARCHIVE_ERR_UNAVAILABLE);
    ArchiveMock_FailTake(); assert(Archive_ReadRecord(0, &record) == ARCHIVE_ERR_UNAVAILABLE); unchanged(&record, &before);
    ArchiveMock_FailTake(); assert(Archive_Reset() == ARCHIVE_ERR_UNAVAILABLE);
    ArchiveMock_FailTake(); assert(Archive_GetUsage() == ARCHIVE_USAGE_UNAVAILABLE);
    assert(Archive_GetUsage() == sizeof(record));
    assert(Archive_WriteRecord(1, NULL, 0) == ARCHIVE_ERR_PARAM);
    assert(Archive_WriteRecord(1, sample, MAX_RECORD_PAYLOAD + 1) == ARCHIVE_ERR_PARAM);
    assert(Archive_ReadRecord(0, NULL) == ARCHIVE_ERR_PARAM);
    assert(Archive_ReadRecord(UINT16_MAX, &record) == ARCHIVE_ERR_NOT_FOUND); unchanged(&record, &before);

    pthread_t thread;
    ArchiveMock_Hold(); assert(pthread_create(&thread, NULL, contended, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0); ArchiveMock_Release();
    assert(Archive_GetUsage() == sizeof(record));
    wipe_command(0, 2); wipe_command(0xAA, 1); wipe_command(0xAA, 3);
    assert(Archive_GetUsage() == sizeof(record));
    ArchiveMock_FailTake(); wipe_command(0xAA, 2);
    assert(Archive_GetUsage() == sizeof(record));

    reset_on_send = true; get_zero(); // TX may wipe/rewrite; returned snapshot stays the old record.
    assert(sent_length == 12 && sent[3] == 0 && sent[4] == REC_ID_HK && sent[6] == 3);
    assert(memcmp(sent + ARCHIVE_TM_HEADER_SIZE, sample, 3) == 0);
    assert(Archive_ReadRecord(0, &record) == ARCHIVE_OK && record.record_id == REC_ID_PAYLOAD);
    memset(&record, 0xA5, sizeof(record)); before = record;
    Archive_CorruptMemoryForTest((uint16_t)offsetof(ArchiveRecord_t, payload));
    reset_on_fault = true;
    assert(Archive_ReadRecord(0, &record) == ARCHIVE_ERR_CRC); unchanged(&record, &before);
    assert(Archive_GetUsage() == 0 && crc_faults == 1); crc_faults = 0;

    const unsigned capacity = ARCHIVE_SIZE_BYTES / sizeof(record);
    for (unsigned i = 0; i < capacity; ++i) assert(Archive_WriteRecord((uint16_t)i, sample, 3) == ARCHIVE_OK);
    const unsigned faults_before = full_faults;
    for (unsigned i = 0; i < 10; ++i) assert(Archive_WriteRecord(99, sample, 3) == ARCHIVE_ERR_FULL);
    assert(full_faults == faults_before + 10 && Archive_GetUsage() == capacity * sizeof(record));
    for (unsigned i = 0; i < capacity; ++i) {
        assert(Archive_ReadRecord((uint16_t)i, &record) == ARCHIVE_OK && record.record_id == i);
        assert(record.length == 3 && memcmp(record.payload, sample, 3) == 0);
    }
    reset_on_fault = true;
    assert(Archive_WriteRecord(99, sample, 3) == ARCHIVE_ERR_FULL && Archive_GetUsage() == 0);
    assert(Archive_WriteRecord(1, sample, 3) == ARCHIVE_OK);
    wipe_command(0xAA, 2); assert(Archive_GetUsage() == 0);

    pthread_t threads[4]; unsigned roles[] = {0, 1, 2, 3};
    assert(pthread_barrier_init(&start, NULL, 4) == 0);
    for (unsigned i = 0; i < 4; ++i) assert(pthread_create(&threads[i], NULL, producer, &roles[i]) == 0);
    for (unsigned i = 0; i < 4; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert(pthread_barrier_destroy(&start) == 0);
    assert(saved == capacity && dropped == 60 - capacity);
    bool seen[60] = {0};
    for (unsigned i = 0; i < capacity; ++i) {
        assert(Archive_ReadRecord((uint16_t)i, &record) == ARCHIVE_OK);
        assert(record.record_id < 60 && !seen[record.record_id]); seen[record.record_id] = true;
        for (unsigned j = 0; j < record.length; ++j) assert(record.payload[j] == record.record_id);
    }
    assert(Archive_Reset() == ARCHIVE_OK);
    uint8_t maximum[MAX_RECORD_PAYLOAD] = {0};
    assert(Archive_WriteRecord(0, maximum, sizeof(maximum)) == ARCHIVE_OK);
    assert(Archive_ReadRecord(0, &record) == ARCHIVE_OK); ++reads;
    assert(pthread_barrier_init(&start, NULL, 3) == 0);
    for (unsigned i = 0; i < 3; ++i) assert(pthread_create(&threads[i], NULL, mixed, &roles[i]) == 0);
    for (unsigned i = 0; i < 3; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert(pthread_barrier_destroy(&start) == 0 && crc_faults == 0);
    assert(Archive_GetUsage() <= capacity * sizeof(record));
    fprintf(stdout, "PASS: archive init/timeout/unchanged output/wipe guards/unlocked callbacks; "
            "60 parallel writes (%u saved), 3000 mixed read/write/wipe calls, %u validated snapshots.\n",
            (unsigned)saved, (unsigned)reads);
    return 0;
}

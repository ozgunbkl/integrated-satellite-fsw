/* Real archive, CRC and host mutex; no TC, UDP, card or ground writes. */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "archive_service.h"
#include "fdir_service.h"
#include "yamcs_interface.h"
#include "mutex_mock.h"

static unsigned faults;
static bool reset_on_fault;
static atomic_uint appended;
static pthread_barrier_t start;
static const uint8_t sample[] = "123456789";

int __wrap_printf(const char *format, ...) { assert(!ArchiveMock_LockHeld()); return 0; }
uint32_t TIME_GetSeconds(void) { assert(!ArchiveMock_LockHeld()); return 123; }
void FDIR_ReportFault(FaultReport_t fault) {
    assert(!ArchiveMock_LockHeld() && fault.source == SRC_ARCHIVE);
    assert(fault.fault_code == FAULT_ARC_CRC_FAIL || fault.fault_code == FAULT_ARC_FULL);
    ++faults;
    if (reset_on_fault) { reset_on_fault = false; assert(Archive_Reset() == ARCHIVE_OK); }
}
bool YAMCS_SendTelemetry(uint16_t apid, void *data, uint16_t length) {
    assert(!"Transfer service API must not send or infer delivery"); return false;
}
static bool same(const ArchiveTransfer_t *a, const ArchiveTransfer_t *b) {
    return a->boot_id == b->boot_id && a->transfer_id == b->transfer_id && a->request_id == b->request_id &&
           a->record_count == b->record_count && a->digest == b->digest;
}
static void seed(void) {
    assert(Archive_Reset() == ARCHIVE_OK);
    assert(Archive_WriteRecord(0x1234, sample, 0) == ARCHIVE_OK);
    assert(Archive_WriteRecord(0xABCD, sample, 9) == ARCHIVE_OK);
}
static void read_all(const ArchiveTransfer_t *transfer) {
    ArchiveRecord_t out;
    for (uint16_t i = 0; i < transfer->record_count; ++i)
        assert(Archive_ReadTransferRecord(transfer, i, &out) == ARCHIVE_OK);
}
static void check_wrong(ArchiveTransfer_t wrong, const ArchiveTransfer_t *right) {
    ArchiveRecord_t out, before;
    memset(&out, 0xA5, sizeof(out)); before = out;
    const uint16_t usage = Archive_GetUsage();
    assert(Archive_ReadTransferRecord(&wrong, 0, &out) == ARCHIVE_ERR_STALE);
    assert(memcmp(&out, &before, sizeof(out)) == 0);
    assert(Archive_ConfirmTransfer(&wrong) == ARCHIVE_ERR_STALE);
    assert(Archive_CancelTransfer(&wrong) == ARCHIVE_ERR_STALE);
    ArchiveTransfer_t current;
    assert(Archive_BeginTransfer(right->request_id, &current) == ARCHIVE_OK && same(right, &current));
    assert(Archive_GetUsage() == usage);
}
static void *append_one(void *argument) {
    const uint16_t id = *(uint16_t *)argument;
    const int b = pthread_barrier_wait(&start);
    assert(b == 0 || b == PTHREAD_BARRIER_SERIAL_THREAD);
    ArchiveStatus_t result;
    do { result = Archive_WriteRecord(id, sample, 9); } while (result == ARCHIVE_ERR_UNAVAILABLE);
    assert(result == ARCHIVE_OK); ++appended;
    return NULL;
}
static void timeout_checks(const ArchiveTransfer_t *transfer) {
    ArchiveTransfer_t out, before;
    ArchiveRecord_t record, record_before;
    memset(&out, 0xA5, sizeof(out)); before = out;
    memset(&record, 0xA5, sizeof(record)); record_before = record;
    const uint16_t usage = Archive_GetUsage();
    ArchiveMock_FailTake(); assert(Archive_BeginTransfer(999, &out) == ARCHIVE_ERR_UNAVAILABLE);
    assert(memcmp(&out, &before, sizeof(out)) == 0);
    ArchiveMock_FailTake(); assert(Archive_ReadTransferRecord(transfer, 0, &record) == ARCHIVE_ERR_UNAVAILABLE);
    assert(memcmp(&record, &record_before, sizeof(record)) == 0);
    ArchiveMock_FailTake(); assert(Archive_ConfirmTransfer(transfer) == ARCHIVE_ERR_UNAVAILABLE);
    ArchiveMock_FailTake(); assert(Archive_CancelTransfer(transfer) == ARCHIVE_ERR_UNAVAILABLE);
    ArchiveMock_FailTake(); assert(Archive_GetPendingTransfer(&out) == ARCHIVE_ERR_UNAVAILABLE);
    assert(memcmp(&out, &before, sizeof(out)) == 0);
    assert(Archive_GetUsage() == usage);
}
int main(void) {
    ArchiveTransfer_t transfer, out, before;
    ArchiveRecord_t record, record_before;
    memset(&out, 0xA5, sizeof(out)); before = out;
    memset(&record, 0xA5, sizeof(record)); record_before = record;
    assert(Archive_BeginTransfer(1, NULL) == ARCHIVE_ERR_PARAM);
    assert(Archive_GetPendingTransfer(NULL) == ARCHIVE_ERR_PARAM);
    assert(Archive_ReadTransferRecord(NULL, 0, &record) == ARCHIVE_ERR_PARAM);
    assert(Archive_ReadTransferRecord(&out, 0, NULL) == ARCHIVE_ERR_PARAM);
    assert(Archive_ConfirmTransfer(NULL) == ARCHIVE_ERR_PARAM && Archive_CancelTransfer(NULL) == ARCHIVE_ERR_PARAM);
    assert(Archive_BeginTransfer(1, &out) == ARCHIVE_ERR_UNAVAILABLE);
    assert(Archive_GetPendingTransfer(&out) == ARCHIVE_ERR_UNAVAILABLE);
    assert(Archive_ReadTransferRecord(&out, 0, &record) == ARCHIVE_ERR_UNAVAILABLE);
    assert(Archive_ConfirmTransfer(&out) == ARCHIVE_ERR_UNAVAILABLE && Archive_CancelTransfer(&out) == ARCHIVE_ERR_UNAVAILABLE);
    assert(memcmp(&out, &before, sizeof(out)) == 0 && memcmp(&record, &record_before, sizeof(record)) == 0);
    ArchiveMock_FailBootId(); assert(!Archive_Init());
    ArchiveMock_ZeroBootId(); assert(!Archive_Init());
    ArchiveMock_FailCreate(); assert(!Archive_Init());
    assert(Archive_Init());
    assert(Archive_GetPendingTransfer(&out) == ARCHIVE_ERR_NOT_FOUND && memcmp(&out, &before, sizeof(out)) == 0);
    assert(Archive_BeginTransfer(1, &out) == ARCHIVE_EMPTY && memcmp(&out, &before, sizeof(out)) == 0);

    seed();
    assert(Archive_BeginTransfer(0, &transfer) == ARCHIVE_OK);
    assert(transfer.boot_id == UINT64_C(0x0123456789ABCDEF) && transfer.transfer_id == 1 && transfer.record_count == 2);
    // Independent Python binascii.crc32 golden canonical byte stream (no struct padding).
    assert(transfer.digest == UINT32_C(0x417EE9BC));
    assert(Archive_BeginTransfer(0, &out) == ARCHIVE_OK && same(&out, &transfer));
    assert(Archive_GetPendingTransfer(&out) == ARCHIVE_OK && same(&out, &transfer));
    assert(Archive_BeginTransfer(1, &out) == ARCHIVE_ERR_BUSY);
    assert(Archive_Init()); // Startup repeat does not clear data or session.
    assert(Archive_ConfirmTransfer(&transfer) == ARCHIVE_ERR_NOT_READY);
    assert(Archive_ReadRecord(0, &record) == ARCHIVE_OK && Archive_ReadRecord(1, &record) == ARCHIVE_OK);
    assert(Archive_ConfirmTransfer(&transfer) == ARCHIVE_ERR_NOT_READY); // Legacy GET is not session proof.
    assert(Archive_WriteRecord(0xEEEE, sample, 9) == ARCHIVE_OK);
    assert(Archive_BeginTransfer(0, &out) == ARCHIVE_OK && same(&out, &transfer));
    ArchiveTransfer_t wrong = transfer; ++wrong.boot_id; check_wrong(wrong, &transfer);
    wrong = transfer; ++wrong.transfer_id; check_wrong(wrong, &transfer);
    wrong = transfer; ++wrong.request_id; check_wrong(wrong, &transfer);
    wrong = transfer; ++wrong.digest; check_wrong(wrong, &transfer);
    wrong = transfer; ++wrong.record_count; check_wrong(wrong, &transfer);
    assert(Archive_ReadTransferRecord(&transfer, 1, &record) == ARCHIVE_OK);
    assert(Archive_ReadTransferRecord(&transfer, 1, &record) == ARCHIVE_OK);
    assert(Archive_ConfirmTransfer(&transfer) == ARCHIVE_ERR_NOT_READY); // Duplicate/out-of-order read isn't all records.
    memset(&record, 0xA5, sizeof(record)); record_before = record;
    assert(Archive_ReadTransferRecord(&transfer, 2, &record) == ARCHIVE_ERR_NOT_FOUND);
    assert(Archive_ReadTransferRecord(&transfer, UINT16_MAX, &record) == ARCHIVE_ERR_NOT_FOUND);
    assert(memcmp(&record, &record_before, sizeof(record)) == 0);
    timeout_checks(&transfer);
    assert(Archive_ReadTransferRecord(&transfer, 0, &record) == ARCHIVE_OK);
    assert(Archive_ConfirmTransfer(&transfer) == ARCHIVE_OK);
    assert(Archive_GetPendingTransfer(&out) == ARCHIVE_ERR_NOT_FOUND);
    assert(Archive_GetUsage() == sizeof(record));
    assert(Archive_ReadRecord(0, &record) == ARCHIVE_OK && record.record_id == 0xEEEE);
    assert(Archive_ConfirmTransfer(&transfer) == ARCHIVE_OK && Archive_GetUsage() == sizeof(record));
    assert(Archive_ReadTransferRecord(&transfer, 0, &record) == ARCHIVE_ERR_STALE);
    assert(Archive_CancelTransfer(&transfer) == ARCHIVE_ERR_STALE);
    assert(Archive_BeginTransfer(0, &out) == ARCHIVE_ERR_STALE);

    ArchiveTransfer_t second;
    assert(Archive_BeginTransfer(1, &second) == ARCHIVE_OK && second.transfer_id > transfer.transfer_id);
    assert(Archive_ConfirmTransfer(&transfer) == ARCHIVE_OK && Archive_GetUsage() == sizeof(record));
    assert(Archive_CancelTransfer(&second) == ARCHIVE_OK && Archive_CancelTransfer(&second) == ARCHIVE_OK);
    assert(Archive_ConfirmTransfer(&second) == ARCHIVE_ERR_STALE && Archive_GetUsage() == sizeof(record));
    assert(Archive_BeginTransfer(1, &out) == ARCHIVE_ERR_STALE);
    assert(Archive_BeginTransfer(2, &out) == ARCHIVE_OK);
    assert(Archive_ConfirmTransfer(&transfer) == ARCHIVE_ERR_STALE); // Old receipt was superseded by cancel.
    assert(Archive_Reset() == ARCHIVE_OK && Archive_ConfirmTransfer(&out) == ARCHIVE_ERR_STALE);
    assert(Archive_WriteRecord(0xEEEE, sample, 9) == ARCHIVE_OK);
    assert(Archive_BeginTransfer(2, &second) == ARCHIVE_OK && second.transfer_id > out.transfer_id);
    assert(Archive_ReadTransferRecord(&out, 0, &record) == ARCHIVE_ERR_STALE);
    assert(Archive_ConfirmTransfer(&out) == ARCHIVE_ERR_STALE && Archive_GetUsage() == sizeof(record));

    seed(); assert(Archive_BeginTransfer(3, &transfer) == ARCHIVE_OK); read_all(&transfer);
    Archive_CorruptMemoryForTest((uint16_t)offsetof(ArchiveRecord_t, record_id)); // Per-record payload CRC still matches.
    const unsigned old_faults = faults;
    assert(Archive_ConfirmTransfer(&transfer) == ARCHIVE_ERR_CRC && faults == old_faults + 1);
    assert(Archive_GetUsage() == 2 * sizeof(record));
    assert(Archive_CancelTransfer(&transfer) == ARCHIVE_OK);
    seed(); Archive_CorruptMemoryForTest((uint16_t)offsetof(ArchiveRecord_t, length));
    memset(&out, 0xA5, sizeof(out)); before = out;
    assert(Archive_BeginTransfer(4, &out) == ARCHIVE_ERR_CRC && memcmp(&out, &before, sizeof(out)) == 0);
    assert(Archive_GetUsage() == 2 * sizeof(record));
    seed(); assert(Archive_BeginTransfer(4, &transfer) == ARCHIVE_OK);
    Archive_CorruptMemoryForTest((uint16_t)(sizeof(record) + offsetof(ArchiveRecord_t, payload)));
    memset(&record, 0xA5, sizeof(record)); record_before = record;
    reset_on_fault = true;
    assert(Archive_ReadTransferRecord(&transfer, 1, &record) == ARCHIVE_ERR_CRC);
    assert(memcmp(&record, &record_before, sizeof(record)) == 0 && Archive_GetUsage() == 0);
    assert(Archive_ConfirmTransfer(&transfer) == ARCHIVE_ERR_STALE);

    seed(); assert(Archive_BeginTransfer(5, &transfer) == ARCHIVE_OK); read_all(&transfer);
    uint16_t ids[] = {10, 11, 12, 13}; pthread_t threads[4];
    assert(pthread_barrier_init(&start, NULL, 5) == 0);
    for (unsigned i = 0; i < 4; ++i) assert(pthread_create(&threads[i], NULL, append_one, &ids[i]) == 0);
    int b = pthread_barrier_wait(&start); assert(b == 0 || b == PTHREAD_BARRIER_SERIAL_THREAD);
    ArchiveStatus_t result;
    do { result = Archive_ConfirmTransfer(&transfer); } while (result == ARCHIVE_ERR_UNAVAILABLE);
    assert(result == ARCHIVE_OK);
    for (unsigned i = 0; i < 4; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert(pthread_barrier_destroy(&start) == 0 && appended == 4 && Archive_GetUsage() == 4 * sizeof(record));
    bool seen[4] = {0};
    for (uint16_t i = 0; i < 4; ++i) {
        assert(Archive_ReadRecord(i, &record) == ARCHIVE_OK && record.record_id >= 10 && record.record_id <= 13);
        assert(!seen[record.record_id - 10]); seen[record.record_id - 10] = true;
    }
    assert(Archive_ConfirmTransfer(&transfer) == ARCHIVE_OK && Archive_GetUsage() == 4 * sizeof(record));

    assert(Archive_Reset() == ARCHIVE_OK);
    const unsigned capacity = ARCHIVE_SIZE_BYTES / sizeof(record);
    uint8_t maximum[MAX_RECORD_PAYLOAD]; memset(maximum, 0xA5, sizeof(maximum));
    for (unsigned i = 0; i < capacity; ++i) assert(Archive_WriteRecord((uint16_t)i, maximum, sizeof(maximum)) == ARCHIVE_OK);
    assert(Archive_BeginTransfer(UINT32_MAX, &transfer) == ARCHIVE_OK && transfer.record_count == capacity);
    assert(Archive_WriteRecord(0xFFFF, sample, 9) == ARCHIVE_ERR_FULL);
    read_all(&transfer);
    assert(Archive_ConfirmTransfer(&transfer) == ARCHIVE_OK && Archive_GetUsage() == 0);
    assert(Archive_ConfirmTransfer(&transfer) == ARCHIVE_OK && Archive_GetUsage() == 0);
    puts("PASS: transfer begin/retry/busy/empty, canonical CRC32 golden, all-field identity guards,");
    puts("      full-read gate, timeout/corruption retention, cancel/reset/old ACK rejection,");
    puts("      prefix-only compaction, ACK retry idempotence, concurrent append, 30-slot confirmation.");
    return 0;
}

#include "archive_service.h"
#include "archive_port.h"
#include "commands.h"
#include "fdir_service.h"
#include "time_service.h"
#include "utils.h"
#include "ccsds_packet.h"
#include "yamcs_interface.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

// RAM-only archive: contents are lost on reset (not non-volatile flash).
static uint8_t archive_memory[ARCHIVE_SIZE_BYTES];
static uint16_t write_index = 0;
static SemaphoreHandle_t archive_mutex;
static uint64_t boot_id;
static uint32_t next_transfer_id;
static bool transfer_active, receipt_valid, receipt_confirmed;
static ArchiveTransfer_t active_transfer, last_receipt;
static uint32_t read_bitmap;

_Static_assert(ARCHIVE_SIZE_BYTES / sizeof(ArchiveRecord_t) <= 32,
               "Transfer read bitmap must cover the archive capacity");

static bool archive_lock(void) {
    return archive_mutex != NULL &&
           xSemaphoreTake(archive_mutex, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool Archive_Init(void){
    // Startup-only, before scheduler consumers; init is not a runtime wipe API.
    if (archive_mutex != NULL) return true;
    uint64_t candidate;
    if (!Archive_PortCreateBootId(&candidate) || candidate == 0) return false;
    archive_mutex = xSemaphoreCreateMutex();
    if (archive_mutex == NULL) return false;
    boot_id = candidate;
    write_index = 0;
    memset(archive_memory, 0, ARCHIVE_SIZE_BYTES);
    return true;
}

ArchiveStatus_t Archive_Reset(void) {
    if (!archive_lock()) return ARCHIVE_ERR_UNAVAILABLE;
    // Explicit destructive maintenance only, never a periodic repair policy.
    // Bind the old CRC report before touching RAM; a concurrent new report or
    // fatal promotion must not be cleared by this operation's stale token.
    uint32_t crc_revision = 0;
    const bool crc_query_ok = FDIR_TryGetFaultRevision(SRC_ARCHIVE, FAULT_ARC_CRC_FAIL, &crc_revision);
    const bool crc_known = crc_query_ok && crc_revision != 0;
    write_index = 0;
    memset(archive_memory, 0, ARCHIVE_SIZE_BYTES);
    // Never rewind the boot/transfer IDs: old ACKs must not name a new RAM generation.
    transfer_active = receipt_valid = false;
    read_bitmap = 0;
    // Read back the empty software state while no writer can publish a record.
    // This is not ECC/physical RAM certification or reconstruction of lost data.
    uint8_t remaining = 0;
    for (size_t i = 0; i < sizeof(archive_memory); ++i) remaining |= archive_memory[i];
    const bool empty_verified = write_index == 0 && remaining == 0 &&
                                !transfer_active && !receipt_valid && read_bitmap == 0;
    // Lock order: archive -> MODE; this quiet API makes ONE zero-wait attempt,
    // no warning-counter mutex, callbacks, logging, flash writes or mode change.
    const bool crc_resolved = crc_known && empty_verified &&
        FDIR_TryResolveCriticalFault(SRC_ARCHIVE, FAULT_ARC_CRC_FAIL, crc_revision);
    xSemaphoreGive(archive_mutex);
    printf("ARCHIVE: Memory wiped. Storage 100%% available.\n");
    if (crc_known)
        printf("ARCHIVE: CRC maintenance %s; system mode and other faults unchanged.\n",
               crc_resolved ? "COMPLETE" : "DEFERRED");
    else if (!crc_query_ok)
        printf("ARCHIVE: CRC maintenance DEFERRED; fault ledger unavailable; existing faults retained.\n");
    return ARCHIVE_OK;
}

ArchiveStatus_t Archive_WriteRecord(uint16_t id, const uint8_t *data, uint16_t len){
    // 1. Parameter validation
    if(data == NULL || len > MAX_RECORD_PAYLOAD){
        return ARCHIVE_ERR_PARAM;
    }

    // Build privately: no shared state or callbacks while holding the archive lock.
    ArchiveRecord_t new_record = {0};
    new_record.record_id = id;
    new_record.length = len;
    memcpy(new_record.payload, data, len);
    new_record.crc = utils_crc16(new_record.payload, len);

    if (!archive_lock()) return ARCHIVE_ERR_UNAVAILABLE;
    // Drop the incoming record when full; never overwrite old records or retry here.
    if(write_index + sizeof(ArchiveRecord_t) > ARCHIVE_SIZE_BYTES){
        xSemaphoreGive(archive_mutex);
        FaultReport_t memory_fault = {
            .source = SRC_ARCHIVE,
            .severity = FAULT_WARNING,
            .fault_code = FAULT_ARC_FULL, // RAM archive full; existing per-attempt warning policy.
            .timestamp = TIME_GetSeconds()
        };
        FDIR_ReportFault(memory_fault);
        printf("ARCHIVE: Error! Storage full. Dropping packet.\n");
        return ARCHIVE_ERR_FULL;
    }

    // Publish bytes and the used-byte count as one protected transaction.
    memcpy(&archive_memory[write_index], &new_record, sizeof(ArchiveRecord_t));
    write_index += sizeof(ArchiveRecord_t);
    xSemaphoreGive(archive_mutex);

    return ARCHIVE_OK;

}

uint16_t Archive_GetUsage(void){
    if (!archive_lock()) return ARCHIVE_USAGE_UNAVAILABLE;
    const uint16_t usage = write_index;
    xSemaphoreGive(archive_mutex);
    return usage;
}


ArchiveStatus_t Archive_ReadRecord(uint16_t index, ArchiveRecord_t *out_record){
    // 1. Parameter validation
    if(out_record == NULL){
        return ARCHIVE_ERR_PARAM;
    }

    if (!archive_lock()) return ARCHIVE_ERR_UNAVAILABLE;
    // Validate the record index before multiplying it into a byte offset.
    const size_t record_size = sizeof(ArchiveRecord_t);
    if ((size_t)index >= sizeof(archive_memory) / record_size ||
        (size_t)index >= (size_t)write_index / record_size) {
        xSemaphoreGive(archive_mutex);
        return ARCHIVE_ERR_NOT_FOUND;
    }
    const size_t offset = (size_t)index * record_size;

    ArchiveRecord_t snapshot;
    memcpy(&snapshot, &archive_memory[offset], sizeof(snapshot));
    xSemaphoreGive(archive_mutex);

    // 4. Validate length first; short-circuiting prevents out-of-bounds CRC reads.
    if (snapshot.length > MAX_RECORD_PAYLOAD ||
        utils_crc16(snapshot.payload, snapshot.length) != snapshot.crc) {
        FaultReport_t archive_fault = {
            .source = SRC_ARCHIVE,
            .severity = FAULT_CRITICAL,
            .fault_code = FAULT_ARC_CRC_FAIL, // CRC Mismatch
            .timestamp = TIME_GetSeconds()
        };
        FDIR_ReportFault(archive_fault);
        
        return ARCHIVE_ERR_CRC; // Corruption detected!
    }

    memcpy(out_record, &snapshot, sizeof(snapshot));
    return ARCHIVE_OK;
}

static void archive_report_crc(void) {
    FaultReport_t fault = {.source = SRC_ARCHIVE, .severity = FAULT_CRITICAL,
        .fault_code = FAULT_ARC_CRC_FAIL, .timestamp = TIME_GetSeconds()};
    FDIR_ReportFault(fault);
}

static bool record_valid(const ArchiveRecord_t *record) {
    return record->length <= MAX_RECORD_PAYLOAD &&
           utils_crc16(record->payload, record->length) == record->crc;
}

static uint32_t digest_byte(uint32_t crc, uint8_t value) {
    crc ^= value;
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    return crc;
}
static uint32_t digest_u16(uint32_t crc, uint16_t value) {
    return digest_byte(digest_byte(crc, (uint8_t)(value >> 8)), (uint8_t)value);
}
// Requires the archive mutex. Bounded by the 30-slot RAM capacity; no callbacks.
static bool prefix_digest(uint16_t count, uint32_t *digest) {
    if (count == 0 || count > write_index / sizeof(ArchiveRecord_t)) return false;
    uint32_t crc = digest_u16(UINT32_MAX, count);
    for (uint16_t i = 0; i < count; ++i) {
        ArchiveRecord_t record;
        memcpy(&record, archive_memory + (size_t)i * sizeof(record), sizeof(record));
        if (!record_valid(&record)) return false;
        crc = digest_u16(crc, i);
        crc = digest_u16(crc, record.record_id);
        crc = digest_u16(crc, record.length);
        crc = digest_u16(crc, record.crc);
        for (uint16_t j = 0; j < record.length; ++j) crc = digest_byte(crc, record.payload[j]);
    }
    *digest = crc ^ UINT32_MAX;
    return true;
}
static bool same_transfer(const ArchiveTransfer_t *a, const ArchiveTransfer_t *b) {
    return a->boot_id == b->boot_id && a->transfer_id == b->transfer_id &&
           a->request_id == b->request_id && a->digest == b->digest &&
           a->record_count == b->record_count;
}

ArchiveStatus_t Archive_BeginTransfer(uint32_t request_id, ArchiveTransfer_t *out_transfer) {
    if (out_transfer == NULL) return ARCHIVE_ERR_PARAM;
    if (!archive_lock()) return ARCHIVE_ERR_UNAVAILABLE;
    ArchiveStatus_t status;
    if (transfer_active) {
        status = active_transfer.request_id == request_id ? ARCHIVE_OK : ARCHIVE_ERR_BUSY;
    } else if (receipt_valid && last_receipt.request_id == request_id) {
        status = ARCHIVE_ERR_STALE; // An old BEGIN is never a new destructive session.
    } else if (write_index == 0) {
        status = ARCHIVE_EMPTY;
    } else if (next_transfer_id == UINT32_MAX) {
        status = ARCHIVE_ERR_EXHAUSTED; // Refuse wrap, never reuse an ID within this boot.
    } else {
        const uint16_t count = (uint16_t)(write_index / sizeof(ArchiveRecord_t));
        uint32_t digest;
        if (!prefix_digest(count, &digest)) status = ARCHIVE_ERR_CRC;
        else {
            active_transfer = (ArchiveTransfer_t){.boot_id = boot_id, .transfer_id = ++next_transfer_id,
                .request_id = request_id, .digest = digest, .record_count = count};
            read_bitmap = 0;
            transfer_active = true;
            status = ARCHIVE_OK;
        }
    }
    if (status == ARCHIVE_OK) *out_transfer = active_transfer;
    xSemaphoreGive(archive_mutex);
    if (status == ARCHIVE_ERR_CRC) archive_report_crc();
    return status;
}

ArchiveStatus_t Archive_ReadTransferRecord(const ArchiveTransfer_t *transfer, uint16_t index,
                                          ArchiveRecord_t *out_record) {
    if (transfer == NULL || out_record == NULL) return ARCHIVE_ERR_PARAM;
    if (!archive_lock()) return ARCHIVE_ERR_UNAVAILABLE;
    ArchiveStatus_t status;
    if (!transfer_active || !same_transfer(transfer, &active_transfer)) status = ARCHIVE_ERR_STALE;
    else if (index >= active_transfer.record_count) status = ARCHIVE_ERR_NOT_FOUND;
    else {
        ArchiveRecord_t record;
        memcpy(&record, archive_memory + (size_t)index * sizeof(record), sizeof(record));
        if (!record_valid(&record)) status = ARCHIVE_ERR_CRC;
        else {
            *out_record = record;
            read_bitmap |= UINT32_C(1) << index;
            status = ARCHIVE_OK;
        }
    }
    xSemaphoreGive(archive_mutex);
    if (status == ARCHIVE_ERR_CRC) archive_report_crc();
    return status;
}

ArchiveStatus_t Archive_GetPendingTransfer(ArchiveTransfer_t *out_transfer) {
    if (out_transfer == NULL) return ARCHIVE_ERR_PARAM;
    if (!archive_lock()) return ARCHIVE_ERR_UNAVAILABLE;
    const ArchiveStatus_t status = transfer_active ? ARCHIVE_OK : ARCHIVE_ERR_NOT_FOUND;
    if (status == ARCHIVE_OK) *out_transfer = active_transfer;
    xSemaphoreGive(archive_mutex);
    return status;
}

ArchiveStatus_t Archive_ConfirmTransfer(const ArchiveTransfer_t *transfer) {
    if (transfer == NULL) return ARCHIVE_ERR_PARAM;
    if (!archive_lock()) return ARCHIVE_ERR_UNAVAILABLE;
    ArchiveStatus_t status;
    if (receipt_valid && receipt_confirmed && same_transfer(transfer, &last_receipt)) {
        status = ARCHIVE_OK; // ACK response lost: no second compaction, even during a newer session.
    } else if (!transfer_active || !same_transfer(transfer, &active_transfer)) status = ARCHIVE_ERR_STALE;
    else if (read_bitmap != (UINT32_MAX >> (32U - active_transfer.record_count))) status = ARCHIVE_ERR_NOT_READY;
    else {
        uint32_t digest;
        if (!prefix_digest(active_transfer.record_count, &digest) || digest != active_transfer.digest) {
            status = ARCHIVE_ERR_CRC;
        } else {
            const size_t prefix_size = (size_t)active_transfer.record_count * sizeof(ArchiveRecord_t);
            const size_t remaining = write_index - prefix_size;
            memmove(archive_memory, archive_memory + prefix_size, remaining);
            write_index = (uint16_t)remaining;
            memset(archive_memory + remaining, 0, sizeof(archive_memory) - remaining);
            last_receipt = active_transfer;
            receipt_valid = receipt_confirmed = true;
            transfer_active = false;
            read_bitmap = 0;
            status = ARCHIVE_OK;
        }
    }
    xSemaphoreGive(archive_mutex);
    if (status == ARCHIVE_ERR_CRC) archive_report_crc();
    return status;
}

ArchiveStatus_t Archive_CancelTransfer(const ArchiveTransfer_t *transfer) {
    if (transfer == NULL) return ARCHIVE_ERR_PARAM;
    if (!archive_lock()) return ARCHIVE_ERR_UNAVAILABLE;
    ArchiveStatus_t status = ARCHIVE_ERR_STALE;
    if (receipt_valid && !receipt_confirmed && same_transfer(transfer, &last_receipt)) status = ARCHIVE_OK;
    else if (transfer_active && same_transfer(transfer, &active_transfer)) {
        last_receipt = active_transfer;
        receipt_valid = true;
        receipt_confirmed = transfer_active = false;
        read_bitmap = 0;
        status = ARCHIVE_OK;
    }
    xSemaphoreGive(archive_mutex);
    return status;
}

#ifdef FSW_TEST_ARCHIVE_CORRUPTION
/**
 * @brief Test Hook: Manually corrupts a byte in memory to simulate radiation.
 * @param byte_index The exact byte in the 4096-byte array to flip.
 */
void Archive_CorruptMemoryForTest(uint16_t byte_index) {
    if (byte_index < ARCHIVE_SIZE_BYTES && archive_lock()) {
        // XOR with 0xFF flips all 8 bits in that byte
        archive_memory[byte_index] ^= 0xFF; 
        xSemaphoreGive(archive_mutex);
    }
}
#endif


static void archive_put_u16_be(uint8_t *destination, uint16_t value) {
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static bool archive_send_record(uint16_t index, const ArchiveRecord_t *record) {
    uint8_t packet[ARCHIVE_TM_HEADER_SIZE + MAX_RECORD_PAYLOAD];
    packet[0] = ARCHIVE_TM_VERSION;
    archive_put_u16_be(&packet[1], index);
    archive_put_u16_be(&packet[3], record->record_id);
    archive_put_u16_be(&packet[5], record->length);
    archive_put_u16_be(&packet[7], record->crc);
    memcpy(&packet[ARCHIVE_TM_HEADER_SIZE], record->payload, record->length);
    return YAMCS_SendTelemetry(APID_ARCHIVE, packet,
                              (uint16_t)(ARCHIVE_TM_HEADER_SIZE + record->length));
}

void ARCHIVE_ProcessCommand(const uint8_t* payload, uint16_t len) {
    if (payload == NULL || len < 1) return;
    if (Archive_ProcessTransferCommand(payload, len)) return;

    uint8_t command_id = payload[0];

    switch (command_id) {
        case ARCHIVE_CMD_GET_BY_INDEX:
            // Corrected: Strict check for exactly 3 bytes (ID + 2-byte Index)
            if (len == 3) {
                uint16_t target_index = (payload[1] << 8) | payload[2];
                
                ArchiveRecord_t record;
                ArchiveStatus_t status = Archive_ReadRecord(target_index, &record);

                if (status == ARCHIVE_OK) {
                    printf("ARCHIVE: Read success at index %d.\n", target_index);
                    if (archive_send_record(target_index, &record)) {
                        printf("ARCHIVE: Index %u submitted to UDP; record retained.\n",
                               (unsigned)target_index);
                    } else {
                        printf("ARCHIVE ERROR: TM send failed for index %u; record retained.\n",
                               (unsigned)target_index);
                    }
                } else {
                    printf("ARCHIVE ERROR: Read failed (Status: %d)\n", status);
                }
            } else {
                // Helpful for Ground Station debugging
                FaultReport_t cmd_fault = {
                    .source = SRC_ARCHIVE,
                    .severity = FAULT_WARNING,
                    .fault_code = FAULT_ARC_BAD_CMD_LEN, // Error code for "Wrong Command Length"
                    .timestamp = TIME_GetSeconds()
                };
                FDIR_ReportFault(cmd_fault);
                printf("ARCHIVE ERROR: GET_INDEX expects 3 bytes, got %d\n", len);
            }
            break;

        case ARCHIVE_CMD_WIPE_ALL:
            // Corrected: Strict check for safety key and length
            if (len == 2) {
                if (payload[1] == 0xAA) {
                    if (Archive_Reset() != ARCHIVE_OK) {
                        printf("ARCHIVE REJECTED: Wipe unavailable; records unchanged.\n");
                    }
                } else {
                    FaultReport_t security_fault = {
                        .source = SRC_ARCHIVE,
                        .severity = FAULT_WARNING,
                        .fault_code = FAULT_ARC_BAD_KEY, // Error code for "Invalid Security Key"
                        .timestamp = TIME_GetSeconds()
                    };
                    FDIR_ReportFault(security_fault);
                    printf("ARCHIVE REJECTED: Invalid safety key 0x%02X\n", payload[1]);
                }
            } else {
                printf("ARCHIVE ERROR: WIPE expects 2 bytes, got %d\n", len);
            }
            break;

        default:
            FaultReport_t unknown_fault = {
                .source = SRC_ARCHIVE,
                .severity = FAULT_WARNING,
                .fault_code = FAULT_ARC_UNKNOWN_CMD,
                .timestamp = TIME_GetSeconds()
            };
            FDIR_ReportFault(unknown_fault);
            printf("ARCHIVE: Unknown Command ID 0x%02X\n", command_id);
            break;
    }
}

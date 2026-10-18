#ifndef ARCHIVE_SERVICE_H
#define ARCHIVE_SERVICE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "utils.h"

#define ARCHIVE_SIZE_BYTES 4096
#define MAX_RECORD_PAYLOAD 128

// Archive TM body (APID_ARCHIVE): version:u8, index:u16, record_id:u16,
// payload_length:u16, payload_crc16:u16, payload:bytes. All u16 fields are BE.
// CRC-16/CCITT-FALSE covers only the payload, not the metadata.
#define ARCHIVE_TM_VERSION 1U
#define ARCHIVE_TM_HEADER_SIZE 9U

// Transfer TM (APID 0x061): version/op/status + manifest22 + index/id/len/crc (u16 BE)
// + exactly len payload bytes. Non-record responses use index=0xFFFF and id/len/crc=0.
#define ARCHIVE_TRANSFER_TM_VERSION 1U
#define ARCHIVE_TRANSFER_TM_HEADER_SIZE 33U
#define ARCHIVE_TRANSFER_MANIFEST_SIZE 22U

#ifdef FSW_TEST_ARCHIVE_CORRUPTION
void Archive_CorruptMemoryForTest(uint16_t byte_index);
#endif

// Usage is a byte count, never this sentinel. Do not interpret a failed query as empty.
#define ARCHIVE_USAGE_UNAVAILABLE UINT16_MAX

typedef enum {
    ARCHIVE_OK = 0,
    ARCHIVE_ERR_FULL = 1,
    ARCHIVE_ERR_CRC = 2,
    ARCHIVE_ERR_PARAM = 3,
    ARCHIVE_ERR_NOT_FOUND = 4,
    ARCHIVE_EMPTY = 5,
    ARCHIVE_ERR_UNAVAILABLE = 6,
    ARCHIVE_ERR_BUSY = 7,
    ARCHIVE_ERR_STALE = 8,
    ARCHIVE_ERR_NOT_READY = 9,
    ARCHIVE_ERR_EXHAUSTED = 10
} ArchiveStatus_t;

// --- ARCHIVE FAULT REGISTRY (For FDIR/Ground Station) ---
#define FAULT_ARC_FULL           0xAF01
#define FAULT_ARC_CRC_FAIL       0xAF02
#define FAULT_ARC_BAD_CMD_LEN    0xAF03
#define FAULT_ARC_BAD_KEY        0xAF04
#define FAULT_ARC_UNKNOWN_CMD    0xAF05


// --- ARCHIVE RECORD ID REGISTRY ---
#define REC_ID_HK          0x01  // Housekeeping Telemetry
#define REC_ID_PAYLOAD     0x02  // Science/Payload Data
#define REC_ID_FAULT       0x03  // Detailed Fault Logs
#define REC_ID_EVENT       0x04  // Special events (e.g., Deployments)

/**
 * @brief Internal representation of a stored record.
 * This is the "envelope" that protects the raw data.
 */
typedef struct {
    uint16_t record_id;
    uint16_t length;
    uint8_t  payload[MAX_RECORD_PAYLOAD];
    uint16_t crc;
} ArchiveRecord_t;

// A logical snapshot of the immutable prefix; concurrent appends are NOT included.
// digest: CRC-32/ISO-HDLC of BE count, then BE index/id/length/crc + payload per record.
// C structs are never a wire encoding. No padding/native byte order enters the digest.
typedef struct {
    uint64_t boot_id;
    uint32_t transfer_id;
    uint32_t request_id;
    uint32_t digest;
    uint16_t record_count;
} ArchiveTransfer_t;

// Startup init is required. Outputs stay unchanged on failure; runtime mutex rules apply.
// Begin retries with the active request_id return the SAME prefix, even after appends.
ArchiveStatus_t Archive_BeginTransfer(uint32_t request_id, ArchiveTransfer_t *out_transfer);
// Read-only discovery after a lost BEGIN reply; does not take over or replace a session.
ArchiveStatus_t Archive_GetPendingTransfer(ArchiveTransfer_t *out_transfer);
ArchiveStatus_t Archive_ReadTransferRecord(const ArchiveTransfer_t *transfer, uint16_t index,
                                          ArchiveRecord_t *out_record);
// Destructive: only the matching, fully read, CRC-checked prefix is removed.
// The ground caller must verify AND durably save all records before calling this.
// Duplicate confirmation succeeds without deleting again (latest completed receipt only).
ArchiveStatus_t Archive_ConfirmTransfer(const ArchiveTransfer_t *transfer);
ArchiveStatus_t Archive_CancelTransfer(const ArchiveTransfer_t *transfer);

/* --- Deterministic API --- */

// Sole startup caller, before tasks. Repeated successful init preserves records.
// Returns false if the boot discriminator or required mutex cannot be obtained.
bool Archive_Init(void);

// Stores a new record in the sequential buffer
ArchiveStatus_t Archive_WriteRecord(uint16_t id, const uint8_t *data, uint16_t len);

// Reads a validated snapshot without removing it. Output is unchanged on failure.
ArchiveStatus_t Archive_ReadRecord(uint16_t index, ArchiveRecord_t *out_record);

// Returns used bytes, or ARCHIVE_USAGE_UNAVAILABLE on uninitialized/lock failure.
uint16_t Archive_GetUsage(void);

// Explicit destructive operation; never called automatically after sending TM.
// Runtime operations wait at most pdMS_TO_TICKS(10) for the archive mutex.
// After verified empty-state readback, one zero-wait attempt resolves ONLY the
// previously observed nonfatal archive CRC revision. Busy/new/fatal reports stay
// latched; no automatic NOMINAL, persistent-history erase or background retry.
ArchiveStatus_t Archive_Reset(void);
void ARCHIVE_ProcessCommand(const uint8_t* payload, uint16_t len);
// Component dispatcher: true means a known transfer opcode was handled/rejected.
bool Archive_ProcessTransferCommand(const uint8_t *payload, uint16_t length);
#endif

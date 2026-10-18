#include "archive_service.h"
#include "commands.h"
#include "mission_ids.h"
#include "yamcs_interface.h"
#include <stdio.h>
#include <string.h>

_Static_assert(1U + ARCHIVE_TRANSFER_MANIFEST_SIZE + 2U <= 48U,
               "Transfer READ must fit the existing TC body limit");

static uint16_t get_u16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static uint32_t get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t get_u64(const uint8_t *p) { return ((uint64_t)get_u32(p) << 32) | get_u32(p + 4); }
static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void put_u64(uint8_t *p, uint64_t v) { put_u32(p, (uint32_t)(v >> 32)); put_u32(p + 4, (uint32_t)v); }
static ArchiveTransfer_t get_manifest(const uint8_t *p) {
    return (ArchiveTransfer_t){.boot_id = get_u64(p), .transfer_id = get_u32(p + 8),
        .request_id = get_u32(p + 12), .digest = get_u32(p + 16), .record_count = get_u16(p + 20)};
}
static void put_manifest(uint8_t *p, const ArchiveTransfer_t *t) {
    put_u64(p, t->boot_id); put_u32(p + 8, t->transfer_id);
    put_u32(p + 12, t->request_id); put_u32(p + 16, t->digest); put_u16(p + 20, t->record_count);
}
static void reply(uint8_t operation, ArchiveStatus_t status, const ArchiveTransfer_t *transfer,
                  uint16_t index, const ArchiveRecord_t *record) {
    uint8_t data[ARCHIVE_TRANSFER_TM_HEADER_SIZE + MAX_RECORD_PAYLOAD] = {0};
    data[0] = ARCHIVE_TRANSFER_TM_VERSION; data[1] = operation; data[2] = (uint8_t)status;
    put_manifest(data + 3, transfer);
    put_u16(data + 25, index);
    uint16_t size = ARCHIVE_TRANSFER_TM_HEADER_SIZE;
    if (record != NULL) {
        put_u16(data + 27, record->record_id); put_u16(data + 29, record->length); put_u16(data + 31, record->crc);
        memcpy(data + size, record->payload, record->length);
        size = (uint16_t)(size + record->length);
    }
    // All service locks have been released. A CONFIRM may already have committed;
    // TX failure never rolls it back: retry the identical command for the receipt.
    if (!YAMCS_SendTelemetry(APID_ARCHIVE_TRANSFER, data, size)) {
        printf("ARCHIVE: Transfer reply op=0x%02X status=%u not submitted; retry same command.\n",
               (unsigned)operation, (unsigned)status);
    }
}

bool Archive_ProcessTransferCommand(const uint8_t *payload, uint16_t length) {
    if (payload == NULL || length == 0) return false;
    const uint8_t op = payload[0];
    if (op < ARCHIVE_CMD_BEGIN_TRANSFER || op > ARCHIVE_CMD_CANCEL_TRANSFER) return false;
    const uint16_t expected = op == ARCHIVE_CMD_BEGIN_TRANSFER ? 5U :
        op == ARCHIVE_CMD_PENDING_TRANSFER ? 1U :
        op == ARCHIVE_CMD_READ_TRANSFER ? 25U : op == ARCHIVE_CMD_CONFIRM_TRANSFER ? 24U : 23U;
    ArchiveTransfer_t transfer = {0};
    ArchiveRecord_t record;
    uint16_t index = UINT16_MAX;
    ArchiveStatus_t status = ARCHIVE_ERR_PARAM;
    bool has_record = false;
    if (length == expected) {
        if (op == ARCHIVE_CMD_BEGIN_TRANSFER) {
            transfer.request_id = get_u32(payload + 1);
            status = Archive_BeginTransfer(transfer.request_id, &transfer);
        } else if (op == ARCHIVE_CMD_PENDING_TRANSFER) {
            status = Archive_GetPendingTransfer(&transfer);
        } else {
            transfer = get_manifest(payload + 1);
            if (op == ARCHIVE_CMD_READ_TRANSFER) {
                index = get_u16(payload + 23);
                status = Archive_ReadTransferRecord(&transfer, index, &record);
                has_record = status == ARCHIVE_OK;
            } else if (op == ARCHIVE_CMD_CONFIRM_TRANSFER) {
                if (payload[23] == ARCHIVE_CONFIRM_KEY) status = Archive_ConfirmTransfer(&transfer);
            } else status = Archive_CancelTransfer(&transfer);
        }
    }
    reply(op, status, &transfer, index, has_record ? &record : NULL);
    return true;
}

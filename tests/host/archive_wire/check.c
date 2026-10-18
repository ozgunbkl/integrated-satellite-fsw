/* Actual command dispatcher -> transfer core -> captured TM; only local host RAM. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "archive_service.h"
#include "commands.h"
#include "mission_ids.h"
#include "fdir_service.h"
#include "yamcs_interface.h"
#include "mutex_mock.h"

static uint8_t reply_bytes[161], command[49];
static uint16_t reply_size;
static unsigned sends;
static bool tx_ok = true;
static const uint8_t sample[] = "123456789";
static uint8_t manifest[22];
int __wrap_printf(const char *format, ...) { assert(!ArchiveMock_LockHeld()); return 0; }
uint32_t TIME_GetSeconds(void) { assert(!ArchiveMock_LockHeld()); return 123; }
void FDIR_ReportFault(FaultReport_t fault) { assert(!ArchiveMock_LockHeld()); }
bool YAMCS_SendTelemetry(uint16_t apid, void *data, uint16_t length) {
    assert(!ArchiveMock_LockHeld() && apid == APID_ARCHIVE_TRANSFER && length <= sizeof(reply_bytes));
    memcpy(reply_bytes, data, length); reply_size = length; ++sends; return tx_ok;
}
static uint16_t u16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static void invoke(uint16_t length, uint8_t op, ArchiveStatus_t status) {
    const unsigned before = sends;
    ARCHIVE_ProcessCommand(command, length);
    assert(sends == before + 1 && reply_bytes[0] == 1 && reply_bytes[1] == op && reply_bytes[2] == status);
    assert(reply_size >= 33 && reply_size == 33 + u16(reply_bytes + 29));
}
static void begin(void) {
    command[0] = ARCHIVE_CMD_BEGIN_TRANSFER; command[1] = 0x12; command[2] = 0x34; command[3] = 0x56; command[4] = 0x78;
    invoke(5, 0x31, ARCHIVE_OK); memcpy(manifest, reply_bytes + 3, sizeof(manifest));
}
static void session_command(uint8_t op) {
    memset(command, 0, sizeof(command)); command[0] = op; memcpy(command + 1, manifest, sizeof(manifest));
}
int main(void) {
    assert(Archive_Init());
    command[0] = 0x32; invoke(1, 0x32, ARCHIVE_ERR_NOT_FOUND);
    command[0] = 0x31; memset(command + 1, 0, 4); invoke(5, 0x31, ARCHIVE_EMPTY);
    assert(Archive_WriteRecord(0x1234, sample, 0) == ARCHIVE_OK);
    assert(Archive_WriteRecord(0xABCD, sample, 9) == ARCHIVE_OK);
    begin();
    const uint8_t golden[] = {1,0x31,0, 1,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
        0,0,0,1, 0x12,0x34,0x56,0x78, 0x41,0x7E,0xE9,0xBC, 0,2,
        0xFF,0xFF,0,0,0,0,0,0};
    assert(reply_size == sizeof(golden) && memcmp(reply_bytes, golden, sizeof(golden)) == 0);
    const uint16_t usage = Archive_GetUsage();
    command[0] = 0x32; invoke(1, 0x32, ARCHIVE_OK); assert(memcmp(reply_bytes + 3, manifest, 22) == 0);

    // Every known opcode rejects every non-exact body size without modifying data/session.
    for (uint8_t op = 0x31; op <= 0x35; ++op) {
        const uint16_t exact = op == 0x31 ? 5 : op == 0x32 ? 1 : op == 0x33 ? 25 : op == 0x34 ? 24 : 23;
        for (uint16_t len = 1; len <= 48; ++len) {
            if (len == exact) continue;
            memset(command, 0xA5, sizeof(command)); command[0] = op;
            invoke(len, op, ARCHIVE_ERR_PARAM); assert(reply_size == 33 && Archive_GetUsage() == usage);
        }
    }
    const unsigned before = sends;
    assert(!Archive_ProcessTransferCommand(NULL, 1));
    assert(!Archive_ProcessTransferCommand(command, 0));
    command[0] = 0x30; assert(!Archive_ProcessTransferCommand(command, 3));
    assert(sends == before);
    session_command(0x34); command[23] = 0; invoke(24, 0x34, ARCHIVE_ERR_PARAM);
    command[23] = ARCHIVE_CONFIRM_KEY; invoke(24, 0x34, ARCHIVE_ERR_NOT_READY);
    assert(Archive_GetUsage() == usage);
    session_command(0x33); command[23] = 0; command[24] = 1; tx_ok = false;
    invoke(25, 0x33, ARCHIVE_OK); // Local send failure is not a ground ACK.
    assert(reply_size == 42 && u16(reply_bytes + 25) == 1 && u16(reply_bytes + 27) == 0xABCD);
    assert(u16(reply_bytes + 29) == 9 && u16(reply_bytes + 31) == 0x29B1 && memcmp(reply_bytes + 33, sample, 9) == 0);
    tx_ok = true; invoke(25, 0x33, ARCHIVE_OK); assert(Archive_GetUsage() == usage);
    session_command(0x33); command[23] = 0; command[24] = 0; invoke(25, 0x33, ARCHIVE_OK);
    assert(reply_size == 33 && u16(reply_bytes + 31) == 0xFFFF); // Legitimate empty payload.
    session_command(0x33); command[23] = 0xFF; command[24] = 0xFF;
    invoke(25, 0x33, ARCHIVE_ERR_NOT_FOUND); assert(reply_size == 33);
    assert(Archive_WriteRecord(0xEEEE, sample, 9) == ARCHIVE_OK);
    session_command(0x34); command[23] = ARCHIVE_CONFIRM_KEY; tx_ok = false;
    invoke(24, 0x34, ARCHIVE_OK); // Lost CONFIRM receipt: prefix already committed, tail retained.
    assert(Archive_GetUsage() == sizeof(ArchiveRecord_t));
    tx_ok = true; invoke(24, 0x34, ARCHIVE_OK); assert(Archive_GetUsage() == sizeof(ArchiveRecord_t));
    session_command(0x33); invoke(25, 0x33, ARCHIVE_ERR_STALE);
    command[0] = 0x31; command[1] = 0x12; command[2] = 0x34; command[3] = 0x56; command[4] = 0x78;
    invoke(5, 0x31, ARCHIVE_ERR_STALE);
    command[4] = 0x79; invoke(5, 0x31, ARCHIVE_OK); memcpy(manifest, reply_bytes + 3, 22);
    session_command(0x35); command[1] ^= 1; invoke(23, 0x35, ARCHIVE_ERR_STALE);
    session_command(0x35); invoke(23, 0x35, ARCHIVE_OK); invoke(23, 0x35, ARCHIVE_OK);
    assert(Archive_GetUsage() == sizeof(ArchiveRecord_t));
    session_command(0x34); command[23] = ARCHIVE_CONFIRM_KEY; invoke(24, 0x34, ARCHIVE_ERR_STALE);
    assert(Archive_Reset() == ARCHIVE_OK);
    uint8_t maximum[128]; memset(maximum, 0xA5, sizeof(maximum));
    assert(Archive_WriteRecord(0x1234, maximum, sizeof(maximum)) == ARCHIVE_OK);
    begin();
    for (uint8_t op = 0x31; op <= 0x35; ++op) {
        session_command(op);
        const uint16_t size = op == 0x31 ? 5 : op == 0x32 ? 1 : op == 0x33 ? 25 : op == 0x34 ? 24 : 23;
        if (op == 0x34) command[23] = ARCHIVE_CONFIRM_KEY;
        ArchiveMock_FailTake(); invoke(size, op, ARCHIVE_ERR_UNAVAILABLE);
        assert(Archive_GetUsage() == sizeof(ArchiveRecord_t));
    }
    session_command(0x33); invoke(25, 0x33, ARCHIVE_OK);
    assert(reply_size == 161 && memcmp(reply_bytes + 33, maximum, 128) == 0);
    Archive_CorruptMemoryForTest((uint16_t)offsetof(ArchiveRecord_t, payload));
    invoke(25, 0x33, ARCHIVE_ERR_CRC); assert(reply_size == 33 && u16(reply_bytes + 29) == 0);
    assert(Archive_GetUsage() == sizeof(ArchiveRecord_t));
    puts("PASS: real archive wire dispatch, 235 malformed-length rejects, manifest golden BE bytes,");
    puts("      pending/empty/read 0/9/128, key/read-set/identity/timeout/CRC guards,");
    puts("      failed read TX retains data, lost CONFIRM receipt retries once, append retained, cancel doesn't wipe.");
    return 0;
}

/* Real UDP receiver -> private queue -> processor -> router, offline boundaries. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "satellite_types.h"
#include "ccsds_packet.h"
#include "tc_proc.h"
#include "utils.h"
#include "commands.h"
#include "fdir_service.h"
#include "watchdog.h"
#include "yamcs_interface.h"

void vYamcsTcReceiverTask(void *parameters);
typedef struct { uint8_t data[80]; uint16_t length; bool accepted, full; } Input;
static Input inputs[96];
static unsigned count, received, queue_calls, queued, consumed, pets, routes, faults, zero_delays;
static TelecommandPacket_t packets[96];
static unsigned origins[96];
static int queue_slot;
QueueHandle_t xCommandQueue = &queue_slot;
static jmp_buf rx_done, proc_done;

static uint16_t be16(const uint8_t *b) { return (uint16_t)(((uint16_t)b[0] << 8) | b[1]); }
static Input *add(uint16_t length, bool accepted) {
    assert(count < sizeof(inputs) / sizeof(inputs[0]) && length <= sizeof(inputs[0].data));
    Input *in = &inputs[count++];
    in->length = length;
    in->accepted = accepted;
    const uint16_t data_len = length > 6 ? (uint16_t)(length - 7) : 0;
    const uint8_t header[] = {0x10,0x60,0xC0,0x07,(uint8_t)(data_len >> 8),(uint8_t)data_len};
    memcpy(in->data, header, sizeof(header));
    for (unsigned i = 6; i < length; ++i) in->data[i] = (uint8_t)(0x30 + i - 6);
    return in;
}

bool YAMCS_IsNetworkStackReady(void) { return true; }
uint64_t TIME_GetMilliseconds(void) { return UINT64_C(0x0102030405060708); }
uint32_t TIME_GetSeconds(void) { return 42; }
void vTaskDelete(void *task) { assert(!"ready receiver must not self-delete"); }
void vTaskDelay(TickType_t wait) { assert(wait == pdMS_TO_TICKS(10)); ++zero_delays; }
int test_socket(int domain, int type, int protocol) {
    assert(domain == AF_INET && type == SOCK_DGRAM && protocol == IPPROTO_UDP); return 42;
}
int test_bind(int fd, const struct sockaddr *addr, socklen_t length) {
    const struct sockaddr_in *a = (const struct sockaddr_in *)addr;
    assert(fd == 42 && length == sizeof(*a) && ntohs(a->sin_port) == YAMCS_TC_PORT);
    assert(a->sin_addr.s_addr == htonl(INADDR_ANY)); return 0;
}
int test_setsockopt(int fd, int level, int option, const void *value, socklen_t length) {
    assert(fd == 42 && level == SOL_SOCKET && option == SO_RCVTIMEO && length == sizeof(struct timeval));
    assert(((const struct timeval *)value)->tv_sec == 1); return 0;
}
int test_close(int fd) { assert(!"no transport errors in this fixture"); return 0; }
ssize_t test_recvfrom(int fd, void *out, size_t capacity, int flags,
                     struct sockaddr *address, socklen_t *length) {
    assert(fd == 42 && capacity == 55 && flags == 0 && address != NULL && length != NULL);
    if (received == count) longjmp(rx_done, 1);
    Input *in = &inputs[received++];
    const size_t copied = in->length < capacity ? in->length : capacity;
    memcpy(out, in->data, copied); // Match UDP truncation: long datagram returns buffer capacity.
    return (ssize_t)copied;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    assert(queue == xCommandQueue && wait == pdMS_TO_TICKS(100));
    Input *in = &inputs[received - 1];
    const TelecommandPacket_t *packet = item;
    assert(in->accepted && in->length >= 7 && in->length <= 54);
    ++queue_calls;
    assert(packet->length == in->length + 10);
    assert(CCSDS_ValidateUnsegmentedPacket(packet->raw_data, packet->length, true, true));
    assert(CCSDS_GetAPID(packet->raw_data) == CCSDS_GetAPID(in->data));
    assert(be16(packet->raw_data + 2) == be16(in->data + 2));
    for (unsigned i = 6; i < 14; ++i) assert(packet->raw_data[i] == 0);
    assert(memcmp(packet->raw_data + 14, in->data + 6, in->length - 6) == 0);
    const uint16_t crc = crc16_ccitt(packet->raw_data, packet->length - 2);
    assert(packet->packet_crc == crc && be16(packet->raw_data + packet->length - 2) == crc);
    for (unsigned i = packet->length; i < sizeof(packet->raw_data); ++i) assert(packet->raw_data[i] == 0);
    if (received == 1) {
        // Independently calculated with Python binascii.crc_hqx(..., 0xFFFF).
        const uint8_t golden[] = {0x18,0x60,0xC0,7,0,12,0,0,0,0,0,0,0,0,0x30,0x12,0x34,0x5A,0xC3};
        assert(packet->length == sizeof(golden) && memcmp(packet->raw_data, golden, sizeof(golden)) == 0);
    }
    if (in->full) return pdFALSE;
    origins[queued] = received - 1;
    packets[queued++] = *packet;
    return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *out, TickType_t wait) {
    assert(queue == xCommandQueue && wait == pdMS_TO_TICKS(100));
    if (consumed == queued) longjmp(proc_done, 1);
    memcpy(out, &packets[consumed++], sizeof(TelecommandPacket_t)); return pdPASS;
}
void watchdog_pet(WatchdogTaskID_t id) {
    assert(id == WDT_TASK_CMD_PROC); ++pets;
    assert(pets == consumed && routes + faults == consumed);
}
static void routed(const uint8_t *payload, uint16_t length, uint16_t apid) {
    const Input *in = &inputs[origins[consumed - 1]];
    assert(CCSDS_GetAPID(in->data) == apid && length == in->length - 6);
    assert(memcmp(payload, in->data + 6, length) == 0); ++routes;
}
#define HANDLER(name, apid) void name(const uint8_t *p, uint16_t n) { routed(p, n, apid); }
HANDLER(ADCS_ProcessCommand, APID_ADCS)
HANDLER(EPS_ProcessCommand, APID_EPS)
HANDLER(HK_ProcessCommand, APID_HK)
HANDLER(ARCHIVE_ProcessCommand, APID_ARCHIVE)
HANDLER(FDIR_ProcessCommand, APID_FDIR)
HANDLER(PAYLOAD_ProcessCommandWrapper, APID_PAYLOAD)
void RECOVERY_ProcessCommand(const uint8_t *payload, uint16_t length, uint32_t now) {
    assert(payload[0] == FDIR_CMD_REARM_BOOT && length == 6 && now == UINT32_C(0x05060708));
    routed(payload, length, APID_FDIR);
}
void FDIR_ReportFault(FaultReport_t fault) {
    assert(fault.source == SRC_CDH && fault.severity == FAULT_WARNING && fault.fault_code == FAULT_CDH_UNKNOWN_APID);
    ++faults;
}

static void check_primary_helpers(void) {
    uint8_t storage[9] = {0};
    uint8_t *p = storage + 1; // Deliberately unaligned.
    p[2] = 0xC0;
    for (unsigned apid = 0; apid <= 0x07FF; ++apid) {
        for (unsigned kind = 0; kind < 4; ++kind) {
            const bool tc = (kind & 1) != 0, sec = (kind & 2) != 0;
            const uint16_t id = (uint16_t)(apid | (tc ? 0x1000 : 0) | (sec ? 0x0800 : 0));
            p[0] = (uint8_t)(id >> 8); p[1] = (uint8_t)id;
            assert(CCSDS_GetAPID(p) == apid && CCSDS_HasSecondaryHeader(p) == sec);
            assert(CCSDS_ValidateUnsegmentedPacket(p, 7, tc, sec));
            assert(!CCSDS_ValidateUnsegmentedPacket(p, 7, !tc, sec));
            assert(!CCSDS_ValidateUnsegmentedPacket(p, 7, tc, !sec));
        }
    }
    assert(!CCSDS_ValidateUnsegmentedPacket(NULL, UINT16_MAX, true, false));
    assert(CCSDS_GetAPID(NULL) == 0 && !CCSDS_HasSecondaryHeader(NULL));
    uint8_t output[16]; memset(output, 0xA5, sizeof(output));
    CCSDS_WrapTelemetry(0x800, NULL, 0, output);
    CCSDS_WrapTelemetry(APID_IDLE, NULL, 0, output);
    CCSDS_WrapTelemetry(APID_ARCHIVE, NULL, 1, output);
    CCSDS_WrapTelemetry(APID_ARCHIVE, output, UINT16_MAX, output);
    for (unsigned i = 0; i < sizeof(output); ++i) assert(output[i] == 0xA5);
    CCSDS_WrapTelemetry(APID_ARCHIVE, NULL, 0, output + 1);
    assert(output[0] == 0xA5 && output[15] == 0xA5);
    assert(CCSDS_ValidateUnsegmentedPacket(output + 1, 14, false, true));
}

int main(void) {
    check_primary_helpers();
    Input *golden = add(9, true); golden->data[7] = 0x12; golden->data[8] = 0x34;
    for (unsigned n = 7; n <= 54; ++n) add((uint16_t)n, true);
    const uint8_t apids[] = {APID_ADCS,APID_EPS,APID_HK,APID_FDIR,APID_PAYLOAD};
    for (unsigned i = 0; i < sizeof(apids); ++i) add(7, true)->data[1] = apids[i];
    Input *rearm = add(12, true);
    rearm->data[1] = APID_FDIR; rearm->data[6] = FDIR_CMD_REARM_BOOT;
    rearm->data[7] = rearm->data[8] = rearm->data[9] = 0;
    rearm->data[10] = 1; rearm->data[11] = 0xA5;
    add(9, true)->full = true;
    Input *unknown = add(7, true); unknown->data[0] = 0x11; unknown->data[1] = 0x23;
    for (unsigned n = 0; n < 7; ++n) add((uint16_t)n, false);
    for (unsigned n = 55; n <= 80; n += 5) add((uint16_t)n, false);
    for (unsigned version = 1; version < 8; ++version) add(7, false)->data[0] |= (uint8_t)(version << 5);
    add(7, false)->data[0] &= (uint8_t)~0x10; // TM, not TC.
    add(7, false)->data[0] |= 0x08; // Secondary header unsupported on wire TC.
    for (unsigned flag = 0; flag < 3; ++flag) add(7, false)->data[2] = (uint8_t)(flag << 6);
    add(7, false)->data[5] = 1; // Declared extra byte missing.
    add(8, false)->data[5] = 0; // Undeclared trailing byte.
    Input *overflow = add(7, false); overflow->data[4] = overflow->data[5] = 0xFF;
    Input *idle = add(7, false); idle->data[0] = 0x17; idle->data[1] = 0xFF;
    if (setjmp(rx_done) == 0) vYamcsTcReceiverTask(NULL);
    unsigned expected = 0;
    for (unsigned i = 0; i < count; ++i) expected += inputs[i].accepted;
    assert(received == count && queue_calls == expected && queued == expected - 1 && zero_delays == 1);
    if (setjmp(proc_done) == 0) vCommandProcessorTask(NULL);
    assert(consumed == queued && pets == queued && routes == queued - 1 && faults == 1);
    printf("PASS: %u offline wire inputs, all 7..54-byte TC lengths, truncation/header/flag rejection,\n"
           "      golden internal bytes/CRC, queue full, six routes and unknown APID; %u commands processed.\n", count, queued);
    return 0;
}

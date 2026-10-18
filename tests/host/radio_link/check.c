/* Existing demo framing with a captured router and clock; no radio or UDP I/O. */
#include "comms_frame.h"
#include "tm_manager.h"
#include "ccsds_packet.h"
#include "cdhs_router.h"
#include "time_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

uint8_t shared_downlink_bus[150];
uint16_t last_packet_len;
static uint8_t routed[MAX_PAYLOAD_SIZE];
static uint16_t routed_length;
static unsigned route_calls;

int __wrap_printf(const char *format, ...) { (void)format; return 0; }
uint64_t TIME_GetMilliseconds(void) { return UINT64_C(0x0102030405060708); }
void CDHS_RoutePacket(const uint8_t *packet, uint16_t length) {
    assert(length <= sizeof(routed));
    memcpy(routed, packet, length);
    routed_length = length;
    ++route_calls;
}

static size_t serialize(const comms_frame_t *frame, uint8_t *wire) {
    wire[0] = frame->start_byte;
    wire[1] = frame->length;
    memcpy(wire + 2, frame->payload, frame->length);
    wire[frame->length + 2] = (uint8_t)(frame->crc >> 8);
    wire[frame->length + 3] = (uint8_t)frame->crc;
    return (size_t)frame->length + 4;
}

static unsigned feed(const uint8_t *wire, size_t length) {
    unsigned complete = 0;
    for (size_t i = 0; i < length; ++i) {
        const int result = COMMS_ParseByte(wire[i]);
        assert(result == 0 || (result == 1 && i == length - 1));
        complete += (unsigned)result;
    }
    return complete;
}

static void check_frames(void) {
    const uint8_t reference[] = "123456789";
    assert(COMMS_CalculateCRC16(reference, sizeof(reference) - 1) == 0x29B1);
    uint8_t payload[MAX_PAYLOAD_SIZE], wire[MAX_PAYLOAD_SIZE + 4];
    for (unsigned i = 0; i < sizeof(payload); ++i) { payload[i] = (uint8_t)(i ^ 0xAA); }
    for (unsigned length = 1; length <= MAX_PAYLOAD_SIZE; ++length) {
        comms_frame_t frame = {0};
        COMMS_CreateFrame(&frame, payload, (uint8_t)length);
        assert(frame.start_byte == FRAME_START_BYTE && frame.length == length);
        assert(memcmp(frame.payload, payload, length) == 0);
        const size_t wire_length = serialize(&frame, wire);
        COMMS_ResetParser();
        const unsigned before = route_calls;
        assert(feed(wire, wire_length) == 1);
        assert(route_calls == before + 1 && routed_length == length);
        assert(memcmp(routed, payload, length) == 0);
        wire[wire_length - 1] ^= 1;
        assert(feed(wire, wire_length) == 0 && route_calls == before + 1);
        wire[wire_length - 1] ^= 1;
        assert(feed(wire, wire_length) == 1 && route_calls == before + 2);
    }
    comms_frame_t frame = {0};
    COMMS_CreateFrame(&frame, payload, sizeof(payload));
    const size_t wire_length = serialize(&frame, wire);
    COMMS_ResetParser();
    assert(feed(wire, 4) == 0);
    COMMS_ResetParser();
    assert(feed(wire, wire_length) == 1);
    const uint8_t bad_lengths[] = {0, MAX_PAYLOAD_SIZE + 1, UINT8_MAX};
    for (unsigned i = 0; i < sizeof(bad_lengths); ++i) {
        assert(COMMS_ParseByte(FRAME_START_BYTE) == 0);
        assert(COMMS_ParseByte(bad_lengths[i]) == 0);
        assert(feed(wire, wire_length) == 1);
    }
    const comms_frame_t saved = frame;
    COMMS_CreateFrame(NULL, payload, 1);
    COMMS_CreateFrame(&frame, NULL, 1);
    COMMS_CreateFrame(&frame, payload, MAX_PAYLOAD_SIZE + 1);
    assert(memcmp(&frame, &saved, sizeof(frame)) == 0);
}

static void check_telemetry(void) {
    const unsigned lengths[] = {0, 1, MAX_PAYLOAD_SIZE - 14};
    uint8_t data[MAX_PAYLOAD_SIZE - 14];
    for (unsigned i = 0; i < sizeof(data); ++i) { data[i] = (uint8_t)(0x40 + i); }
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        const unsigned length = lengths[i];
        TM_SendReport(APID_ARCHIVE, data, (uint16_t)length);
        assert(last_packet_len == length + 18);
        assert(shared_downlink_bus[0] == FRAME_START_BYTE && shared_downlink_bus[1] == length + 14);
        const uint8_t *packet = shared_downlink_bus + 2;
        // TM type is zero; only the secondary-header bit is set.
        assert(packet[0] == 0x08 && packet[1] == APID_ARCHIVE);
        assert(CCSDS_ValidateUnsegmentedPacket(packet, (uint16_t)(length + 14), false, true));
        assert(packet[2] == 0xC0 && packet[3] == 0 && packet[4] == 0 && packet[5] == length + 7);
        for (unsigned byte = 0; byte < 8; ++byte) { assert(packet[6 + byte] == byte + 1); }
        assert(memcmp(packet + 14, data, length) == 0);
        assert(CCSDS_GetAPID(packet) == APID_ARCHIVE && CCSDS_HasSecondaryHeader(packet));
        const uint16_t crc = COMMS_CalculateCRC16(shared_downlink_bus, last_packet_len - 2);
        assert(shared_downlink_bus[last_packet_len - 2] == (uint8_t)(crc >> 8));
        assert(shared_downlink_bus[last_packet_len - 1] == (uint8_t)crc);
        COMMS_ResetParser();
        assert(feed(shared_downlink_bus, last_packet_len) == 1);
        assert(routed_length == length + 14 && memcmp(routed, packet, routed_length) == 0);
    }
}

int main(void) {
    check_frames();
    check_telemetry();
    puts("PASS: demo radio CRC, 1..64-byte frames, reject/resync/reset and bounded telemetry framing.");
    return 0;
}

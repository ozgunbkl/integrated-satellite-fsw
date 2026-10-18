#include "ccsds_packet.h"
#include "time_service.h"
#include <string.h>

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void write_be16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

uint16_t CCSDS_GetAPID(const uint8_t *buffer) {
    return buffer == NULL ? 0 : (uint16_t)(read_be16(buffer) & 0x07FFU);
}

bool CCSDS_HasSecondaryHeader(const uint8_t *buffer) {
    return buffer != NULL && (read_be16(buffer) & 0x0800U) != 0;
}

bool CCSDS_ValidateUnsegmentedPacket(const uint8_t *buffer, uint16_t length,
                                    bool telecommand, bool secondary_header) {
    if (buffer == NULL || length < sizeof(CCSDS_PrimaryHeader_t) + 1U) return false;
    const uint16_t expected = (telecommand ? 0x1000U : 0U) | (secondary_header ? 0x0800U : 0U);
    if ((read_be16(buffer) & 0xF800U) != expected ||
        (read_be16(buffer + 2) & 0xC000U) != 0xC000U) return false;
    // Widen before adding: 0xFFFF must not wrap into an apparently short packet.
    return (uint32_t)length == (uint32_t)read_be16(buffer + 4) + sizeof(CCSDS_PrimaryHeader_t) + 1U;
}

void CCSDS_WrapTelemetry(uint16_t apid, const uint8_t *app_data, uint16_t app_data_len, uint8_t *out_buffer) {
    if (out_buffer == NULL || (app_data == NULL && app_data_len != 0U) ||
        apid > 0x07FFU || apid == APID_IDLE || app_data_len > UINT16_MAX - 7U) return;
    // Version 0, Type 0 (TM), Secondary Header 1. TC would set bit 12.
    write_be16(out_buffer, (uint16_t)(0x0800U | apid));
    write_be16(out_buffer + 2, 0xC000U); // Legacy demo sequence is always zero.
    write_be16(out_buffer + 4, (uint16_t)(8U + app_data_len - 1U));
    const uint64_t now = TIME_GetMilliseconds();
    for (unsigned i = 0; i < 8; ++i) out_buffer[6 + i] = (uint8_t)(now >> (56U - 8U * i));
    if (app_data_len != 0U) memcpy(out_buffer + 14, app_data, app_data_len);
}

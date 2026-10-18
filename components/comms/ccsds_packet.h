#ifndef CCSDS_PACKET_H
#define CCSDS_PACKET_H

#include <stdint.h>
#include <stdbool.h>
#include "mission_ids.h"

// CCSDS Primary Header (6 bytes / 48 bits)
typedef struct __attribute__((packed)) {
    uint16_t packet_id;
    uint16_t sequence_ctrl;
    uint16_t packet_length;
} CCSDS_PrimaryHeader_t;

// CCSDS Secondary Header (8 bytes / 64 bits)
typedef struct __attribute__((packed)){
    uint64_t mission_time;     // MET in milliseconds 
} CCSDS_SecondaryHeader_t;

// Extract APID from a raw buffer
uint16_t CCSDS_GetAPID(const uint8_t* buffer);


// Check if the Secondary Header Flag is set
bool CCSDS_HasSecondaryHeader(const uint8_t* buffer);

// Project-sized unsegmented packet: primary version/type/secondary flag and
// exact length agreement only. Does not authenticate, check CRC or decode data.
bool CCSDS_ValidateUnsegmentedPacket(const uint8_t *buffer, uint16_t length,
                                    bool telecommand, bool secondary_header);

// Legacy demo envelope: 8-byte BE mission ms, not active UDP. Caller owns output
// capacity (14 + app_data_len). Demo sequence remains zero; invalid input is ignored.
void CCSDS_WrapTelemetry(uint16_t apid, const uint8_t* app_data, uint16_t app_data_len, uint8_t* out_buffer);


#endif

#ifndef CDHS_ROUTER_H
#define CDHS_ROUTER_H

#include <stdint.h>

/**
 * @brief Route a complete internal telecommand after CRC verification.
 * packet_buffer contains the 14-byte header, command data and trailing 2-byte CRC.
 * length includes the header and CRC, and must be within the internal TC bounds.
 * Subsystem handlers receive command data only, without the header or CRC.
 */
void CDHS_RoutePacket(const uint8_t* packet_buffer, uint16_t length);

#endif

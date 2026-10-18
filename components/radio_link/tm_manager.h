#ifndef TM_MANAGER_H
#define TM_MANAGER_H

#include <stdint.h>

/**
 * @brief Packages and downlinks subsystem data using CCSDS and Comms framing.
 * * @param apid     The CCSDS Application Process Identifier (e.g., 0x010 for ADCS)
 * @param data     Pointer to the subsystem data buffer
 * @param data_len Length of the data buffer
 */
void TM_SendReport(uint16_t apid, const uint8_t* data, uint16_t data_len);

#endif

#include "tm_manager.h"
#include "ccsds_packet.h"
#include "comms_frame.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>


#ifdef SI_TEST_MODE
    extern uint8_t shared_downlink_bus[150];
    extern uint16_t last_packet_len;
#endif

/**
 * @brief High-level function to send telemetry from any subsystem
 * * @param apid The ID of the subsystem (e.g., 0x020 for EPS)
 * @param data Pointer to the subsystem's raw data
 * @param data_len Length of the raw data
 */

 void TM_SendReport (uint16_t apid, const uint8_t* data, uint16_t data_len){
    uint8_t ccsds_packet[128];  // Buffer for headers + data
    uint8_t radio_frame[150];   // Buffer for the final "Box" (comms frame)

    // 1. Wrap data into CCSDS Packet (Adds Pri & Sec headers)
    CCSDS_WrapTelemetry(apid, data, data_len, ccsds_packet);

    // Total size of CCSDS: 6 (pri) + 8 (sec) + User Data
    uint16_t ccsds_total_len = 14 + data_len;

    // 2. Wrap the CCSDS packet into a Comms frame for radio
    // Add 0xAA, Length and the CRC

    radio_frame[0] = FRAME_START_BYTE;
    radio_frame[1] = (uint8_t)ccsds_total_len;
    memcpy(&radio_frame[2], ccsds_packet, ccsds_total_len);

    // 3. Calculate CRC over [0xAA + len + CCSDS]
    uint16_t crc = COMMS_CalculateCRC16(radio_frame, ccsds_total_len + 2);
    radio_frame[ccsds_total_len + 2] = (uint8_t)(crc >> 8);
    radio_frame[ccsds_total_len + 3] = (uint8_t)(crc & 0xFF);

    

#ifdef SI_TEST_MODE
    // Copy to shared bus for the simulator
    last_packet_len = ccsds_total_len + 4;
    memcpy(shared_downlink_bus, radio_frame, last_packet_len);

    // Diagnostic hex print
    printf("SAT_RADIO_OUT (%d bytes): ", last_packet_len);
    for(int i = 0; i < last_packet_len; i++) {
        printf("%02X ", shared_downlink_bus[i]);
    }
    printf("\n");
#else
    // 4. Physical send  (RADIO_Transmit(radio_frame, ccsds_total_len + 4))
    printf("TM_MANAGER: Downlinking packet for APID 0x%03X (%d bytes total)\n", apid, ccsds_total_len + 4);
#endif
 }

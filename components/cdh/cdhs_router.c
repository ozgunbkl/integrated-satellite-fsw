#include "cdhs_router.h"
#include "ccsds_packet.h"
#include "satellite_types.h"
#include "adcs_service.h"
#include "eps_commands.h"
#include "hk_service.h"
#include "archive_service.h"
#include "fdir_service.h"
#include "payload_service.h"
#include "commands.h"
#include "time_service.h"
#include "recovery.h"

#include <stdio.h>

static void handle_internal_cdh_cmd(const uint8_t* payload, uint16_t len);

void CDHS_RoutePacket(const uint8_t* packet_buffer, uint16_t length){
    if (packet_buffer == NULL) {
        printf("CDHS: Null packet buffer. Dropping.\n");
        return;
    }

    if (length < TELECOMMAND_MIN_SIZE || length > TELECOMMAND_MAX_SIZE) {
        FaultReport_t len_fault = {
            .source = SRC_CDH,
            .severity = FAULT_WARNING,
            .fault_code = (length < TELECOMMAND_MIN_SIZE)
                ? FAULT_CDH_PACKET_TOO_SHORT : FAULT_CDH_ROUTING_ERROR,
            .timestamp = TIME_GetSeconds()
        };
        FDIR_ReportFault(len_fault);
        printf("CDHS: Invalid telecommand length (%u bytes). Dropping.\n", (unsigned)length);
        return;
    }
    
    // 1. Extract the APID
    uint16_t apid = CCSDS_GetAPID(packet_buffer);

    
    // 2. Exclude both the fixed internal header and the trailing CRC.
    const uint8_t* userData = packet_buffer + CCSDS_HEADER_SIZE;
    uint16_t userDataLen = length - CCSDS_HEADER_SIZE - CCSDS_CRC_SIZE;

    // 3. Post office switch
    switch (apid){
        case APID_ADCS:
            printf("CDHS: Routing packet to ADCS...\n");
            ADCS_ProcessCommand(userData, userDataLen);
            break;
        
        case APID_EPS:
            printf("CDHS: Routing packet to EPS...\n");
            EPS_ProcessCommand(userData, userDataLen); 
            break;

        case APID_HK:
            printf("CDHS: Routing packet to HK...\n");
            HK_ProcessCommand(userData, userDataLen);
            break;

        case APID_ARCHIVE:
            printf("CDHS: Routing packet to ARCHIVE...\n");
            ARCHIVE_ProcessCommand(userData, userDataLen);
            break;
        
        case APID_FDIR:
            printf("CDHS: Routing packet to FDIR...\n");
            if (userDataLen && userData[0] == FDIR_CMD_REARM_BOOT)
                RECOVERY_ProcessCommand(userData, userDataLen, TIME_GetMilliseconds());
            else FDIR_ProcessCommand(userData, userDataLen);
            break;

        case APID_PAYLOAD:
            printf("CDHS: Routing packet to PAYLOAD...\n");
            PAYLOAD_ProcessCommandWrapper(userData, userDataLen);
            break;

        case APID_CDHS:
            printf("CDHS: Routing packet to CDHS...\n");
            handle_internal_cdh_cmd(userData, userDataLen);
            break;

        case APID_IDLE:
            
            break;
        
        default:
            printf("CDHS WARNING: Unknown APID 0x%03X recieved!\n", apid);
            FaultReport_t unknown_apid_fault = {
                .source = SRC_CDH,
                .severity = FAULT_WARNING,
                .fault_code = FAULT_CDH_UNKNOWN_APID,
                .timestamp = TIME_GetSeconds()
            };
            FDIR_ReportFault(unknown_apid_fault);
            break;
    }
}


static void handle_internal_cdh_cmd(const uint8_t* payload, uint16_t len) {
    if (len < 1) return;

    switch (payload[0]) {
        case CDH_CMD_REBOOT:
            printf("CDHS: System Rebooting...\n");
            // If using ESP32: esp_restart();
            break;
        case CDH_CMD_SYNC_TIME:
            // Update your internal RTC or software clock
            printf("CDHS: Clock synchronized.\n");
            break;
    }
}

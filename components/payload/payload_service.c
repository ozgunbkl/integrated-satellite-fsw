#include "payload_service.h"
#include "payload_sim.h"
#include "archive_service.h"
#include "commands.h"
#include "fdir_service.h"
#include "time_service.h"
#include "yamcs_interface.h"
#include <string.h>
#include <stdio.h>


#define SIM_DATA_SIZE 64    // Size of one "Science Packet"


// Private internal state
static PayloadTelemetry_t pl_telemetry;

void Payload_Init(void) {
    memset(&pl_telemetry, 0, sizeof(PayloadTelemetry_t));
    pl_telemetry.current_state = PL_STATE_OFF;
}

void Payload_Update(void){
    PayloadTelemetry_t* tl = &pl_telemetry;

    // 1. Only do work if the payload is ACTIVE
    if(tl->current_state != PL_STATE_ACTIVE){
        return;
    }

    // 2. Logic to handle the data rate 
    // In SIL, we simulate this. Every time Update is called, 
    // we assume one "cycle" has passed.
    static uint32_t cycle_count = 0;
    cycle_count++;

    // If data_rate is 5Hz, and our loop runs at 10Hz, 
    // we produce data every 2 cycles.
    // For now and simplicity: generate data every update
    uint8_t raw_data[SIM_DATA_SIZE];
    PayloadSim_GenerateData(raw_data, SIM_DATA_SIZE);

    // 3. Route the data to the Archive Service
    ArchiveStatus_t arc_status = Archive_WriteRecord(REC_ID_PAYLOAD, raw_data, SIM_DATA_SIZE);
    if (arc_status == ARCHIVE_OK) {
        tl->bytes_generated += SIM_DATA_SIZE;
    } else {
        // If the Archive is full, the payload should move to ERROR state
        printf("DEBUG: Archive rejected write! Status Code: %d, Size: %d\n", arc_status, SIM_DATA_SIZE);
        tl->current_state = PL_STATE_ERROR;
        tl->error_counter++;

        // Report to fdir
        FaultReport_t payload_fault = {
            .source = SRC_PAYLOAD,
            .severity = FAULT_WARNING,
            .fault_code = FAULT_PAY_SENSOR_ERROR,
            .timestamp = TIME_GetSeconds()
        };
        FDIR_ReportFault(payload_fault);

        printf("PAYLOAD: Archive rejected write! Moving to ERROR state. [Code: %d]\n", arc_status);
    } 
}

PayloadStatus_t Payload_ProcessCommand(PayloadCmd_t cmd, uint8_t param) {
    pl_telemetry.last_cmd_received = cmd;

    switch (cmd) {
        case PL_CMD_INIT:
            if (pl_telemetry.current_state == PL_STATE_OFF || pl_telemetry.current_state == PL_STATE_SAFE){
                pl_telemetry.current_state = PL_STATE_STANDBY;
                return PL_OK;
            }
            break;
        
        case PL_CMD_START:
            if (pl_telemetry.current_state == PL_STATE_STANDBY) {
                pl_telemetry.current_state = PL_STATE_ACTIVE;
                return PL_OK;
            }
            break;

        case PL_CMD_STOP:
            if (pl_telemetry.current_state == PL_STATE_ACTIVE) {
                pl_telemetry.current_state = PL_STATE_STANDBY;
                return PL_OK;
            }
            break;

        case PL_CMD_RESET:
            pl_telemetry.current_state = PL_STATE_OFF;
            pl_telemetry.error_counter = 0;
            return PL_OK;

        case PL_CMD_SET_RATE:
            if(param > 0 && param <= 10){   //Limit 10Hz
                pl_telemetry.data_rate = param;
                return PL_OK;
            } else {
                // Report to fdir
                FaultReport_t rate_fault = {
                    .source = SRC_PAYLOAD,
                    .severity = FAULT_WARNING,
                    .fault_code = FAULT_PAY_INVALID_RATE,
                    .timestamp = TIME_GetSeconds()
                };
                FDIR_ReportFault(rate_fault);
            }
            break;

    }
    
    // If the switch breaks or falls through, it means the command was illegal 
    // for the current state (e.g., trying to START while already ACTIVE).
    FaultReport_t state_fault = {
        .source = SRC_PAYLOAD,
        .severity = FAULT_WARNING,
        .fault_code = FAULT_PAY_ILL_STATE,
        .timestamp = TIME_GetSeconds()
    };
    FDIR_ReportFault(state_fault);


    return PL_ERR_INVALID_STATE;
}

PayloadTelemetry_t Payload_GetTelemetry(void) {
    return pl_telemetry;
}

void PAYLOAD_ProcessCommandWrapper(const uint8_t* payload, uint16_t len) {
    if (len < 1) {
        YAMCS_SendEvent("CMD_NACK: PAYLOAD empty payload");
        return;
    }

    // The first byte is the Command ID (0 to 4)
    PayloadCmd_t cmd = (PayloadCmd_t)payload[0];
    
    // The second byte (if exists) is the parameter (like for SET_RATE)
    uint8_t param = 0;
    if (len >= 2) {
        param = payload[1];
    }

    // Call your existing logic
    PayloadStatus_t result = Payload_ProcessCommand(cmd, param);

    if (result != PL_OK) {
        printf("PAYLOAD: Command %d failed with error %d\n", cmd, result);
        YAMCS_SendEvent("CMD_NACK: PAYLOAD cmd=%u err=%u", (unsigned)cmd, (unsigned)result);
    } else {
        printf("PAYLOAD: Command %d executed successfully.\n", cmd);
        if (cmd == PL_CMD_SET_RATE) {
            YAMCS_SendEvent("CMD_ACK: PAYLOAD_SET_RATE=%u", (unsigned)param);
        } else {
            YAMCS_SendEvent("CMD_ACK: PAYLOAD cmd=%u", (unsigned)cmd);
        }
    }
}
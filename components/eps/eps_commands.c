#include "eps_commands.h"
#include "commands.h" 
#include "fdir_service.h"
#include "time_service.h"
#include <stdio.h>
#include <math.h>


void EPS_ProcessCommand(const uint8_t* payload, uint16_t len) {
    if (payload == NULL || len < 1) return;

    uint8_t command_id = payload[0];

    switch (command_id) {
        case EPS_CMD_HEATER_CTRL:
            if (len >= 2) {
                uint8_t state = payload[1];
                // EPS_SetHeaterState(state);
                printf("EPS: Heater command received (stub; no load change). State: %d\n", state);
            } else {
                // Report malformed command
                FaultReport_t eps_f = { 
                    .source = SRC_EPS, 
                    .severity = FAULT_WARNING,                         
                    .fault_code = FAULT_EPS_HEATER_FAIL, 
                    .timestamp = TIME_GetSeconds() 
                };
                FDIR_ReportFault(eps_f);
            }
            break;

        case EPS_CMD_BUS_RESET:
            printf("EPS: Executing Power Bus Reset...\n");
            // Add reset logic here
            break;

        default:
            // --- REPORT UNKNOWN COMMAND TO FDIR ---
            printf("EPS ERROR: Command ID 0x%02X not recognized.\n", command_id);
            FaultReport_t unknown_f = {
                .source = SRC_EPS,
                .severity = FAULT_WARNING,
                .fault_code = FAULT_EPS_UNKNOWN_CMD,
                .timestamp = TIME_GetSeconds()
            };
            FDIR_ReportFault(unknown_f);
            break;
    }
}


void EPS_Monitor(float battery_voltage) {
    if (!isfinite(battery_voltage) || battery_voltage < 3.3f) {
        FaultReport_t pwr_fault = {
            .source = SRC_EPS,
            .severity = FAULT_CRITICAL, // High severity!
            .fault_code = FAULT_EPS_LOW_VOLTAGE,
            .timestamp = TIME_GetSeconds()
        };
        FDIR_ReportFault(pwr_fault);
        
        // Existing FDIR EPS critical-fault policy requests SAFE, preserving
        // EMERGENCY/CRITICAL. The separate CDH monitor owns the 2.5 V CRITICAL path.
    }
}

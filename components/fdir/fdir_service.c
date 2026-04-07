#include "fdir_service.h"
#include "commands.h"
#include "time_service.h"
#include "yamcs_interface.h"
#include <stdio.h>

#define MAX_REPETITION_THRESHOLD 3
#define WARNING_THRESHOLD 3

static SystemMode_t current_mode = MODE_NOMINAL;
static uint16_t fault_counters[SRC_COUNT];  // One counter for each FaultSource_t

static bool heater_fault_active = true;

void FDIR_Init(void) {
    current_mode = MODE_NOMINAL;
    for (int i = 0; i < SRC_COUNT; i++) {
        fault_counters[i] = 0;
    }
}

void FDIR_ReportFault(FaultReport_t report) {
    
    report.timestamp = TIME_GetSeconds();

    // Safety check: ensure the source index is within bounds
    if (report.source >= SRC_COUNT) {
        return; 
    }

    printf("[%lu] FDIR: Fault from Source %d (Severity: %d) Code: 0x%04X\n",
           report.timestamp,
           report.source,
           report.severity,
           report.fault_code);

    // 1. Immediate Action for FATAL
    if(report.severity == FAULT_FATAL){
        current_mode = MODE_EMERGENCY;
        return;
    }

    // 2. Immediate Action for CRITICAL EPS (Power is life)
    if(report.source == SRC_EPS && report.severity == FAULT_CRITICAL){
        current_mode = MODE_SAFE;
        return;
    }

    // 3. Increment for WARNINGS
    if(report.severity == FAULT_WARNING){
        fault_counters[report.source]++;
    }
}


SystemMode_t FDIR_GetCurrentMode(void) {
    return current_mode;
}

static void Trigger_Recovery_Action(FaultSource_t source) {
    printf("FDIR: Escalating Recovery forSource[%d] - Persistent Limit Reached!\n", source);

    // Recovery Logic: If a warning repeats too much, we degrade the system mode
    if (current_mode == MODE_NOMINAL) {
        current_mode = MODE_DEGRADED;
    }
}

void FDIR_Process(void) {
    // Check every subsystem's fault counter
    for (int i = 0; i < SRC_COUNT; i++) {
        
        // If a subsystem hits the threshold (e.g., 3 warnings)
        if (fault_counters[i] >= WARNING_THRESHOLD) {
            
            // 1. Isolation & Recovery Action
            Trigger_Recovery_Action((FaultSource_t)i);
            
            // 2. Clear the counter
            fault_counters[i] = 0; 
        }
    }
}

bool FDIR_HasActiveFaults(void) {
    for (int i = 0; i < SRC_COUNT; i++) {
        if (fault_counters[i] > 0) {
            return true; // Found a non-zero counter!
        }
    }
    return false; // All counters are zero
}


void FDIR_ProcessCommand(const uint8_t* payload, uint16_t len) {
    if (len < 1) return;

    uint8_t command_id = payload[0];

    switch (command_id) {
        case FDIR_CMD_ACK_FAULT:
            // Earth is acknowledging the issues. 
            // We clear all warning counters to stop escalation.
            for (int i = 0; i < SRC_COUNT; i++) {
                fault_counters[i] = 0;
            }
            printf("FDIR: All fault counters cleared via Ground Command.\n");
            break;

        case FDIR_CMD_RESET_MODE: // Set system mode from ground
            if (len >= 2) {
                const uint8_t requested_mode = payload[1];
                if (requested_mode > (uint8_t)MODE_CRITICAL) {
                    printf("FDIR: Invalid mode %u from Ground.\n", (unsigned)requested_mode);
                    YAMCS_SendEvent("CMD_NACK: FDIR_SET_MODE invalid=%u", (unsigned)requested_mode);
                    break;
                }

                current_mode = (SystemMode_t)requested_mode;
                printf("FDIR: System Mode manually set to %d by Ground.\n", current_mode);
                YAMCS_SendEvent("CMD_ACK: FDIR_SET_MODE=%u", (unsigned)requested_mode);
            } else {
                YAMCS_SendEvent("CMD_NACK: FDIR_SET_MODE missing arg");
            }
            break;

        default:
            printf("FDIR: Unknown Command ID 0x%02X\n", command_id);
            break;
    }
}

void FDIR_SetSystemMode(SystemMode_t new_mode) {
    if (current_mode == new_mode) return; // No change needed

    printf("SYSTEM: Mode changing from %d to %d\n", current_mode, new_mode);
    current_mode = new_mode;

    // Logic for what happens during the change
    if (new_mode == MODE_CRITICAL) {
        // Emergency: Tell ADCS to stop and EPS to shed load
        // (In a real sat, you'd turn off non-essential switches here)
    }
}
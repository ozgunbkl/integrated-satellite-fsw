#include "fdir_service.h"
#include "commands.h"
#include "system_mode.h"
#include "time_service.h"
#include "yamcs_interface.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>

#define WARNING_THRESHOLD 3

static uint16_t fault_counters[SRC_COUNT];  // One counter for each FaultSource_t
static SemaphoreHandle_t s_fault_mutex = NULL;

bool FDIR_Init(void) {
    if (s_fault_mutex != NULL) {
        return false;
    }

    s_fault_mutex = xSemaphoreCreateMutex();
    if (s_fault_mutex == NULL) {
        return false;
    }

    for (int i = 0; i < SRC_COUNT; i++) {
        fault_counters[i] = 0;
    }

    printf("FDIR: Fault counters initialized.\n");
    return true;
}

static bool fdir_lock_counters(TickType_t timeout) {
    return s_fault_mutex != NULL &&
           xSemaphoreTake(s_fault_mutex, timeout) == pdTRUE;
}

void FDIR_ReportFault(FaultReport_t report) {
    
    report.timestamp = TIME_GetSeconds();

    // Safety check: reject invalid enum values before indexing or mode changes.
    if ((unsigned int)report.source >= (unsigned int)SRC_COUNT ||
        (unsigned int)report.severity > (unsigned int)FAULT_FATAL) {
        printf("FDIR: Rejected invalid fault report (source=%d, severity=%d).\n",
               (int)report.source,
               (int)report.severity);
        return; 
    }

    bool recorded = true;
    if (report.severity >= FAULT_CRITICAL) {
        recorded = SYSTEM_MODE_RecordFault((uint8_t)report.source, report.fault_code,
                                          report.severity == FAULT_FATAL, report.timestamp);
    }

    printf("[%lu] FDIR: Fault from Source %d (Severity: %d) Code: 0x%04X\n",
           (unsigned long)report.timestamp,
           report.source,
           report.severity,
           report.fault_code);

    if (report.severity >= FAULT_CRITICAL) {
        if (!recorded) printf("FDIR: Fault record unavailable/full; conservative operating inhibit retained.\n");
        return;
    }

    // 3. Increment for WARNINGS
    if(report.severity == FAULT_WARNING){
        if (!fdir_lock_counters(portMAX_DELAY)) {
            printf("FDIR: Warning counter unavailable for source %d.\n",
                   (int)report.source);
            return;
        }

        if (fault_counters[report.source] < WARNING_THRESHOLD) {
            fault_counters[report.source]++;
        }
        xSemaphoreGive(s_fault_mutex);
    }
}

void FDIR_Process(void) {
    bool recovery_pending[SRC_COUNT] = {false};
    bool any_pending = false;
    if (!fdir_lock_counters(pdMS_TO_TICKS(10))) {
        printf("FDIR: Fault counters unavailable for processing.\n");
        return;
    }

    // Keep counters until the decision commits. ACK and new warnings serialize
    // with this transaction, so no stale snapshot can act after an earlier ACK,
    // and a new warning cannot be erased by completion of an older batch.
    for (int i = 0; i < SRC_COUNT; i++) {
        if (fault_counters[i] >= WARNING_THRESHOLD) {
            recovery_pending[i] = true;
            any_pending = true;
        }
    }

    // Lock order: FDIR counters -> mode. Quiet transitions have a finite 10 ms
    // lock wait and no I/O. Never call a logging mode API under the FDIR mutex.
    // Already DEGRADED also handles the batch; BOOT/safety modes defer it.
    const bool handled = any_pending &&
        (SYSTEM_MODE_TransitionQuiet(MODE_NOMINAL, MODE_DEGRADED) ||
         SYSTEM_MODE_TransitionQuiet(MODE_DEGRADED, MODE_DEGRADED));
    if (handled) {
        for (int i = 0; i < SRC_COUNT; i++) {
            if (recovery_pending[i]) {
                fault_counters[i] = 0;
            }
        }
    }
    xSemaphoreGive(s_fault_mutex);

    // Describe the committed decision, not a guarantee of the current mode.
    for (int i = 0; i < SRC_COUNT; i++) {
        if (handled && recovery_pending[i]) {
            printf("FDIR: Warning threshold handled for source %d; DEGRADED decision committed.\n", i);
        }
    }
}

bool FDIR_HasActiveFaults(void) {
    if (!fdir_lock_counters(portMAX_DELAY)) {
        return true;
    }

    bool has_active_faults = false;
    for (int i = 0; i < SRC_COUNT; i++) {
        if (fault_counters[i] > 0) {
            has_active_faults = true;
            break;
        }
    }
    xSemaphoreGive(s_fault_mutex);
    if (has_active_faults) return true;
    bool blocked = true;
    return !SYSTEM_MODE_HasBlockingFaults(&blocked) || blocked;
}

bool FDIR_GetActiveFaults(FDIR_ActiveFaultSnapshot_t *out) {
    return SYSTEM_MODE_GetFaults(out);
}

bool FDIR_GetFaultRevision(FaultSource_t source, uint16_t code, uint32_t *revision) {
    return (unsigned int)source < (unsigned int)SRC_COUNT &&
           SYSTEM_MODE_GetFaultRevision((uint8_t)source, code, revision);
}

bool FDIR_ResolveCriticalFault(FaultSource_t source, uint16_t code, uint32_t revision) {
    return (unsigned int)source < (unsigned int)SRC_COUNT &&
           SYSTEM_MODE_ResolveFault((uint8_t)source, code, revision);
}

bool FDIR_TryGetFaultRevision(FaultSource_t source, uint16_t code, uint32_t *revision) {
    return (unsigned int)source < (unsigned int)SRC_COUNT &&
           SYSTEM_MODE_TryGetFaultRevision((uint8_t)source, code, revision);
}

bool FDIR_TryResolveCriticalFault(FaultSource_t source, uint16_t code, uint32_t revision) {
    return (unsigned int)source < (unsigned int)SRC_COUNT &&
           SYSTEM_MODE_TryResolveFault((uint8_t)source, code, revision);
}

void FDIR_ProcessCommand(const uint8_t* payload, uint16_t len) {
    if (payload == NULL || len < 1) return;

    uint8_t command_id = payload[0];

    switch (command_id) {
        case FDIR_CMD_ACK_FAULT:
            // Earth is acknowledging the issues. 
            // We clear all warning counters to stop escalation.
            if (!fdir_lock_counters(portMAX_DELAY)) {
                printf("FDIR: Cannot clear warning counters; service unavailable.\n");
                YAMCS_SendEvent("CMD_NACK: FDIR_ACK_FAULT unavailable");
                break;
            }
            for (int i = 0; i < SRC_COUNT; i++) {
                fault_counters[i] = 0;
            }
            xSemaphoreGive(s_fault_mutex);
            printf("FDIR: Warning counters cleared via Ground Command; critical/fatal records retained.\n");
            break;

        case FDIR_CMD_RESET_MODE: // Set system mode from ground
            if (len >= 2) {
                const uint8_t requested_mode = payload[1];
                if (requested_mode > (uint8_t)MODE_CRITICAL) {
                    printf("FDIR: Invalid mode %u from Ground.\n", (unsigned)requested_mode);
                    YAMCS_SendEvent("CMD_NACK: FDIR_SET_MODE invalid=%u", (unsigned)requested_mode);
                    break;
                }

                const SystemMode_t new_mode = (SystemMode_t)requested_mode;
                if (SYSTEM_MODE_Set(new_mode)) {
                    printf("FDIR: System Mode manually set to %s by Ground.\n",
                           SYSTEM_MODE_ToString(new_mode));
                    YAMCS_SendEvent("CMD_ACK: FDIR_SET_MODE=%u", (unsigned)requested_mode);
                } else {
                    printf("FDIR: Failed to set System Mode to %s by Ground.\n",
                           SYSTEM_MODE_ToString(new_mode));
                    YAMCS_SendEvent("CMD_NACK: FDIR_SET_MODE rejected=%u",
                                    (unsigned)requested_mode);
                }
            } else {
                YAMCS_SendEvent("CMD_NACK: FDIR_SET_MODE missing arg");
            }
            break;

        default:
            printf("FDIR: Unknown Command ID 0x%02X\n", command_id);
            break;
    }
}

#include "payload_service.h"
#include "payload_sim.h"
#include "archive_service.h"
#include "commands.h"
#include "fdir_service.h"
#include "time_service.h"
#include "yamcs_interface.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "eps_service.h"


#define SIM_DATA_SIZE 64    // Size of one "Science Packet"


// Private internal state
static PayloadTelemetry_t pl_telemetry;
static SemaphoreHandle_t s_payload_mutex = NULL;
static bool sample_clock_valid;
static uint64_t last_sample_ms;

static bool payload_lock(void) {
    return s_payload_mutex != NULL && xSemaphoreTake(s_payload_mutex, pdMS_TO_TICKS(10)) == pdTRUE;
}

static void payload_permission_only(SystemMode_t mode, void *context) { }

static void payload_start_action(SystemMode_t mode, void *context) {
    pl_telemetry.current_state = PL_STATE_ACTIVE;
    sample_clock_valid = false;
}

void Payload_Init(void) {
    if (s_payload_mutex == NULL) s_payload_mutex = xSemaphoreCreateMutex();
    if (s_payload_mutex == NULL || xSemaphoreTake(s_payload_mutex, portMAX_DELAY) != pdTRUE) {
        printf("PAYLOAD: Required state mutex initialization failed.\n");
        abort();
    }
    memset(&pl_telemetry, 0, sizeof(PayloadTelemetry_t));
    pl_telemetry.current_state = PL_STATE_OFF;
    sample_clock_valid = false;
    xSemaphoreGive(s_payload_mutex);
}

void Payload_Update(void){
    if (!payload_lock()) return;
    PayloadTelemetry_t* tl = &pl_telemetry;

    // 1. Only do work if the payload is ACTIVE
    if(tl->current_state != PL_STATE_ACTIVE){
        xSemaphoreGive(s_payload_mutex);
        return;
    }
    if (!EPS_RunIfPermitted(EPS_OP_PAYLOAD, payload_permission_only, NULL)) {
        tl->current_state = PL_STATE_SAFE;
        xSemaphoreGive(s_payload_mutex);
        printf("PAYLOAD: Permission unavailable; entering SAFE without generating data.\n");
        return;
    }

    // Explicit SET_RATE 1..10 is now a real upper-rate bound. Zero preserves
    // the legacy unconfigured behavior; ground selects 1 Hz before this soak.
    // Permission is checked even between samples; no bursts/catch-up retries.
    if (tl->data_rate != 0) {
        const uint64_t now = TIME_GetMilliseconds();
        const uint64_t period = (1000U + tl->data_rate - 1U) / tl->data_rate;
        if (sample_clock_valid && (now < last_sample_ms || now - last_sample_ms < period)) {
            xSemaphoreGive(s_payload_mutex);
            return;
        }
        sample_clock_valid = true;
        last_sample_ms = now;
    }
    uint8_t raw_data[SIM_DATA_SIZE];
    PayloadSim_GenerateData(raw_data, SIM_DATA_SIZE);

    // Recheck before archive submission; discard a prepared sample if permission was lost.
    if (!EPS_RunIfPermitted(EPS_OP_PAYLOAD, payload_permission_only, NULL)) {
        tl->current_state = PL_STATE_SAFE;
        xSemaphoreGive(s_payload_mutex);
        printf("PAYLOAD: Permission lost during preparation; sample discarded, entering SAFE.\n");
        return;
    }

    // A transaction past this last authorization may finish if safety changes
    // before/during submission. Archive/FDIR cannot run under EPS/mode locks.
    ArchiveStatus_t arc_status = Archive_WriteRecord(REC_ID_PAYLOAD, raw_data, SIM_DATA_SIZE);
    if (arc_status == ARCHIVE_OK) {
        tl->bytes_generated += SIM_DATA_SIZE;
    } else {
        // If the Archive is full, the payload should move to ERROR state
        tl->current_state = PL_STATE_ERROR;
        tl->error_counter++;

    }
    xSemaphoreGive(s_payload_mutex);
    if (arc_status != ARCHIVE_OK) {
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
    if (!payload_lock()) return PL_ERR_UNAVAILABLE;
    pl_telemetry.last_cmd_received = cmd;
    PayloadStatus_t result = PL_ERR_INVALID_STATE;
    uint16_t fault_code = FAULT_PAY_ILL_STATE;

    switch (cmd) {
        case PL_CMD_INIT:
            if (pl_telemetry.current_state == PL_STATE_OFF || pl_telemetry.current_state == PL_STATE_SAFE){
                pl_telemetry.current_state = PL_STATE_STANDBY;
                result = PL_OK;
            }
            break;
        
        case PL_CMD_START:
            if (pl_telemetry.current_state == PL_STATE_STANDBY) {
                result = EPS_RunIfPermitted(EPS_OP_PAYLOAD, payload_start_action, NULL)
                    ? PL_OK : PL_ERR_INHIBITED;
            }
            break;

        case PL_CMD_STOP:
            if (pl_telemetry.current_state == PL_STATE_ACTIVE) {
                pl_telemetry.current_state = PL_STATE_STANDBY;
                result = PL_OK;
            }
            break;

        case PL_CMD_RESET:
            pl_telemetry.current_state = PL_STATE_OFF;
            pl_telemetry.error_counter = 0;
            result = PL_OK;
            break;

        case PL_CMD_SET_RATE:
            if(param > 0 && param <= 10){   //Limit 10Hz
                pl_telemetry.data_rate = param;
                sample_clock_valid = false;
                result = PL_OK;
            } else {
                fault_code = FAULT_PAY_INVALID_RATE;
            }
            break;

    }
    
    xSemaphoreGive(s_payload_mutex);
    // Safety denial is an expected condition, not a repetitive FDIR warning.
    if (result == PL_ERR_INVALID_STATE) {
        FaultReport_t state_fault = {.source = SRC_PAYLOAD, .severity = FAULT_WARNING,
            .fault_code = fault_code, .timestamp = TIME_GetSeconds()};
        FDIR_ReportFault(state_fault);
    }
    return result;
}

PayloadTelemetry_t Payload_GetTelemetry(void) {
    PayloadTelemetry_t telemetry = {.current_state = PL_STATE_SAFE};
    if (!payload_lock()) return telemetry;
    telemetry = pl_telemetry;
    xSemaphoreGive(s_payload_mutex);
    return telemetry;
}

void PAYLOAD_ProcessCommandWrapper(const uint8_t* payload, uint16_t len) {
    if (payload == NULL || len < 1) {
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

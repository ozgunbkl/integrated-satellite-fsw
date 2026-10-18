#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <math.h>
#include "eps_telemetry.h"
#include "eps_commands.h"
#include "fdir_service.h"
#include "time_service.h"
#include "system_mode.h"
#include "eps_service.h"
#include "eps_sim_model.h"
#include "watchdog.h"

static eps_mode_t s_current_mode = EPS_MODE_NOMINAL;

static float last_solar_power = 0.0f;

float EPS_CalculateSoC(float voltage);

typedef struct {
    float voltage;
    bool report_low_voltage;
} EpsLoadPolicy_t;

// Only called with xEPSDataMutex held. Preserve COMMS and unrelated PDU bits.
static void eps_shed_loads_locked(void) {
    g_eps_telemetry.ul_PDU_Register &= ~(PDU_LOAD_PAYLOAD_BIT | PDU_LOAD_HEATER_BIT);
    g_eps_telemetry.xFaultFlags.safe_mode_active = 1;
}

// Both EPS data and system mode are locked. No logging, FDIR, or nested locks.
static void eps_apply_load_policy(SystemMode_t system_mode, void *context) {
    const EpsLoadPolicy_t *policy = context;
    const bool low_voltage = !isfinite(policy->voltage) || policy->voltage < EPS_VOLTAGE_CRITICAL_LOW;

    switch (system_mode) {
        case MODE_INITIAL_BOOT:
        case MODE_NOMINAL:
        case MODE_DEGRADED:
            break;
        case MODE_SAFE:
        case MODE_EMERGENCY:
        case MODE_CRITICAL:
        default:
            eps_shed_loads_locked();
            s_current_mode = EPS_MODE_SAFE;
            return;
    }

    // Shed immediately on the detection cycle, including a recovery relapse.
    if (low_voltage) {
        eps_shed_loads_locked();
        s_current_mode = policy->report_low_voltage ? EPS_MODE_LOW_VOLTAGE : EPS_MODE_SAFE;
        return;
    }

    switch (s_current_mode) {
        case EPS_MODE_NOMINAL:
            // Preserve healthy boot defaults and any already-off PDU bits.
            break;
        case EPS_MODE_LOW_VOLTAGE:
            eps_shed_loads_locked();
            s_current_mode = EPS_MODE_SAFE;
            break;
        case EPS_MODE_SAFE:
            // INITIAL_BOOT never authorizes a recovery load-enable operation.
            if ((system_mode == MODE_NOMINAL || system_mode == MODE_DEGRADED) &&
                policy->voltage >= EPS_VOLTAGE_RECOVERY) {
                g_eps_telemetry.ul_PDU_Register |= PDU_LOAD_PAYLOAD_BIT | PDU_LOAD_HEATER_BIT;
                g_eps_telemetry.xFaultFlags.safe_mode_active = 0;
                s_current_mode = EPS_MODE_RECOVERY;
            } else {
                eps_shed_loads_locked();
            }
            break;
        case EPS_MODE_RECOVERY:
            if (system_mode == MODE_NOMINAL || system_mode == MODE_DEGRADED) {
                s_current_mode = EPS_MODE_NOMINAL;
            } else {
                eps_shed_loads_locked();
                s_current_mode = EPS_MODE_SAFE;
            }
            break;
        default:
            eps_shed_loads_locked();
            s_current_mode = EPS_MODE_SAFE;
            break;
    }
}


void vEPS_Manager_Task(void *pvParameters){
    while (1) {
        // Revision is captured BEFORE the health sample. A later report cannot
        // be cleared by this observation. Missing/failed query never clears.
        uint32_t low_fault_revision = 0;
        const bool revision_available = FDIR_GetFaultRevision(SRC_EPS, FAULT_EPS_LOW_VOLTAGE,
                                                              &low_fault_revision);
        // Own the telemetry and local EPS state before consulting global mode.
        if (xSemaphoreTake(xEPSDataMutex, portMAX_DELAY) == pdTRUE) {
            
            float voltage = g_eps_telemetry.f_BusVoltage;
            // This quiet ledger operation takes only MODE, not the FDIR warning
            // mutex. EPS -> mode order; validation and clear precede data unlock.
            const bool low_fault_resolved = revision_available && low_fault_revision != 0 &&
                isfinite(voltage) && voltage >= EPS_VOLTAGE_RECOVERY &&
                FDIR_ResolveCriticalFault(SRC_EPS, FAULT_EPS_LOW_VOLTAGE, low_fault_revision);
            g_eps_telemetry.f_StateOfCharge = EPS_CalculateSoC(voltage); // Update SOC based on voltage

            // --- Post-Eclipse Detection ---
            float current_solar = g_eps_telemetry.f_SolarInputPower; // Assuming this is in your struct

            const bool sunrise_detected = current_solar > 0.5f && last_solar_power <= 0.1f;
            last_solar_power = current_solar;
            // Environment owns solar input; manager must not compound it.

            // Authorize and write PDU bits against the same locked global mode.
            const eps_mode_t previous_mode = s_current_mode;
            const uint32_t previous_pdu = g_eps_telemetry.ul_PDU_Register;
            EpsLoadPolicy_t policy = {
                .voltage = voltage,
                // Non-finite samples are reported by EPS_Monitor, not twice here.
                .report_low_voltage = isfinite(voltage) && voltage < EPS_VOLTAGE_CRITICAL_LOW &&
                    (previous_mode == EPS_MODE_NOMINAL || previous_mode == EPS_MODE_RECOVERY)
            };
            g_eps_telemetry.xFaultFlags.low_voltage_warn = !isfinite(voltage) || voltage < EPS_VOLTAGE_CRITICAL_LOW;
            const bool mode_available = SYSTEM_MODE_WithCurrent(eps_apply_load_policy, &policy);
            if (!mode_available) {
                // Unknown permission must never enable non-essential loads.
                eps_shed_loads_locked();
                s_current_mode = EPS_MODE_SAFE;
            }
            const eps_mode_t updated_mode = s_current_mode;
            const uint32_t updated_pdu = g_eps_telemetry.ul_PDU_Register;

            // Release the Mutex
            xSemaphoreGive(xEPSDataMutex);

            // Fault reporting and diagnostics run outside both state locks.
            if (low_fault_resolved) {
                printf("EPS: Verified %.2f V recovery; low-voltage fault resolved, system mode unchanged.\n", voltage);
            }
            if (sunrise_detected) {
                printf("MANAGER: Sunrise detected; solar input unchanged, normal recovery policy retained.\n");
            }
            EPS_Monitor(voltage); // Existing 3.3 V fault path plus invalid samples.
            if (policy.report_low_voltage) {
                FaultReport_t low_v_fault = {
                    .source = SRC_EPS,
                    .severity = FAULT_CRITICAL,
                    .fault_code = FAULT_EPS_LOW_VOLTAGE,
                    .timestamp = TIME_GetSeconds()
                };
                FDIR_ReportFault(low_v_fault);
            }
            if (previous_mode != updated_mode || previous_pdu != updated_pdu) {
                printf("MANAGER: EPS mode %d -> %d; PDU 0x%08lX -> 0x%08lX; global permission %s.\n",
                       (int)previous_mode, (int)updated_mode,
                       (unsigned long)previous_pdu, (unsigned long)updated_pdu,
                       mode_available ? "checked" : "unavailable (loads shed)");
            }
        }

        // Liveness of the completed loop, not proof of EPS health. No data/mode
        // lock is held when the heartbeat service (or its FDIR failure path) runs.
        watchdog_pet(WDT_TASK_EPS_MANAGER);
        // The Manager Task runs frequently to respond fast to faults.
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}


// This allows other tasks to check if we are in SAFE mode or NOMINAL
eps_mode_t EPS_GetMode(void) {
    if (xEPSDataMutex == NULL ||
        xSemaphoreTake(xEPSDataMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return EPS_MODE_SAFE;
    }
    const eps_mode_t mode = s_current_mode;
    xSemaphoreGive(xEPSDataMutex);
    return mode;
}

typedef struct {
    EpsOperation_t operation;
    SystemModeAction_t action;
    void *context;
    bool applied;
} EpsOperationContext_t;

static void eps_authorize_operation(SystemMode_t mode, void *context) {
    EpsOperationContext_t *request = context;
    const bool normal = mode == MODE_NOMINAL || mode == MODE_DEGRADED;
    bool mode_allowed = normal;
    if (request->operation == EPS_OP_DETUMBLE) {
        mode_allowed = normal || mode == MODE_INITIAL_BOOT;
    } else if (request->operation == EPS_OP_ORBIT_BOOST) {
        mode_allowed = mode == MODE_NOMINAL;
    }
    if (!mode_allowed || s_current_mode != EPS_MODE_NOMINAL ||
        !isfinite(g_eps_telemetry.f_BusVoltage) ||
        !(g_eps_telemetry.f_BusVoltage >= EPS_VOLTAGE_CRITICAL_LOW) ||
        g_eps_telemetry.xFaultFlags.safe_mode_active) {
        return;
    }
    if (request->operation == EPS_OP_PAYLOAD &&
        !(g_eps_telemetry.ul_PDU_Register & PDU_LOAD_PAYLOAD_BIT)) {
        return;
    }
    request->action(mode, request->context);
    request->applied = true;
}

bool EPS_RunIfPermitted(EpsOperation_t operation, SystemModeAction_t action, void *context) {
    if ((unsigned int)operation > (unsigned int)EPS_OP_PAYLOAD || action == NULL ||
        xEPSDataMutex == NULL || xSemaphoreTake(xEPSDataMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }
    EpsOperationContext_t request = {
        .operation = operation, .action = action, .context = context, .applied = false
    };
    const bool checked = SYSTEM_MODE_WithCurrent(eps_authorize_operation, &request);
    xSemaphoreGive(xEPSDataMutex);
    return checked && request.applied;
}

float EPS_CalculateSoC(float voltage) {
    return EPS_SIM_StateOfCharge(voltage);
}

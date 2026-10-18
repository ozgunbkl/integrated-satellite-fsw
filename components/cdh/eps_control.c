// src/eps_control.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "eps_control.h"
#include "system_mode.h"
#include "watchdog.h"
#include "eps_telemetry.h"
#include "eps_commands.h"
#include "fdir_service.h"
#include "time_service.h"
#include <stdio.h>
#include <math.h>

#define CRITICAL_BUS_VOLTAGE 2.5f

float Get_Battery_Voltage(void);

void vEPSMonitoringTask(void *pvParameters) {
    printf("CDH EPS Monitor: Now linked to EPS Subsystem Telemetry.\n");

    for(;;) {
        // Snapshot shared EPS telemetry; the current project supplies simulated values.
        if(xSemaphoreTake(xEPSDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            float real_voltage = g_eps_telemetry.f_BusVoltage;
            xSemaphoreGive(xEPSDataMutex);
            SystemMode_t current_mode = SYSTEM_MODE_Get();

            // Report the power fault before requesting the critical power mode.
            const bool invalid_voltage = !isfinite(real_voltage);
            if(invalid_voltage || (real_voltage < CRITICAL_BUS_VOLTAGE && current_mode != MODE_CRITICAL)) {
                
                // --- NEW: REPORT TO THE SYSTEM-WIDE LOG ---
                FaultReport_t global_pwr_fault = {
                    .source = SRC_EPS,
                    .severity = FAULT_CRITICAL,
                    .fault_code = FAULT_EPS_LOW_VOLTAGE,
                    .timestamp = TIME_GetSeconds()
                };
                FDIR_ReportFault(global_pwr_fault);

                // FDIR may have changed the mode to SAFE. Use a fresh snapshot,
                // preserve EMERGENCY, and never overwrite a concurrent decision.
                const SystemMode_t fault_mode = SYSTEM_MODE_Get();
                if (invalid_voltage) {
                    // No measured evidence for the <2.5 V CRITICAL transition.
                    // FDIR has already inhibited operation, preserving stronger modes.
                    printf("CDH: Invalid bus voltage; power fault reported, no recovery authorized.\n");
                } else if (fault_mode == MODE_EMERGENCY) {
                    printf("CDH: Low voltage; observed MODE_EMERGENCY, power transition skipped.\n");
                } else if (fault_mode != MODE_CRITICAL) {
                    if (SYSTEM_MODE_Transition(fault_mode, MODE_CRITICAL)) {
                        printf("CDH: Low voltage! Entered MODE_CRITICAL.\n");
                    } else {
                        printf("CDH: CRITICAL transition not applied (mode changed or lock unavailable); recheck next cycle.\n");
                    }
                }
            }

            else if (real_voltage > 6.0f && current_mode == MODE_CRITICAL) {
                // Voltage recovery alone cannot authorize normal operations.
                if (SYSTEM_MODE_Transition(MODE_CRITICAL, MODE_SAFE)) {
                    printf("CDH: Power Recovery detected (%.2fV). Entered MODE_SAFE; MODE_NOMINAL not requested.\n", real_voltage);
                } else {
                    printf("CDH: SAFE recovery transition not applied (mode changed or lock unavailable); recheck next cycle.\n");
                }
            }
        }

        watchdog_pet(WDT_TASK_EPS_MON);
        
        // Check once per second (10s was way too slow for safety!)
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}


void vEPS_SetSafeModePower(int mode_id) {
    printf("STUB CALLED: EPS received power command (Mode ID: %d).\n", mode_id);
}

static float simulated_v = 8.0f; // Start healthy
float Get_Battery_Voltage(void) {
    return simulated_v; 
}

// src/adcs/adcs_manager.c

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "adcs_service.h" // Access to the prepared ADCS service
#include "adcs_units.h"
#include "watchdog.h"     // To pet the watchdog for this task
#include "adcs_types.h" 
#include "eps_telemetry.h" // To check EPS status for situational events
#include <stdio.h>
#include <math.h>

void vADCS_Manager_Task(void *pvParameters) {
    // app_main prepares the service and initial spin before tasks can use them.
    // Do not reset here: a command may have changed the mode since preparation.
    printf("ADCS TASK: Started. Startup state prepared by app_main.\n");

    for(;;) {
        // 3. RUN THE ENGINE
        // This calls the math, reads sim sensors, and runs B-Dot/PD
        ADCS_Process();

        float current_alt = ADCS_GetAltitude();

        if (current_alt < 500.0f) {
            // This is the "Orbit Maintenance" situational event
            printf("ADCS_TASK: Low Altitude Detected (%.2f km)!\n", current_alt);
            
            // Get the status of both systems
            float alt_before = current_alt; // Record starting point
            // Actual mutation rechecks ADCS, live EPS and global permission together.
            if (ADCS_TryOrbitBoost(50.0f)) {
                printf("ADCS_TASK: Maintenance Complete. Altitude Boosted.\n");
                // MANEUVER SUMMARY PRINT
                printf("\n============================================\n");
                printf("🚀 ORBIT MANEUVER EXECUTED\n");
                printf("Pre-Burn Alt:  %.2f km\n", alt_before);
                printf("Post-Burn Alt: %.2f km\n", ADCS_GetAltitude());
                printf("Status:        ORBIT RAISED SUCCESSFULLY\n");
                printf("============================================\n\n");
            } else {
                // Log specifically why we didn't boost
                printf("ADCS_TASK: [REJECTED] Boost not applied (mode/power/state/fuel permission).\n");
            }
        }
        // Every few seconds, report the altitude
        static int orbit_report_timer = 0;
        if (orbit_report_timer++ >= 50) { 
            printf(">>> ORBIT: Altitude: %.2f km | Status: %s <<<\n", 
                    current_alt, 
                    !isfinite(current_alt) ? "UNAVAILABLE" :
                    (current_alt < 500.0f) ? "CRITICAL (Low Orbit)" : "NOMINAL");
            
            // FDIR: If we get too low, maybe force a mode change to conserve power
            if (current_alt < 480.0f) {
                printf("FDIR: ALTITUDE EMERGENCY! Prepare for re-entry.\n");
            }
            orbit_report_timer = 0;
        }

        // Demo plant step in seconds, shared with the controller's derivative interval.
        ADCS_StepSimulation(ADCS_CYCLE_S);

        watchdog_pet(WDT_TASK_ADCS);

        // 6. Update frequency: 10Hz (100ms) matches DT = 0.1f definition
        vTaskDelay(pdMS_TO_TICKS(ADCS_CYCLE_MS));
    }
}

// src/deployment_service.c

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system_mode.h"
#include <stdio.h>

void vDeploymentTask(void *pvParameters) {
    // Demonstration timing only; no physical actuator/sensor or flight-rule claim.
    const uint32_t DEPLOY_DELAY_MS = 10000; 

    printf("DEPLOY: Simulation starting safety wait (%lu ms)...\n", (unsigned long)DEPLOY_DELAY_MS);

    vTaskDelay(pdMS_TO_TICKS(DEPLOY_DELAY_MS));

    printf("DEPLOY: Simulation safety wait over; simulating deployment (no burn-wire output).\n");
    
    // Simulate the physical deployment
    vTaskDelay(pdMS_TO_TICKS(2000)); 

    // Retry only bookkeeping; never repeat the simulated safety/deployment waits.
    while (!SYSTEM_MODE_CompleteDeployment()) {
        printf("DEPLOY: Completion record unavailable; retrying in 100 ms.\n");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    printf("DEPLOY: Simulation completion recorded; existing safety-mode decisions preserved.\n");

    // Deployment is a one-time thing, so we delete this task to free RAM
    vTaskDelete(NULL); 
}

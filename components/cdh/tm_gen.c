// src/tm_gen.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "satellite_types.h"
#include "task_defs.h"
#include "state_manager.h"
#include "watchdog.h"
#include "hk_service.h"
#include <stdio.h>

extern QueueHandle_t xTelemetryQueue;


void vTelemetryGeneratorTask(void *pvParameters) {
    
    HK_Telemetry_t tx_packet;
    float simulated_voltage = 3.3f;      // Initial simulated voltage value
    float FSW_MIN_BUS_VOLTAGE = 2.8f;    // Critical safety threshold

    printf("TM Generator Task initialized and running.\n");
    for(;;) {
        // Always run HK periodic reporting so Yamcs receives live packets even outside nominal mode.
        HK_RunPeriodic((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));

        if(get_system_mode() == MODE_NOMINAL){
            printf("TM GEN: -- Running NOMINAL Mission Cycle --\n");
            tx_packet.timestamp = xTaskGetTickCount();
            tx_packet.bus_voltage = simulated_voltage;
        
            if(tx_packet.bus_voltage < FSW_MIN_BUS_VOLTAGE) {
                printf("TM GEN: !!! Bus voltage below threshold (%.2f V). Packet discarded. !!!\n", tx_packet.bus_voltage);
            }
            else {
                if(xQueueSend(xTelemetryQueue, &tx_packet, 0) != pdPASS) {
                    printf("WARNING: Telemetry Queue Full, packet lost.\n");
                }
            }
            
        }else{
            printf("TM GEN: Waiting for MODE_NOMINAL. Current mode: %d\n", get_system_mode());
        }

        watchdog_pet(WDT_TASK_TM_GEN);
        
        vTaskDelay(pdMS_TO_TICKS(5000)); 
    }
}
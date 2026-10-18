// src/tm_gen.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "satellite_types.h"
#include "task_defs.h"
#include "system_mode.h"
#include "watchdog.h"
#include "hk_service.h"
#include <stdio.h>
#include <string.h>

extern QueueHandle_t xTelemetryQueue;


void vTelemetryGeneratorTask(void *pvParameters) {
    
    uint16_t sequence_count = 0;
    uint32_t last_archive_ms = 0;
    bool first_archive_cycle = true;
    const float simulated_voltage = 3.3f; // Legacy demo queue, not the EPS bus reading.
    const float FSW_MIN_BUS_VOLTAGE = 2.8f;

    printf("TM Generator Task initialized and running.\n");
    for(;;) {
        // Always run HK periodic reporting so Yamcs receives live packets even outside nominal mode.
        const TickType_t now_ticks = xTaskGetTickCount();
        const uint32_t now_ms = (uint32_t)(now_ticks * portTICK_PERIOD_MS);
        HK_RunPeriodic(now_ms);

        // Keep the legacy archive cadence at 5 s while servicing 1 s HK commands.
        if (first_archive_cycle || (uint32_t)(now_ms - last_archive_ms) >= 5000U) {
            first_archive_cycle = false;
            last_archive_ms = now_ms;
            const SystemMode_t system_mode = SYSTEM_MODE_Get();
            if(system_mode == MODE_NOMINAL){
                HK_Telemetry_t tx_packet;
                // Clear padding/bitfield storage as well as all unmeasured/reserved fields.
                memset(&tx_packet, 0, sizeof(tx_packet));
                printf("TM GEN: -- Running NOMINAL Mission Cycle --\n");
                tx_packet.timestamp = now_ticks;
                tx_packet.sequence_count = sequence_count++;
                tx_packet.status_flags.system_mode = system_mode;
                tx_packet.bus_voltage = simulated_voltage;
                // ext_temp_c and crc_checksum remain zero: no sample/inner CRC in this demo.

                if(tx_packet.bus_voltage < FSW_MIN_BUS_VOLTAGE) {
                    printf("TM GEN: !!! Bus voltage below threshold (%.2f V). Packet discarded. !!!\n", tx_packet.bus_voltage);
                }
                else {
                    if(xQueueSend(xTelemetryQueue, &tx_packet, 0) != pdPASS) {
                        printf("WARNING: Telemetry Queue Full, packet lost.\n");
                    }
                }

            }else{
                printf("TM GEN: Waiting for MODE_NOMINAL. Current mode: %d\n", system_mode);
            }
        }

        watchdog_pet(WDT_TASK_TM_GEN);
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

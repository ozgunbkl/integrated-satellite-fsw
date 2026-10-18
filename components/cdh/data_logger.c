#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "satellite_types.h"
#include "system_mode.h"
#include "watchdog.h"
#include "archive_service.h" 
#include "payload_service.h"
#include <stdio.h>

extern QueueHandle_t xTelemetryQueue;
DownlinkMode_t g_downlink_mode = DOWNLINK_INACTIVE;

void vDataLoggerTask(void *pvParameters){
    HK_Telemetry_t rx_log_packet;
    const TickType_t xLogWaitTime = pdMS_TO_TICKS(100);

    // app_main initializes the archive before starting any task; do not reset it here.

    printf("DATA LOGGER: Task initialized, monitoring telemetry queue.\n");

    for(;;) {
        // --- STEP A: REAL ARCHIVING ---
        // Replace your placeholder printf with a real write operation
        if (xQueueReceive(xTelemetryQueue, &rx_log_packet, xLogWaitTime) == pdPASS) {
            
            ArchiveStatus_t write_status = Archive_WriteRecord(REC_ID_HK, (uint8_t*)&rx_log_packet, sizeof(HK_Telemetry_t));
            
            if (write_status == ARCHIVE_OK) {
                // Optional: low-frequency debug log
                // printf("DATA LOGGER: Record saved at T: %lu\n", rx_log_packet.timestamp);
            } else {
                // A full archive reports its existing FDIR warning; lock failure only
                // returns UNAVAILABLE. The dequeued sample is dropped, not retried.
                printf("DATA LOGGER: Archiving failed! Error: %d\n", write_status);
            }
        }

        // --- STEP B: ARCHIVE READOUT ---
        // The current path only reads records locally; it does not transmit them.
        SystemMode_t current_mode = SYSTEM_MODE_Get();
        if (g_downlink_mode == DOWNLINK_ACTIVE &&
            (current_mode == MODE_NOMINAL || current_mode == MODE_DEGRADED)) {
            printf("DATA LOGGER: --- ARCHIVE READOUT ACTIVE ---\n");

            ArchiveRecord_t out_record;
            for (uint16_t i = 0; i < 50; i++) {
                ArchiveStatus_t read_status = Archive_ReadRecord(i, &out_record);

                if (read_status == ARCHIVE_OK) {
                    printf("DATA LOGGER: Archive record %d [ID: 0x%02X, CRC: 0x%04X]\n",
                            i + 1, out_record.record_id, out_record.crc);
                } else if (read_status == ARCHIVE_EMPTY || read_status == ARCHIVE_ERR_NOT_FOUND) {
                    printf("DATA LOGGER: End of stored data reached at index %d.\n", i);
                    break;
                } else {
                    printf("DATA LOGGER: Archive read failed at index %d (status %d).\n",
                           i, read_status);
                    break;
                }
            }

            printf("DATA LOGGER: Archive readout finished; records retained until transfer is confirmed.\n");
            g_downlink_mode = DOWNLINK_INACTIVE;
        }

        // --- STEP C: PERIODIC SCIENCE LOGGING ---
        // Ensure Payload runs so it can call Archive_WriteRecord for science data
        Payload_Update();

        watchdog_pet(WDT_TASK_DATA_LOG);
    }
}

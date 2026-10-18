// src/tc_proc.c

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "satellite_types.h"
#include "watchdog.h"
#include <stdio.h>
#include <stdint.h>
#include "tc_proc.h"
#include "utils.h"
#include "esp_log.h"
#include "cdhs_router.h"

extern QueueHandle_t xCommandQueue;

/**
 * @brief The Command Processor Task
 * It pulls raw CCSDS packets from the queue, verifies them, and routes them.
 */
void vCommandProcessorTask(void *pvParameters) {
    TelecommandPacket_t rx_command;
    
    printf("TC PROC: Task initialized. Standard: CCSDS Space Packet Protocol.\n");

    for(;;) {
        // Wait for ground commands from the injection task or radio driver
        if(xQueueReceive(xCommandQueue, &rx_command, pdMS_TO_TICKS(100)) == pdPASS) {
            
            // Validate bounds before subtracting the CRC size or reading the buffer.
            if (rx_command.length < TELECOMMAND_MIN_SIZE ||
                rx_command.length > sizeof(rx_command.raw_data)) {
                printf("TC PROC: [LENGTH ERROR] Dropping packet: %u bytes (valid %u-%u).\n",
                       (unsigned)rx_command.length,
                       (unsigned)TELECOMMAND_MIN_SIZE,
                       (unsigned)sizeof(rx_command.raw_data));
            } else {
                // 1. INTEGRITY CHECK (Layer 2)
                uint16_t calculated_crc = crc16_ccitt(
                    rx_command.raw_data, rx_command.length - CCSDS_CRC_SIZE);
                const uint16_t trailer_crc = ((uint16_t)rx_command.raw_data[rx_command.length - 2U] << 8) |
                                              rx_command.raw_data[rx_command.length - 1U];

                if (calculated_crc == trailer_crc && trailer_crc == rx_command.packet_crc) {
                    printf("TC PROC: [CRC OK] Packet Length: %d bytes. Handing to Router...\n", rx_command.length);

                    // 2. ROUTING (Layer 3): pass the complete verified frame.
                    CDHS_RoutePacket(rx_command.raw_data, rx_command.length);
                } else {
                    printf("TC PROC: [CRC ERROR] Dropping packet. Trailer: 0x%04X, Metadata: 0x%04X, Computed: 0x%04X\n",
                           trailer_crc, rx_command.packet_crc, calculated_crc);

                    // Optional: Notify FDIR of a communication link error
                }
            }
        }
        
        // Keep the watchdog happy
        watchdog_pet(WDT_TASK_CMD_PROC);
    }
}

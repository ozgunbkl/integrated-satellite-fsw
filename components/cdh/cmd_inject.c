// src/cmd_inject.c

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "satellite_types.h"
#include "system_mode.h"
#include "utils.h"
#include "ccsds_packet.h"
#include "commands.h"
#include "cdhs_router.h"
#include <string.h>
#include <stdio.h>

extern QueueHandle_t xCommandQueue;
extern DownlinkMode_t g_downlink_mode;

// Helper to simulate a Ground Station "Packing" a CCSDS packet
void Inject_CCSDS_Command(uint16_t apid, uint8_t cmd_id, uint8_t param) {
    TelecommandPacket_t tx_packet;
    memset(&tx_packet, 0, sizeof(TelecommandPacket_t));

    // 1. Manually construct the raw buffer
    // Bytes 0-1: APID (Simplified CCSDS Header simulation)
    tx_packet.raw_data[0] = (uint8_t)(apid >> 8);
    tx_packet.raw_data[1] = (uint8_t)(apid & 0xFF);
    
    // Bytes 2-13: Rest of CCSDS Header (Sequence, Time, etc. - simplified for now)
    // Byte 14: Command ID (Our internal TC_ID)
    tx_packet.raw_data[14] = cmd_id;
    // Byte 15: Parameter (e.g., the new Mode)
    tx_packet.raw_data[15] = param;

    // 2. Set the length (Header 14 bytes + Data 2 bytes + CRC 2 bytes)
    tx_packet.length = 18;

    // 3. Calculate CRC over the raw data (excluding the 2-byte CRC field at the end)
    tx_packet.packet_crc = crc16_ccitt(tx_packet.raw_data, tx_packet.length - 2);

    // 4. Place the CRC at the end of the raw buffer (Professional standard)
    tx_packet.raw_data[16] = (uint8_t)(tx_packet.packet_crc >> 8);
    tx_packet.raw_data[17] = (uint8_t)(tx_packet.packet_crc & 0xFF);

    printf("INJECTOR: Sending APID 0x%03X, CMD %d, CRC 0x%04X\n", apid, cmd_id, tx_packet.packet_crc);
    
    xQueueSend(xCommandQueue, &tx_packet, pdMS_TO_TICKS(100));
}

void vCommandInjectionTask(void *pvParameters) {
    // Wait for system boot
    vTaskDelay(pdMS_TO_TICKS(5000));

    // --- TEST 1: Change Mode to NOMINAL ---
    // APID_CDHS (usually 0x01 in our router), CMD_SET_MODE, Param: MODE_NOMINAL
    Inject_CCSDS_Command(APID_CDHS, TC_SET_MODE, MODE_NOMINAL);

    vTaskDelay(pdMS_TO_TICKS(10000));

    // --- TEST 2: Send EPS Command (Heater Control) ---
    // APID_EPS (usually 0x02), CMD_ID (EPS_CMD_HEATER_CTRL = 1), Param: 1 (ON)
    Inject_CCSDS_Command(APID_EPS, EPS_CMD_HEATER_CTRL, 1);

    vTaskDelay(pdMS_TO_TICKS(10000));

    // --- TEST 3: Ground Station Pass ---
    g_downlink_mode = DOWNLINK_ACTIVE;
    printf("INJECTOR: Ground Station Pass Detected!\n");

    vTaskDelete(NULL); 
}

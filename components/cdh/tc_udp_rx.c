#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "satellite_types.h"
#include "utils.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TC_RX_PORT 10025
#define CCSDS_PRIMARY_HEADER_SIZE 6U
#define CCSDS_SECONDARY_HEADER_SIZE 8U
#define INTERNAL_NOCRC_OVERHEAD (CCSDS_PRIMARY_HEADER_SIZE + CCSDS_SECONDARY_HEADER_SIZE)

extern QueueHandle_t xCommandQueue;

void vYamcsTcReceiverTask(void *pvParameters) {
    (void)pvParameters;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        printf("TC_RX: socket() failed\n");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(TC_RX_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (const struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        printf("TC_RX: bind() failed on port %d\n", TC_RX_PORT);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    printf("TC_RX: listening on UDP %d\n", TC_RX_PORT);

    uint8_t rx_buf[64];

    for (;;) {
        struct sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);
        const int rx_len = recvfrom(sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&src_addr, &src_len);

        if (rx_len <= 0) {
            continue;
        }

        if (rx_len < (int)CCSDS_PRIMARY_HEADER_SIZE) {
            printf("TC_RX: packet too short: %d\n", rx_len);
            continue;
        }

        const uint16_t internal_no_crc_len = (uint16_t)(rx_len + CCSDS_SECONDARY_HEADER_SIZE);
        const uint16_t internal_total_len = (uint16_t)(internal_no_crc_len + 2U);

        if (internal_total_len > sizeof(((TelecommandPacket_t *)0)->raw_data)) {
            printf("TC_RX: packet too large after expansion: %u\n", internal_total_len);
            continue;
        }

        TelecommandPacket_t tc_packet;
        memset(&tc_packet, 0, sizeof(tc_packet));

        memcpy(tc_packet.raw_data, rx_buf, CCSDS_PRIMARY_HEADER_SIZE);
        memset(tc_packet.raw_data + CCSDS_PRIMARY_HEADER_SIZE, 0, CCSDS_SECONDARY_HEADER_SIZE);

        if (rx_len > (int)CCSDS_PRIMARY_HEADER_SIZE) {
            memcpy(
                tc_packet.raw_data + INTERNAL_NOCRC_OVERHEAD,
                rx_buf + CCSDS_PRIMARY_HEADER_SIZE,
                (size_t)(rx_len - (int)CCSDS_PRIMARY_HEADER_SIZE));
        }

        tc_packet.packet_crc = crc16_ccitt(tc_packet.raw_data, internal_no_crc_len);
        tc_packet.raw_data[internal_no_crc_len] = (uint8_t)(tc_packet.packet_crc >> 8);
        tc_packet.raw_data[internal_no_crc_len + 1U] = (uint8_t)(tc_packet.packet_crc & 0xFFU);
        tc_packet.length = internal_total_len;

        if (xQueueSend(xCommandQueue, &tc_packet, pdMS_TO_TICKS(100)) != pdPASS) {
            printf("TC_RX: command queue full, dropped packet\n");
        }
    }
}

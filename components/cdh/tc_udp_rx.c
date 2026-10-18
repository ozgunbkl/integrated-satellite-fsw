#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "satellite_types.h"
#include "utils.h"
#include "yamcs_interface.h"
#include "ccsds_packet.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>

#define TC_RX_PORT YAMCS_TC_PORT
#define CCSDS_PRIMARY_HEADER_SIZE 6U
#define CCSDS_SECONDARY_HEADER_SIZE 8U
#define INTERNAL_NOCRC_OVERHEAD (CCSDS_PRIMARY_HEADER_SIZE + CCSDS_SECONDARY_HEADER_SIZE)
#define UDP_TC_MAX_SIZE (TELECOMMAND_MAX_SIZE - CCSDS_SECONDARY_HEADER_SIZE - CCSDS_CRC_SIZE)

extern QueueHandle_t xCommandQueue;

void vYamcsTcReceiverTask(void *pvParameters) {
    (void)pvParameters;

    if (!YAMCS_IsNetworkStackReady()) {
        printf("TC_RX: network stack unavailable; receiver skipped.\n");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(TC_RX_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int sock = -1;
    // One extra byte makes even truncated oversized datagrams unambiguously too large.
    uint8_t rx_buf[UDP_TC_MAX_SIZE + 1U];

    for (;;) {
        if (sock < 0) {
            sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            const struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
            if (sock < 0 || setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
                bind(sock, (const struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
                printf("TC_RX: socket setup failed; retry in 1 second.\n");
                if (sock >= 0) { close(sock); }
                sock = -1;
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            printf("TC_RX: listening on UDP %d\n", TC_RX_PORT);
        }
        struct sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);
        const int rx_len = recvfrom(sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&src_addr, &src_len);

        if (rx_len < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                printf("TC_RX: receive failed; socket will be reopened.\n");
                close(sock);
                sock = -1;
            }
            vTaskDelay(pdMS_TO_TICKS(100)); // Avoid spin on persistent socket errors.
            continue;
        }
        if (rx_len == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (rx_len > (int)UDP_TC_MAX_SIZE ||
            !CCSDS_ValidateUnsegmentedPacket(rx_buf, (uint16_t)rx_len, true, false) ||
            CCSDS_GetAPID(rx_buf) == APID_IDLE) {
            printf("TC_RX: invalid wire TC header/length: %d\n", rx_len);
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
        // Private queue format adds 8 bytes and a CRC; describe that format in
        // its header instead of retaining the shorter ground datagram length.
        tc_packet.raw_data[0] |= 0x08U;
        const uint16_t internal_data_length = internal_total_len - CCSDS_PRIMARY_HEADER_SIZE - 1U;
        tc_packet.raw_data[4] = (uint8_t)(internal_data_length >> 8);
        tc_packet.raw_data[5] = (uint8_t)internal_data_length;
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

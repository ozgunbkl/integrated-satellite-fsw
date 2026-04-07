#include "yamcs_interface.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <arpa/inet.h>
#include <lwip/sockets.h>
#include "esp_log.h"

#define CCSDS_PRIMARY_HEADER_SIZE 6U
#define YAMCS_MAX_UDP_PACKET 512U
#define YAMCS_MAX_EVENT_TEXT 240U
#define YAMCS_EVENT_PACKET_TEXT_LEN 64U
#define YAMCS_DEBUG_TX 1

static const char *TAG = "YAMCS_IF";

static int s_tm_socket = -1;
static struct sockaddr_in s_tm_addr;
static uint16_t s_seq_counter_per_apid[2048] = {0};
static uint32_t s_tx_count = 0;
static uint8_t s_tm_packet[YAMCS_MAX_UDP_PACKET];
static char s_event_text[YAMCS_MAX_EVENT_TEXT];
static uint8_t s_event_packet[YAMCS_EVENT_PACKET_TEXT_LEN];

static bool yamcs_open_tm_socket(void) {
    if (s_tm_socket >= 0) {
        return true;
    }

    s_tm_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_tm_socket < 0) {
        ESP_LOGE(TAG, "socket() failed, errno=%d", errno);
        return false;
    }

    memset(&s_tm_addr, 0, sizeof(s_tm_addr));
    s_tm_addr.sin_family = AF_INET;
    s_tm_addr.sin_port = htons(YAMCS_TM_PORT);
    s_tm_addr.sin_addr.s_addr = inet_addr(YAMCS_DEST_IP);

#if YAMCS_DEBUG_TX
    ESP_LOGI(TAG, "UDP socket ready, destination=%s:%u", YAMCS_DEST_IP, (unsigned)YAMCS_TM_PORT);
#endif

    return true;
}

static void yamcs_build_ccsds_primary_header(uint8_t *out, uint16_t apid, uint16_t payload_len) {
    const uint16_t apid11 = (uint16_t)(apid & 0x07FFU);
    uint16_t packet_id = apid11;
    uint16_t seq = s_seq_counter_per_apid[apid11]++;
    uint16_t seq_ctrl = (uint16_t)(0xC000U | (seq & 0x3FFFU));
    uint16_t packet_length = (payload_len == 0U) ? 0U : (uint16_t)(payload_len - 1U);

    packet_id = htons(packet_id);
    seq_ctrl = htons(seq_ctrl);
    packet_length = htons(packet_length);

    memcpy(&out[0], &packet_id, sizeof(packet_id));
    memcpy(&out[2], &seq_ctrl, sizeof(seq_ctrl));
    memcpy(&out[4], &packet_length, sizeof(packet_length));
}

bool YAMCS_SendTelemetry(uint16_t apid, void *data, uint16_t len) {
    if (data == NULL && len > 0U) {
        ESP_LOGE(TAG, "invalid telemetry buffer, apid=%u len=%u", apid, len);
        return false;
    }

    if ((uint32_t)len + CCSDS_PRIMARY_HEADER_SIZE > YAMCS_MAX_UDP_PACKET) {
        ESP_LOGE(TAG, "packet too large, apid=%u len=%u", apid, len);
        return false;
    }

    if (!yamcs_open_tm_socket()) {
        return false;
    }

    yamcs_build_ccsds_primary_header(s_tm_packet, apid, len);

    if (len > 0U) {
        memcpy(s_tm_packet + CCSDS_PRIMARY_HEADER_SIZE, data, len);
    }

    const int bytes_to_send = (int)(CCSDS_PRIMARY_HEADER_SIZE + len);
    const int sent = sendto(
        s_tm_socket,
        s_tm_packet,
        bytes_to_send,
        0,
        (const struct sockaddr *)&s_tm_addr,
        (socklen_t)sizeof(s_tm_addr));

    if (sent != bytes_to_send) {
        ESP_LOGE(TAG, "sendto failed, apid=%u len=%u sent=%d errno=%d", apid, len, sent, errno);
        return false;
    }

    s_tx_count++;
#if YAMCS_DEBUG_TX
    // Keep serial output useful without flooding logs.
    if (s_tx_count <= 5U || (s_tx_count % 20U) == 0U || apid == YAMCS_APID_EVENT) {
        ESP_LOGI(TAG, "TX ok #%lu apid=%u payload=%u", (unsigned long)s_tx_count, apid, len);
    }
#endif

    return true;
}

bool YAMCS_SendEvent(const char *format, ...) {
    if (format == NULL) {
        return false;
    }

    va_list args;
    va_start(args, format);
    const int written = vsnprintf(s_event_text, sizeof(s_event_text), format, args);
    va_end(args);

    if (written < 0) {
        return false;
    }

    // XTCE defines EventMessageType as fixed 64 bytes (512 bits).
    // Always send exactly 64 bytes to avoid extractor "64 > remaining" errors.
    memset(s_event_packet, 0, sizeof(s_event_packet));

    size_t copy_len = strnlen(s_event_text, sizeof(s_event_text));
    if (copy_len > (sizeof(s_event_packet) - 1U)) {
        copy_len = sizeof(s_event_packet) - 1U;
    }
    memcpy(s_event_packet, s_event_text, copy_len);

    return YAMCS_SendTelemetry(YAMCS_APID_EVENT, s_event_packet, (uint16_t)sizeof(s_event_packet));
}

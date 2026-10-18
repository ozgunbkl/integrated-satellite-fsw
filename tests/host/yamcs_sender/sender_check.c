/* Real sender code, pthread-backed mutexes and captured (offline) UDP packets. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>
// Resolve host mocks explicitly, independently of the editor's ESP32 include paths.
#include "include/freertos/FreeRTOS.h"
#include "include/freertos/semphr.h"
#include "include/lwip/sockets.h"
#include "yamcs_interface.h"

enum { WORKERS = 4, ITEMS = 200, TM_BASE = 0x200, SINGLE_APID = 0x100 };
struct HostSemaphore { pthread_mutex_t mutex; };
static struct HostSemaphore host_mutex;
static _Thread_local unsigned lock_depth;
static unsigned create_calls, socket_calls, send_calls, format_calls, successful_packets;
static bool fail_create, fail_take, fail_socket, fail_send, short_send, fail_format;
static bool concurrent_phase;
static bool fail_options;
static unsigned close_calls;
static uint16_t expected_sequence[2048];
static bool seen[WORKERS][ITEMS];
static uint8_t expected_data[506];
static size_t expected_length;
static uint16_t expected_apid;

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    create_calls++;
    if (fail_create) { fail_create = false; return NULL; }
    assert(create_calls == 2);
    assert(pthread_mutex_init(&host_mutex.mutex, NULL) == 0);
    return &host_mutex;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait) {
    assert(semaphore == &host_mutex && wait == portMAX_DELAY);
    assert(lock_depth == 0); // Detect nested non-recursive locking immediately.
    if (fail_take) { fail_take = false; return pdFALSE; }
    assert(pthread_mutex_lock(&semaphore->mutex) == 0);
    lock_depth = 1;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    assert(semaphore == &host_mutex && lock_depth == 1);
    lock_depth = 0;
    assert(pthread_mutex_unlock(&semaphore->mutex) == 0);
    return pdTRUE;
}

int __real_vsnprintf(char *buffer, size_t capacity, const char *format, va_list args);
int __wrap_vsnprintf(char *buffer, size_t capacity, const char *format, va_list args) {
    assert(lock_depth == 1);
    format_calls++;
    if (fail_format) { fail_format = false; return -1; }
    return __real_vsnprintf(buffer, capacity, format, args);
}

int test_socket(int domain, int type, int protocol) {
    assert(lock_depth == 1);
    assert(domain == AF_INET && type == SOCK_DGRAM && protocol == IPPROTO_UDP);
    socket_calls++;
    if (fail_socket) { fail_socket = false; errno = ENOMEM; return -1; }
    return 42;
}

int test_close(int fd) {
    assert(lock_depth == 1 && fd == 42);
    close_calls++;
    return 0;
}

int test_setsockopt(int fd, int level, int option, const void *value, socklen_t length) {
    assert(lock_depth == 1 && fd == 42 && level == SOL_SOCKET && option == SO_SNDTIMEO);
    assert(length == sizeof(struct timeval));
    const struct timeval *timeout = value;
    assert(timeout->tv_sec == 0 && timeout->tv_usec == 250000);
    if (fail_options) { fail_options = false; return -1; }
    return 0;
}

static uint16_t read_u16(const uint8_t *bytes) {
    return ((uint16_t)bytes[0] << 8) | bytes[1];
}

ssize_t test_sendto(int socket_fd, const void *buffer, size_t length, int flags,
                    const struct sockaddr *address, socklen_t address_length) {
    send_calls++;
    assert(lock_depth == 1 && socket_fd == 42 && flags == 0);
    assert(length >= 7 && length <= 512);
    assert(address_length == sizeof(struct sockaddr_in));
    const struct sockaddr_in *destination = (const struct sockaddr_in *)address;
    assert(destination->sin_family == AF_INET);
    assert(ntohs(destination->sin_port) == YAMCS_TM_PORT);
    assert(destination->sin_addr.s_addr == inet_addr(YAMCS_DEST_IP));

    uint8_t snapshot[512];
    memcpy(snapshot, buffer, length);
    sched_yield(); // Other workers try to send while this packet remains in use.
    assert(memcmp(snapshot, buffer, length) == 0);
    const uint8_t *packet = buffer;
    uint16_t apid = read_u16(packet);
    assert(apid < 2048);
    assert((read_u16(packet + 2) & 0xC000U) == 0xC000U);
    assert((read_u16(packet + 2) & 0x3FFFU) == (expected_sequence[apid]++ & 0x3FFFU));
    assert(read_u16(packet + 4) == length - 7U);
    const uint8_t *payload = packet + 6;
    size_t payload_length = length - 6U;

    if (!concurrent_phase) {
        assert(apid == expected_apid && payload_length == expected_length);
        assert(memcmp(payload, expected_data, expected_length) == 0);
    } else if (apid == YAMCS_APID_EVENT) {
        assert(payload_length == 64 && payload[63] == 0);
        unsigned worker, item;
        assert(sscanf((const char *)payload, "worker=%u item=%u", &worker, &item) == 2);
        assert(worker < WORKERS && (worker % 2U) == 1 && item < ITEMS);
        char expected[64] = {0};
        snprintf(expected, sizeof(expected), "worker=%u item=%u payload=%u",
                 worker, item, worker ^ item);
        assert(memcmp(payload, expected, sizeof(expected)) == 0);
        assert(!seen[worker][item]);
        seen[worker][item] = true;
    } else {
        assert(payload_length >= 8 && payload_length <= 96 && payload[0] == 0xA5);
        unsigned worker = payload[1];
        unsigned item = read_u16(payload + 2);
        assert(worker < WORKERS && (worker % 2U) == 0 && item < ITEMS);
        assert(apid == TM_BASE + worker);
        assert(payload_length == 8U + ((item * 7U + worker) % 89U));
        for (size_t i = 4; i < payload_length; i++) {
            assert(payload[i] == (uint8_t)(worker ^ item ^ i));
        }
        assert(!seen[worker][item]);
        seen[worker][item] = true;
    }

    if (fail_send) { fail_send = false; errno = EIO; return -1; }
    if (short_send) { short_send = false; return (ssize_t)length - 1; }
    successful_packets++;
    return (ssize_t)length;
}

static void expect_single(uint16_t apid, const void *data, size_t length) {
    assert(length <= sizeof(expected_data));
    expected_apid = apid;
    expected_length = length;
    memcpy(expected_data, data, length);
}

static void *worker_main(void *argument) {
    unsigned worker = *(const unsigned *)argument;
    for (unsigned item = 0; item < ITEMS; item++) {
        if ((worker % 2U) == 1) {
            assert(YAMCS_SendEvent("worker=%u item=%u payload=%u", worker, item, worker ^ item));
        } else {
            uint8_t payload[96];
            uint16_t length = (uint16_t)(8U + ((item * 7U + worker) % 89U));
            payload[0] = 0xA5;
            payload[1] = (uint8_t)worker;
            payload[2] = (uint8_t)(item >> 8);
            payload[3] = (uint8_t)item;
            for (size_t i = 4; i < length; i++) payload[i] = (uint8_t)(worker ^ item ^ i);
            assert(YAMCS_SendTelemetry((uint16_t)(TM_BASE + worker), payload, length));
        }
        assert(lock_depth == 0);
        sched_yield();
    }
    return NULL;
}

static void *link_worker(void *argument) {
    (void)argument;
    for (unsigned i = 0; i < 200; i++) {
        assert(YAMCS_SetLinkConnected(false));
        sched_yield();
        assert(YAMCS_SetLinkConnected(true));
        sched_yield();
    }
    return NULL;
}

int main(void) {
    uint8_t probe[] = {0xA5, 0x12, 0x34};
    expect_single(SINGLE_APID, probe, sizeof(probe));
    assert(!YAMCS_IsNetworkStackReady());
    assert(!YAMCS_MarkNetworkStackReady());
    assert(!YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe)));
    assert(!YAMCS_SendEvent("probe"));
    assert(socket_calls == 0);
    fail_create = true;
    assert(!YAMCS_Init());
    assert(!YAMCS_SendEvent("probe"));
    assert(YAMCS_Init());
    assert(!YAMCS_Init() && create_calls == 2);
    assert(!YAMCS_IsNetworkStackReady());
    assert(!YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe)));
    assert(!YAMCS_SendEvent("probe"));
    assert(socket_calls == 0 && send_calls == 0 && format_calls == 0 && lock_depth == 0);
    fail_take = true;
    assert(!YAMCS_MarkNetworkStackReady());
    assert(!YAMCS_IsNetworkStackReady());
    assert(!YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe)));
    assert(socket_calls == 0 && send_calls == 0);
    assert(YAMCS_MarkNetworkStackReady());
    assert(YAMCS_MarkNetworkStackReady()); // Startup publication is idempotent.
    fail_take = true;
    assert(!YAMCS_IsNetworkStackReady());
    assert(YAMCS_IsNetworkStackReady());
    assert(socket_calls == 0 && lock_depth == 0);
    assert(!YAMCS_IsLinkConnected());
    assert(!YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe)));
    assert(YAMCS_SetLinkConnected(true) && YAMCS_IsLinkConnected());

    uint8_t large[507] = {0};
    assert(!YAMCS_SendTelemetry(SINGLE_APID, NULL, 1));
    assert(!YAMCS_SendTelemetry(SINGLE_APID, NULL, 0));
    assert(!YAMCS_SendTelemetry(SINGLE_APID, large, 0));
    assert(!YAMCS_SendTelemetry(0x0800, large, 1));
    assert(!YAMCS_SendTelemetry(UINT16_MAX, large, 1));
    assert(!YAMCS_SendTelemetry(SINGLE_APID, large, sizeof(large)));
    assert(!YAMCS_SendEvent(NULL));
    assert(lock_depth == 0 && socket_calls == 0);
    fail_take = true;
    assert(!YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe)));
    assert(socket_calls == 0);
    fail_socket = true;
    assert(!YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe)));
    assert(lock_depth == 0);
    fail_options = true;
    assert(!YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe)));
    assert(close_calls == 1);
    assert(YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe)));
    fail_send = true;
    assert(!YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe)));
    assert(lock_depth == 0);
    short_send = true;
    assert(!YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe)));
    assert(lock_depth == 0);
    assert(YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe)));
    assert(close_calls == 3); // Timeout-setup, failed send and short send each close.
    const unsigned before_reconnect = socket_calls;
    assert(YAMCS_SetLinkConnected(false));
    assert(close_calls == 4 && YAMCS_IsNetworkStackReady() && !YAMCS_IsLinkConnected());
    assert(!YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe)));
    assert(!YAMCS_SendEvent("offline"));
    assert(socket_calls == before_reconnect);
    assert(YAMCS_SetLinkConnected(true));
    assert(YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe)));
    assert(socket_calls == before_reconnect + 1);

    memset(large, 0x5A, sizeof(large));
    expect_single(SINGLE_APID, large, 506);
    assert(YAMCS_SendTelemetry(SINGLE_APID, large, 506));
    uint8_t event[64] = {0};
    memcpy(event, "probe 7", 7);
    expect_single(YAMCS_APID_EVENT, event, sizeof(event));
    assert(YAMCS_SendEvent("probe %u", 7U));
    char long_text[401];
    memset(long_text, 'Q', 400);
    long_text[400] = 0;
    memset(event, 'Q', 63);
    event[63] = 0;
    expect_single(YAMCS_APID_EVENT, event, sizeof(event));
    assert(YAMCS_SendEvent("%s", long_text));
    fail_format = true;
    assert(!YAMCS_SendEvent("probe"));
    assert(lock_depth == 0);
    fail_take = true;
    assert(!YAMCS_SendEvent("probe"));
    assert(lock_depth == 0);

    concurrent_phase = true;
    unsigned before_packets = successful_packets;
    unsigned worker_ids[WORKERS];
    pthread_t threads[WORKERS];
    for (unsigned i = 0; i < WORKERS; i++) {
        worker_ids[i] = i;
        assert(pthread_create(&threads[i], NULL, worker_main, &worker_ids[i]) == 0);
    }
    for (unsigned i = 0; i < WORKERS; i++) assert(pthread_join(threads[i], NULL) == 0);
    for (unsigned i = 0; i < WORKERS; i++) {
        for (unsigned j = 0; j < ITEMS; j++) assert(seen[i][j]);
    }
    assert(successful_packets - before_packets == WORKERS * ITEMS);
    assert(socket_calls == 6 && close_calls == 4 && create_calls == 2);
    concurrent_phase = false;
    expect_single(SINGLE_APID, probe, sizeof(probe));
    pthread_t link_thread;
    assert(pthread_create(&link_thread, NULL, link_worker, NULL) == 0);
    for (unsigned i = 0; i < 200; i++) {
        (void)YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe));
        sched_yield();
    }
    assert(pthread_join(link_thread, NULL) == 0);
    assert(YAMCS_IsLinkConnected());
    assert(YAMCS_SendTelemetry(SINGLE_APID, probe, sizeof(probe)));
    assert(pthread_mutex_destroy(&host_mutex.mutex) == 0);
    puts("PASS: 4 workers, 800 intact telemetry/event packets and per-APID sequence checks;");
    puts("      init/lock/socket/send/format failures release safely; payload limits and event truncation checked.");
    puts("      unready stack blocks socket/send/format; readiness publication/query failures stay safe.");
    return 0;
}

/* Real startup policy, sender and TC receiver. SDK/socket/RTOS boundaries are mocked. */
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>
#include "include/freertos/FreeRTOS.h"
#include "include/freertos/semphr.h"
#include "include/freertos/queue.h"
#include "include/freertos/task.h"
#include "include/lwip/sockets.h"
#include "yamcs_interface.h"
#include "utils.h"
#include "startup_network.h"
#include "include/esp_netif.h"
#include "include/esp_event.h"

extern void vYamcsTcReceiverTask(void *parameters);
QueueHandle_t xCommandQueue = NULL;
struct HostSemaphore { bool locked; };
static struct HostSemaphore host_mutex;
static bool fail_take, fail_socket, fail_bind;
static unsigned socket_calls, bind_calls, recv_calls, send_calls, close_calls, delete_calls;
static unsigned queue_calls, crc_calls;
static jmp_buf receive_exit;
static jmp_buf abort_exit;
static bool abort_expected;
static unsigned abort_calls, netif_init_calls, netif_deinit_calls, error_logs;
static esp_err_t netif_result;
static char last_error[256];
static esp_err_t event_result;
static unsigned event_create_calls, event_delete_calls;
static bool event_loop_exists;
static bool fail_options;
static unsigned delay_calls, receive_errors;

void vTaskDelay(TickType_t ticks) {
    assert(ticks == 1000 || ticks == 100 || ticks == 10);
    delay_calls++;
    fail_socket = fail_bind = fail_options = false;
}

int test_setsockopt(int fd, int level, int option, const void *value, socklen_t length) {
    assert(fd == 42 && level == SOL_SOCKET && option == SO_RCVTIMEO);
    assert(length == sizeof(struct timeval));
    const struct timeval *timeout = value;
    assert(timeout->tv_sec == 1 && timeout->tv_usec == 0);
    return fail_options ? -1 : 0;
}

esp_err_t esp_event_loop_create_default(void) {
    ++event_create_calls;
    assert(YAMCS_IsNetworkStackReady());
    assert(event_loop_exists == (event_result == ESP_ERR_INVALID_STATE));
    if (event_result == ESP_OK) {
        event_loop_exists = true;
    }
    return event_result;
}

esp_err_t esp_event_loop_delete_default(void) {
    ++event_delete_calls;
    assert(!"Do not delete a loop after failed creation or touch another owner's loop");
    return ESP_FAIL;
}

esp_err_t esp_netif_init(void) {
    ++netif_init_calls;
    assert(!host_mutex.locked);
    return netif_result;
}

esp_err_t esp_netif_deinit(void) {
    ++netif_deinit_calls;
    assert(!"No unsupported network stack rollback is allowed");
    return ESP_FAIL;
}

const char *esp_err_to_name(esp_err_t error) {
    switch (error) {
        case ESP_FAIL: return "ESP_FAIL";
        case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
        default: return "UNKNOWN_ERROR";
    }
}

void test_log_error(const char *tag, const char *format, ...) {
    assert(strcmp(tag, "MAIN") == 0 || strcmp(tag, "YAMCS_IF") == 0);
    ++error_logs;
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(last_error, sizeof(last_error), format, args);
    va_end(args);
    assert(written > 0 && (unsigned)written < sizeof(last_error));
}

_Noreturn void __wrap_abort(void) {
    assert(abort_expected);
    ++abort_calls;
    longjmp(abort_exit, 1);
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    assert(!host_mutex.locked);
    return &host_mutex;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait) {
    assert(semaphore == &host_mutex && wait == portMAX_DELAY && !host_mutex.locked);
    if (fail_take) { fail_take = false; return pdFALSE; }
    host_mutex.locked = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    assert(semaphore == &host_mutex && host_mutex.locked);
    host_mutex.locked = false;
    return pdTRUE;
}

void vTaskDelete(void *task) {
    assert(task == NULL && !host_mutex.locked);
    delete_calls++;
    // Return deliberately: production must return after deleting itself.
}

int test_socket(int domain, int type, int protocol) {
    assert(domain == AF_INET && type == SOCK_DGRAM && protocol == IPPROTO_UDP);
    assert(!host_mutex.locked); // Only the TC receiver reaches sockets here.
    socket_calls++;
    return fail_socket ? -1 : 42;
}

int test_bind(int socket_fd, const struct sockaddr *address, socklen_t length) {
    assert(socket_fd == 42 && length == sizeof(struct sockaddr_in));
    const struct sockaddr_in *local = (const struct sockaddr_in *)address;
    assert(local->sin_family == AF_INET && ntohs(local->sin_port) == YAMCS_TC_PORT);
    assert(local->sin_addr.s_addr == htonl(INADDR_ANY));
    bind_calls++;
    return fail_bind ? -1 : 0;
}

int test_close(int socket_fd) {
    assert(socket_fd == 42);
    close_calls++;
    return 0;
}

ssize_t test_recvfrom(int socket_fd, void *buffer, size_t length, int flags,
                      struct sockaddr *address, socklen_t *address_length) {
    assert(socket_fd == 42 && buffer != NULL && length == 55 && flags == 0);
    assert(address != NULL && *address_length == sizeof(struct sockaddr_in));
    recv_calls++;
    if (receive_errors > 0) {
        errno = receive_errors == 1 ? EIO : (receive_errors == 2 ? EINTR : EAGAIN);
        receive_errors--;
        return -1;
    }
    // First receive proves a ready stack permits listening without an IP check.
    longjmp(receive_exit, 1);
}

ssize_t test_sendto(int socket_fd, const void *buffer, size_t length, int flags,
                    const struct sockaddr *address, socklen_t address_length) {
    send_calls++;
    assert(false); // Sends must be blocked while the stack is unready.
    return -1;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    queue_calls++;
    assert(false); // This fixture never supplies a received command.
    return pdFALSE;
}

uint16_t crc16_ccitt(const uint8_t *data, size_t length) {
    crc_calls++;
    assert(false);
    return 0;
}

uint64_t TIME_GetMilliseconds(void) { return 0; }

static void check_unready_receiver(void) {
    const unsigned before_deletes = delete_calls;
    vYamcsTcReceiverTask(NULL);
    assert(delete_calls == before_deletes + 1);
    assert(socket_calls == 0 && bind_calls == 0 && recv_calls == 0);
    assert(send_calls == 0 && close_calls == 0 && queue_calls == 0 && crc_calls == 0);
    assert(!host_mutex.locked);
}

int main(void) {
    check_unready_receiver(); // Sender service is not initialized yet.
    assert(YAMCS_Init());
    assert(!YAMCS_IsNetworkStackReady());
    check_unready_receiver(); // A sender mutex alone is not network readiness.
    uint8_t probe = 7;
    assert(!YAMCS_SendTelemetry(0x100, &probe, sizeof(probe)));
    assert(!YAMCS_SendEvent("offline"));
    fail_take = true;
    assert(!YAMCS_MarkNetworkStackReady());
    check_unready_receiver();
    assert(socket_calls == 0 && send_calls == 0);

    const esp_err_t failures[] = {ESP_FAIL, ESP_ERR_NO_MEM, ESP_ERR_INVALID_STATE, 0x7654};
    for (unsigned i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        netif_result = failures[i];
        const unsigned before_init = netif_init_calls;
        const unsigned before_errors = error_logs;
        assert(STARTUP_InitNetworkStack() == netif_result);
        assert(netif_init_calls == before_init + 1 && netif_deinit_calls == 0);
        assert(error_logs == before_errors + 1);
        assert(strstr(last_error, esp_err_to_name(netif_result)) != NULL);
        assert(strstr(last_error, "Wi-Fi skipped; socket access stays disabled.") != NULL);
        assert(!YAMCS_IsNetworkStackReady());
        assert(!YAMCS_SendTelemetry(0x100, &probe, sizeof(probe)));
        assert(!YAMCS_SendEvent("offline"));
        check_unready_receiver();
        assert(socket_calls == 0 && send_calls == 0 && abort_calls == 0);
    }

    netif_result = ESP_OK;
    fail_take = true;
    assert(STARTUP_InitNetworkStack() == ESP_FAIL);
    assert(abort_calls == 0 && netif_init_calls == 5 && netif_deinit_calls == 0);
    assert(!YAMCS_IsNetworkStackReady());
    assert(strstr(last_error, "Failed to publish network stack readiness") != NULL);
    assert(STARTUP_InitNetworkStack() == ESP_OK);
    assert(netif_init_calls == 6 && netif_deinit_calls == 0 && abort_calls == 0);
    fail_take = true;
    check_unready_receiver(); // A failed readiness query also fails closed.
    assert(YAMCS_IsNetworkStackReady());

    const esp_err_t event_results[] = {ESP_OK, ESP_ERR_NO_MEM, ESP_FAIL, ESP_ERR_INVALID_STATE, 0x7654};
    for (unsigned i = 0; i < sizeof(event_results) / sizeof(event_results[0]); ++i) {
        event_result = event_results[i];
        event_loop_exists = event_result == ESP_ERR_INVALID_STATE;
        const unsigned before_errors = error_logs;
        const unsigned before_create = event_create_calls;
        assert(STARTUP_CreateWifiEventLoop() == event_result);
        assert(event_create_calls == before_create + 1 && event_delete_calls == 0);
        assert(netif_init_calls == 6 && netif_deinit_calls == 0 && abort_calls == 0);
        assert(YAMCS_IsNetworkStackReady());
        assert(socket_calls == 0 && bind_calls == 0 && send_calls == 0);
        assert(event_loop_exists == (event_result == ESP_OK || event_result == ESP_ERR_INVALID_STATE));
        if (event_result == ESP_OK) {
            assert(error_logs == before_errors);
        } else {
            assert(error_logs == before_errors + 1);
            assert(strstr(last_error, esp_err_to_name(event_result)) != NULL);
            assert(strstr(last_error, "Wi-Fi skipped; network stack stays ready.") != NULL);
        }
    }

    const unsigned before_socket_fail = delete_calls;
    fail_socket = true;
    if (setjmp(receive_exit) == 0) { vYamcsTcReceiverTask(NULL); assert(false); }
    assert(socket_calls == 2 && bind_calls == 1 && delete_calls == before_socket_fail);
    assert(delay_calls == 1);
    fail_bind = true;
    if (setjmp(receive_exit) == 0) { vYamcsTcReceiverTask(NULL); assert(false); }
    assert(socket_calls == 4 && bind_calls == 3 && close_calls == 1 && delay_calls == 2);
    fail_options = true;
    if (setjmp(receive_exit) == 0) { vYamcsTcReceiverTask(NULL); assert(false); }
    assert(socket_calls == 6 && bind_calls == 4 && close_calls == 2 && delay_calls == 3);
    receive_errors = 3;
    if (setjmp(receive_exit) == 0) {
        vYamcsTcReceiverTask(NULL);
        assert(false);
    }
    assert(socket_calls == 8 && bind_calls == 6 && recv_calls == 7);
    assert(close_calls == 3 && delay_calls == 6 && delete_calls == before_socket_fail && send_calls == 0);
    assert(queue_calls == 0 && crc_calls == 0 && !host_mutex.locked);
    puts("PASS: unready stack blocks real TM/event/TC socket access; query failures fail closed;");
    puts("      ready stack permits TC bind/receive; socket/bind/options errors retry with delay;");
    puts("      receive timeouts retain the socket, fatal receive errors close/reopen without spin.");
    puts("      four SDK init errors return unchanged after one attempt, no rollback/socket access;");
    puts("      success publishes readiness; publication failure returns ESP_FAIL without abort.");
    puts("      event-loop success/four errors use one call, no delete/retry/abort or readiness change;");
    puts("      an existing loop stays untouched and TC remains able to bind/receive.");
    return 0;
}

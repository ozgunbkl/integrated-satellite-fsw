/* A real host mutex shared by archive fixtures; no production RTOS replacement. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/semphr.h"
#include "mutex_mock.h"
#include "archive_port.h"
#include "archive_service.h"
#include "fdir_service.h"

// These isolated archive fixtures do not own a MODE ledger. The integrated
// fdir_faults fixture supplies the real query/resolve path and verifies it.
bool FDIR_TryGetFaultRevision(FaultSource_t source, uint16_t code, uint32_t *revision) {
    assert(ArchiveMock_LockHeld() && source == SRC_ARCHIVE && code == FAULT_ARC_CRC_FAIL);
    (void)revision;
    return false;
}
bool FDIR_TryResolveCriticalFault(FaultSource_t source, uint16_t code, uint32_t revision) {
    (void)source; (void)code; (void)revision;
    assert(!"No known CRC revision in an isolated archive fixture");
    return false;
}

static pthread_mutex_t host_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local bool held, fail_take;
static bool fail_create;
static bool fail_boot_id, zero_boot_id;

void ArchiveMock_FailBootId(void) { fail_boot_id = true; }
void ArchiveMock_ZeroBootId(void) { zero_boot_id = true; }
bool Archive_PortCreateBootId(uint64_t *id) {
    assert(id != NULL && !held);
    if (fail_boot_id) { fail_boot_id = false; return false; }
    *id = zero_boot_id ? 0 : UINT64_C(0x0123456789ABCDEF);
    zero_boot_id = false;
    return true;
}

void ArchiveMock_FailCreate(void) { fail_create = true; }
void ArchiveMock_FailTake(void) { fail_take = true; }
bool ArchiveMock_LockHeld(void) { return held; }

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    if (fail_create) { fail_create = false; return NULL; }
    return &host_mutex;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks) {
    assert(handle == &host_mutex && ticks == pdMS_TO_TICKS(10));
    if (fail_take) { fail_take = false; return pdFALSE; }
    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_nsec += 10000000L;
    if (deadline.tv_nsec >= 1000000000L) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000L; }
    const int result = pthread_mutex_timedlock(&host_mutex, &deadline);
    if (result == ETIMEDOUT) return pdFALSE;
    assert(result == 0 && !held);
    held = true;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
    assert(handle == &host_mutex && held);
    held = false;
    assert(pthread_mutex_unlock(&host_mutex) == 0);
    return pdTRUE;
}
void ArchiveMock_Hold(void) { assert(xSemaphoreTake(&host_mutex, pdMS_TO_TICKS(10)) == pdTRUE); }
void ArchiveMock_Release(void) { assert(xSemaphoreGive(&host_mutex) == pdTRUE); }

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "startup_resources.h"
#include "satellite_types.h"
#include "esp_log.h"

static int queue_slots[2], mutex_slot;
static unsigned fail_at, allocations, aborts, errors, continued;
static char last_error[256];
static StartupResources_t observed;
static jmp_buf abort_exit;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
    ++allocations;
    assert(allocations <= 2);
    if (allocations == 1) {
        assert(length == 10 && item_size == sizeof(HK_Telemetry_t));
    } else {
        assert(length == 5 && item_size == sizeof(TelecommandPacket_t));
    }
    return allocations == fail_at ? NULL : &queue_slots[allocations - 1];
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    ++allocations;
    assert(allocations == 3);
    return allocations == fail_at ? NULL : &mutex_slot;
}

void test_log_error(const char *tag, const char *format, ...) {
    assert(strcmp(tag, "MAIN") == 0);
    ++errors;
    va_list args;
    va_start(args, format);
    int length = vsnprintf(last_error, sizeof(last_error), format, args);
    va_end(args);
    assert(length > 0 && (unsigned)length < sizeof(last_error));
}

_Noreturn void __wrap_abort(void) {
    ++aborts;
    // Catch only the production helper's abort inside this host process.
    longjmp(abort_exit, 1);
}

static void reset_probe(unsigned failing_allocation) {
    fail_at = failing_allocation;
    allocations = aborts = errors = continued = 0;
    last_error[0] = '\0';
    // The helper must clear output fields, not inherit an accidental old handle.
    memset(&observed, 0xA5, sizeof(observed));
}

static void caller_probe(void) {
    STARTUP_CreateRequiredResources(&observed);
    ++continued;
}

int main(int argc, char **argv) {
    // Separate processes model separate boots; no production test-reset API.
    assert(argc == 2);
    const bool repeat = strncmp(argv[1], "repeat_", 7) == 0;
    const char *scenario = repeat ? argv[1] + 7 : argv[1];
    const bool null_output = strcmp(scenario, "null") == 0;
    unsigned failure = 0;
    if (strcmp(scenario, "telemetry_queue") == 0) failure = 1;
    else if (strcmp(scenario, "command_queue") == 0) failure = 2;
    else if (strcmp(scenario, "eps_mutex") == 0) failure = 3;
    else assert(null_output || strcmp(scenario, "normal") == 0 ||
                (repeat && (strcmp(scenario, "normal_other") == 0 ||
                            strcmp(scenario, "normal_null") == 0)));

    reset_probe(failure);
    unsigned char before_first[sizeof(observed)];
    memcpy(before_first, &observed, sizeof(observed));
    if (setjmp(abort_exit) == 0) {
        if (null_output) STARTUP_CreateRequiredResources(NULL);
        else caller_probe();
        assert(!null_output && failure == 0);
    }
    if (null_output) {
        assert(allocations == 0 && aborts == 1 && errors == 1 && continued == 0);
        assert(strstr(last_error, "output is NULL") != NULL);
        assert(memcmp(&observed, before_first, sizeof(observed)) == 0);
    } else if (failure) {
        const char *expected[] = {"telemetry queue", "command queue", "EPS data mutex"};
        assert(allocations == failure && aborts == 1 && errors == 1 && continued == 0);
        assert(strstr(last_error, expected[failure - 1]) != NULL);
        assert(observed.telemetry_queue == (failure == 1 ? NULL : &queue_slots[0]));
        assert(observed.command_queue == (failure <= 2 ? NULL : &queue_slots[1]));
        assert(observed.eps_data_mutex == NULL);
    } else {
        assert(allocations == 3 && aborts == 0 && errors == 0 && continued == 1);
        assert(observed.telemetry_queue == &queue_slots[0]);
        assert(observed.command_queue == &queue_slots[1]);
        assert(observed.eps_data_mutex == &mutex_slot);
    }

    if (repeat) {
        unsigned char after_first[sizeof(observed)], other_before[sizeof(observed)];
        StartupResources_t other;
        memset(&other, 0x5A, sizeof(other));
        memcpy(after_first, &observed, sizeof(observed));
        memcpy(other_before, &other, sizeof(other));
        const unsigned old_allocations = allocations, old_aborts = aborts;
        const unsigned old_errors = errors, old_continued = continued;
        if (setjmp(abort_exit) == 0) {
            if (strcmp(scenario, "normal_other") == 0) STARTUP_CreateRequiredResources(&other);
            else if (strcmp(scenario, "normal_null") == 0) STARTUP_CreateRequiredResources(NULL);
            else caller_probe();
            assert(!"Repeated startup resource creation returned");
        }
        assert(allocations == old_allocations && aborts == old_aborts + 1);
        assert(errors == old_errors + 1 && continued == old_continued);
        assert(strstr(last_error, "already attempted") != NULL);
        assert(memcmp(&observed, after_first, sizeof(observed)) == 0);
        assert(memcmp(&other, other_before, sizeof(other)) == 0);
    }

    printf("PASS: startup resources %s (allocation order/fatal policy/unchanged repeat output).\n", argv[1]);
    return 0;
}

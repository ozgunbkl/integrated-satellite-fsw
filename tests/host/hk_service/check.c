#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "hk_service.h"
#include "commands.h"
#include "adcs_service.h"
#include "eps_telemetry.h"
#include "yamcs_interface.h"
#include "freertos/task.h"

_Static_assert(sizeof(MasterHK_t) == 28, "Keep the current ground packet layout");
_Static_assert(HK_PACKET_SIZE == 25, "Keep table serialization distinct from master HK");

static int hk_slot, eps_slot;
static bool hk_locked, eps_locked, fail_create, fail_hk, fail_eps, fail_give;
static unsigned mutex_creates, hk_takes, hk_gives;
static bool tx_ok = true, reenter;
static uint32_t reenter_time;
static unsigned attempts, adcs_reads, tick_reads;
static TickType_t ticks;
static uint8_t captured[sizeof(MasterHK_t)];
static char last_event[180];
EPS_Telemetry_t g_eps_telemetry;
SemaphoreHandle_t xEPSDataMutex = &eps_slot;

SemaphoreHandle_t xSemaphoreCreateMutex(void) { ++mutex_creates; return fail_create ? NULL : &hk_slot; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t wait) {
    assert(wait == pdMS_TO_TICKS(10)); // Init must not wait forever either.
    // No HK/EPS nesting, even during fault and report paths.
    assert(!hk_locked && !eps_locked);
    if (handle == &hk_slot) {
        ++hk_takes;
        if (fail_hk) return pdFALSE;
        hk_locked = true;
    } else {
        assert(handle == &eps_slot);
        if (fail_eps) return pdFALSE;
        eps_locked = true;
    }
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
    if (handle == &hk_slot) {
        assert(hk_locked); ++hk_gives;
        if (fail_give) return pdFALSE;
        hk_locked = false;
    }
    else { assert(handle == &eps_slot && eps_locked); eps_locked = false; }
    return pdTRUE;
}
TickType_t xTaskGetTickCount(void) {
    assert(!hk_locked && !eps_locked);
    ++tick_reads;
    return ticks;
}
void ADCS_GetTelemetry(ADCSTelemetry_t *out) {
    assert(!hk_locked && !eps_locked);
    ++adcs_reads;
    memset(out, 0, sizeof(*out));
    out->angular_velocity = (Vector3_t){1.0f, -2.0f, 0.5f}; // rad/s, no degree conversion
}
bool YAMCS_SendTelemetry(uint16_t apid, void *data, uint16_t length) {
    assert(!hk_locked && !eps_locked);
    assert(apid == 5 && length == sizeof(captured) && data != NULL);
    memcpy(captured, data, length);
    ++attempts;
    if (reenter) {
        reenter = false;
        HK_RunPeriodic(reenter_time); // Same due slot cannot recurse into another TX.
    }
    return tx_ok;
}
bool YAMCS_SendEvent(const char *format, ...) {
    assert(!hk_locked && !eps_locked);
    va_list args;
    va_start(args, format);
    vsnprintf(last_event, sizeof(last_event), format, args);
    va_end(args);
    return true;
}

static void reset(void) {
    fail_hk = fail_eps = fail_give = false;
    tx_ok = true;
    reenter = false;
    xEPSDataMutex = &eps_slot;
    attempts = adcs_reads = tick_reads = 0;
    assert(HK_Init());
    assert(!hk_locked && !eps_locked && tick_reads == 0 && attempts == 0);
}
static void due(uint32_t now, unsigned expected) {
    HK_RunPeriodic(now);
    assert(attempts == expected);
    assert(!hk_locked && !eps_locked);
}
static void set_interval(uint8_t seconds) {
    const uint8_t cmd[] = {HK_CMD_SET_INTERVAL, seconds};
    HK_ProcessCommand(cmd, sizeof(cmd));
}

static void check_table(void) {
    reset();
    for (unsigned i = 0; i < HK_MAX_PARAMS; ++i) {
        hk_param_id_t id = (hk_param_id_t)i;
        assert(HK_CheckLimits(id) == -1);
        HK_SetLimits(id, 10, 20);
        for (uint32_t value = 9; value <= 21; ++value) {
            assert(HK_UpdateParam(id, value, UINT32_MAX - i) == 0);
            hk_entry_t out;
            assert(HK_ReadParam(id, &out) == 0);
            assert(out.value == value && out.timestamp == UINT32_MAX - i && out.is_valid == 1);
            assert(HK_CheckLimits(id) == (value < 10 || value > 20));
        }
        HK_SetLimits(id, 99, 1); // Inverted limits must not replace [10,20].
        assert(HK_UpdateParam(id, 15, 0) == 0 && HK_CheckLimits(id) == 0);
        HK_SetLimits(id, UINT32_MAX, UINT32_MAX);
        assert(HK_UpdateParam(id, UINT32_MAX, 0) == 0 && HK_CheckLimits(id) == 0);
    }
    const hk_param_id_t invalid[] = {(hk_param_id_t)-1, HK_MAX_PARAMS, (hk_param_id_t)INT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        hk_entry_t out = {.value = 123, .timestamp = 456, .is_valid = 7};
        assert(HK_UpdateParam(invalid[i], 0, 0) == -1);
        assert(HK_ReadParam(invalid[i], &out) == -1);
        assert(out.value == 123 && out.timestamp == 456 && out.is_valid == 7);
        assert(HK_CheckLimits(invalid[i]) == -2);
        HK_SetLimits(invalid[i], 0, 0);
    }
    assert(HK_ReadParam(HK_ID_BATT_TEMP, NULL) == -1);
    set_interval(60);
    reset(); // Also clears limits and restores default interval/epoch.
    for (unsigned i = 0; i < HK_MAX_PARAMS; ++i) {
        assert(HK_CheckLimits((hk_param_id_t)i) == -1);
        assert(HK_UpdateParam((hk_param_id_t)i, 0, 0) == 0);
        assert(HK_CheckLimits((hk_param_id_t)i) == 0);
        assert(HK_UpdateParam((hk_param_id_t)i, 1, 0) == 0);
        assert(HK_CheckLimits((hk_param_id_t)i) == 1);
    }
    due(4999, 0);
    due(5000, 1);
}

static void check_serialization(void) {
    reset();
    uint8_t buffer[HK_PACKET_SIZE + 2];
    memset(buffer, 0xA5, sizeof(buffer));
    assert(HK_Serialize(NULL, HK_PACKET_SIZE) == -1);
    assert(HK_Serialize(buffer + 1, HK_PACKET_SIZE - 1) == -1);
    for (unsigned i = 0; i < sizeof(buffer); ++i) assert(buffer[i] == 0xA5);
    assert(HK_Serialize(buffer + 1, HK_PACKET_SIZE) == HK_PACKET_SIZE);
    for (unsigned i = 1; i <= HK_PACKET_SIZE; ++i) assert(buffer[i] == 0);
    for (unsigned i = 0; i < HK_MAX_PARAMS; ++i) {
        assert(HK_UpdateParam((hk_param_id_t)i, 0x12345678U + i, 999) == 0);
    }
    assert(HK_Serialize(buffer + 1, HK_PACKET_SIZE + 1) == HK_PACKET_SIZE);
    assert(buffer[0] == 0xA5 && buffer[sizeof(buffer) - 1] == 0xA5);
    for (unsigned i = 0; i < HK_MAX_PARAMS; ++i) {
        const uint8_t expected[] = {0x12, 0x34, 0x56, (uint8_t)(0x78 + i), 1};
        assert(memcmp(buffer + 1 + i * HK_ENTRY_SIZE, expected, sizeof(expected)) == 0);
    }
}

static void check_master_packet(void) {
    reset();
    g_eps_telemetry.f_BusVoltage = 8.5f;
    g_eps_telemetry.f_StateOfCharge = 75.0f;
    ticks = 0x01020304U;
    assert(HK_UpdateParam(HK_ID_BATT_TEMP, 25, 0) == 0);
    HK_GenerateReport();
    const uint8_t expected[] = {
        0x01,0x02,0x03,0x04, 0x41,0x08,0,0, 0x42,0x96,0,0, 0x41,0xC8,0,0,
        0x3F,0x80,0,0, 0xC0,0,0,0, 0x3F,0,0,0
    };
    assert(attempts == 1 && tick_reads == 1 && adcs_reads == 1);
    assert(memcmp(captured, expected, sizeof(expected)) == 0);
    reset();
    HK_GenerateReport();
    const uint8_t zero[] = {0,0,0,0}; // Existing missing-temperature representation.
    assert(memcmp(captured + 12, zero, sizeof(zero)) == 0);
}

static void check_timing_commands(void) {
    reset();
    due(0, 0); due(4999, 0); due(5000, 1); due(5000, 1);
    due(9999, 1); due(10000, 2);
    due(60000, 3); due(60000, 3); due(64999, 3); due(65000, 4); // No catch-up burst.
    reset();
    const uint32_t before_wrap = UINT32_MAX - 1000U;
    due(before_wrap, 1); due(3998, 1); due(3999, 2); // Exactly 5 s across wrap.
    for (unsigned seconds = 1; seconds <= 60; ++seconds) {
        reset(); set_interval((uint8_t)seconds);
        due(seconds * 1000U - 1U, 0); due(seconds * 1000U, 1);
    }
    const uint8_t invalid[] = {0, 61, 255};
    for (unsigned i = 0; i < sizeof(invalid); ++i) {
        reset(); set_interval(invalid[i]);
        assert(strstr(last_event, "CMD_NACK") != NULL);
        due(4999, 0); due(5000, 1);
    }
    reset();
    const uint8_t missing[] = {HK_CMD_SET_INTERVAL};
    const uint8_t unknown[] = {0xFF};
    HK_ProcessCommand(NULL, 2); HK_ProcessCommand(unknown, 0);
    HK_ProcessCommand(missing, sizeof(missing)); HK_ProcessCommand(unknown, sizeof(unknown));
    due(4999, 0); due(5000, 1);
    set_interval(1); due(5999, 1); due(6000, 2);
    set_interval(60); due(65999, 2); due(66000, 3);
    const uint8_t force[] = {HK_CMD_FORCE_REPORT};
    HK_ProcessCommand(force, sizeof(force));
    assert(attempts == 4);
    due(125999, 4); due(126000, 5); // Force leaves periodic deadline unchanged.
}

static void check_failures(void) {
    reset();
    fail_hk = true;
    hk_entry_t out = {.value = 7};
    uint8_t buffer[HK_PACKET_SIZE];
    memset(buffer, 0xA5, sizeof(buffer));
    assert(HK_ReadParam(HK_ID_BATT_TEMP, &out) == -1 && out.value == 7);
    assert(HK_UpdateParam(HK_ID_BATT_TEMP, 99, 0) == -1);
    assert(HK_CheckLimits(HK_ID_BATT_TEMP) == -1);
    assert(HK_Serialize(buffer, sizeof(buffer)) == -1 && buffer[0] == 0xA5);
    HK_SetLimits(HK_ID_BATT_TEMP, 1, 2);
    set_interval(1);
    HK_GenerateReport(); due(5000, 0);
    fail_hk = false;
    assert(HK_ReadParam(HK_ID_BATT_TEMP, &out) == 0 && !out.is_valid);
    due(4999, 0); // Failed interval command did not take effect.
    fail_eps = true;
    due(5000, 0);
    assert(strstr(last_event, "snapshot unavailable") != NULL);
    fail_eps = false;
    due(5000, 0); due(9999, 0); due(10000, 1); // Failed snapshot consumes its slot.
    xEPSDataMutex = NULL;
    HK_GenerateReport(); assert(attempts == 1);
    reset();
    tx_ok = false;
    due(5000, 1);
    assert(strstr(last_event, "TX failed") != NULL);
    due(5000, 1); due(9999, 1); due(10000, 2);
    tx_ok = true; reenter = true; reenter_time = 15000;
    due(15000, 3);
}

static void check_initialization(const char *scenario) {
    if (strcmp(scenario, "init_allocation") == 0 || strcmp(scenario, "init_lock") == 0 ||
        strcmp(scenario, "init_unlock") == 0) {
        fail_create = strcmp(scenario, "init_allocation") == 0;
        fail_hk = strcmp(scenario, "init_lock") == 0;
        fail_give = strcmp(scenario, "init_unlock") == 0;
        assert(!HK_Init());
        assert(mutex_creates == 1 && hk_takes == (fail_create ? 0U : 1U));
        assert(hk_gives == (fail_give ? 1U : 0U));
        assert(hk_locked == fail_give && !eps_locked);
        assert(attempts == 0 && tick_reads == 0 && adcs_reads == 0 && last_event[0] == '\0');
        // No cleanup/retry is fabricated after release failure: production aborts.
        return;
    }

    reset();
    assert(mutex_creates == 1);
    assert(HK_UpdateParam(HK_ID_BATT_TEMP, 18, 1234) == 0);
    HK_SetLimits(HK_ID_BATT_TEMP, 10, 20);
    set_interval(60);
    due(60000, 1);
    if (strcmp(scenario, "init_lock_preserves_seed") == 0) {
        fail_hk = true;
        const unsigned before_takes = hk_takes, before_gives = hk_gives;
        assert(!HK_Init());
        assert(hk_takes == before_takes + 1 && hk_gives == before_gives && !hk_locked);
        fail_hk = false; // Host-only inspection; production does not resume after false.
        hk_entry_t entry;
        assert(HK_ReadParam(HK_ID_BATT_TEMP, &entry) == 0);
        assert(entry.value == 18 && entry.timestamp == 1234 && entry.is_valid == 1);
        assert(HK_CheckLimits(HK_ID_BATT_TEMP) == 0);
        due(119999, 1); due(120000, 2); // Interval and reserved due time both survived.
    } else {
        assert(strcmp(scenario, "init_reuses_mutex") == 0);
        assert(HK_Init());
        hk_entry_t entry;
        assert(HK_ReadParam(HK_ID_BATT_TEMP, &entry) == 0);
        assert(entry.value == 0 && entry.timestamp == 0 && entry.is_valid == 0);
        assert(HK_UpdateParam(HK_ID_BATT_TEMP, 1, 0) == 0);
        assert(HK_CheckLimits(HK_ID_BATT_TEMP) == 1); // Default [0,0] restored.
        due(4999, 1); due(5000, 2); // Default period 5 s and due epoch 0 restored.
    }
    assert(mutex_creates == 1 && !hk_locked && !eps_locked);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    if (strcmp(argv[1], "normal") != 0) {
        check_initialization(argv[1]);
        printf("PASS: HK required initialization case %s; explicit result and bounded acquisition.\n", argv[1]);
        return 0;
    }
    fail_create = true;
    assert(!HK_Init());
    due(5000, 0); HK_GenerateReport(); assert(attempts == 0);
    fail_create = false;
    check_table();
    check_serialization();
    check_master_packet();
    check_timing_commands();
    check_failures();
    puts("PASS: HK bounds/limits/reset, serialization canaries, 28-byte BE master packet,"
         " 1..60 s commands, wrap/no-burst scheduling and bounded-lock/TX failures.");
    return 0;
}

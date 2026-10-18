/* Real recovery + MODE, persistent bytes/SDK mutex boundary mocked. No flash,
 * archive deletion, serial or real reset. Each case starts a new host process. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recovery.h"
#include "recovery_port.h"
#include "system_mode.h"
#include "commands.h"
#include "freertos/semphr.h"

static const char *scenario;
static uint8_t disk[RECOVERY_RECORD_SIZE];
static bool have_disk, rheld, mheld, fail_lock, fail_save;
static unsigned saves, releases, loads;
static bool is(const char *s) { return strcmp(scenario, s) == 0; }
static uint32_t crc(const uint8_t *p, unsigned n) {
    uint32_t x = ~0U;
    while (n--) { x ^= *p++; for (unsigned i = 0; i < 8; ++i) x = (x >> 1) ^ (0xEDB88320U & (0U - (x & 1U))); }
    return ~x;
}
static void set32(unsigned offset, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) disk[offset + i] = (uint8_t)(n >> (24U - i * 8U));
}
static void seed(void) {
    have_disk = true; memcpy(disk, "FSR1", 4); disk[4] = 1; disk[5] = 48;
    set32(8, 7); set32(12, is("failed_boots") ? 2 : 0);
    set32(16, is("failed_boots") || is("unfinished") ? RECOVERY_OPEN_BOOT :
          is("history") || is("approved_history") ? RECOVERY_FAULT : 0);
    if (is("approved_history")) set32(36, 7);
    if (is("counter_exhausted")) set32(8, UINT32_MAX);
    if (is("invalid_flags")) set32(16, 128);
    if (is("invalid_version")) disk[4] = 2;
    if (is("invalid_reserved")) disk[43] = 1;
    if (is("invalid_boot")) set32(8, 0);
    set32(44, crc(disk, 44));
    if (is("corrupt")) disk[20] ^= 1;
}
bool RECOVERY_PORT_InitLock(void) { return !is("lock_init_failure"); }
bool RECOVERY_PORT_Lock(void) {
    assert(!rheld && !mheld);
    if (fail_lock) { fail_lock = false; return false; }
    rheld = true; return true;
}
void RECOVERY_PORT_Unlock(void) { assert(rheld && !mheld); rheld = false; }
RecoveryLoad_t RECOVERY_PORT_Load(uint8_t p[RECOVERY_RECORD_SIZE]) {
    ++loads;
    if (is("load_error")) return RECOVERY_LOAD_ERROR;
    if (!have_disk) return RECOVERY_LOAD_EMPTY;
    memcpy(p, disk, sizeof(disk)); return RECOVERY_LOAD_OK;
}
bool RECOVERY_PORT_Save(const uint8_t p[RECOVERY_RECORD_SIZE]) {
    assert(!mheld && memcmp(p, "FSR1", 4) == 0 && p[5] == 48);
    assert(crc(p, 44) == ((uint32_t)p[44] << 24 | (uint32_t)p[45] << 16 | (uint32_t)p[46] << 8 | p[47]));
    ++saves;
    if (is("release_race") && saves == 3) SYSTEM_MODE_LatchRecoveryFailure();
    if (is("init_write_error") || fail_save) return false;
    memcpy(disk, p, sizeof(disk)); have_disk = true; return true;
}
bool RECOVERY_PORT_IsBadReset(unsigned reason) { return reason != 1 && reason != 2 && reason != 3; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks) {
    assert(mutex == (void *)1 && !mheld && (ticks == 10 || ticks == portMAX_DELAY));
    mheld = true; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) { assert(mutex == (void *)1 && mheld); mheld = false; return pdTRUE; }
static RecoverySnapshot_t state(void) { RecoverySnapshot_t s; assert(RECOVERY_GetSnapshot(&s)); return s; }
static void healthy(uint32_t base) {
    RecoveryObservation_t observation = {.healthy=true};
    for (uint32_t delta = 0; delta <= 30000; delta += 1000) (void)RECOVERY_Poll(&observation, base + delta);
}

int main(int argc, char **argv) {
    assert(argc == 2); scenario = argv[1];
    assert(crc((const uint8_t *)"123456789", 9) == 0xCBF43926U);
    if (is("history") || is("approved_history") || is("failed_boots") || is("unfinished") ||
            is("prior_healthy") || is("corrupt") || strncmp(scenario,"invalid_",8) == 0 || is("counter_exhausted")) seed();
    const bool invalid = is("corrupt") || strncmp(scenario,"invalid_",8) == 0 || is("counter_exhausted") ||
        is("load_error") || is("no_storage") || is("lock_init_failure") || is("init_write_error");
    unsigned reason = is("fresh") || have_disk ? 1 : is("software") ? 3 : 4;
    if (strncmp(scenario, "reason_", 7) == 0) reason = (unsigned)strtoul(scenario + 7, NULL, 10);
    const bool ok = RECOVERY_Init(!is("no_storage"), reason);
    assert(ok == !invalid);
    assert(SYSTEM_MODE_Init(MODE_INITIAL_BOOT));
    if (!is("undeployed")) assert(SYSTEM_MODE_CompleteDeployment());
    if (invalid) {
        assert(SYSTEM_MODE_Get() == MODE_SAFE && !SYSTEM_MODE_Set(MODE_NOMINAL));
        assert(!SYSTEM_MODE_ReleaseBootInhibit());
        assert(saves == (is("init_write_error") ? 1U : 0U));
        assert(!RECOVERY_RequestRearm(1, RECOVERY_REARM_KEY, 0));
        puts("PASS: recovery invalid storage/init fails closed without overwrite/erase."); return 0;
    }
    RecoverySnapshot_t first = state();
    assert(first.boot_count == (have_disk && loads && disk[11] == 8 ? 8U : 1U));
    assert(saves == 1 && first.storage_ok && !first.ready);
    const bool held = (reason != 1 && reason != 2 && reason != 3) || is("history") || is("approved_history") || is("failed_boots");
    assert(first.held == held && SYSTEM_MODE_IsBootInhibited() == held);
    if (is("failed_boots")) assert(first.failed_boots == 3 && (first.flags & RECOVERY_BOOT_LOOP));
    if (is("unfinished")) assert(first.failed_boots == 1);
    if (held) assert(!SYSTEM_MODE_Set(MODE_INITIAL_BOOT) && !SYSTEM_MODE_Set(MODE_NOMINAL) && !SYSTEM_MODE_Set(MODE_DEGRADED));
    assert(!RECOVERY_RequestRearm(first.boot_count, RECOVERY_REARM_KEY, 0));
    if (is("repeat_init")) { assert(!RECOVERY_Init(true,1)); assert(!state().storage_ok && SYSTEM_MODE_IsBootInhibited()); }
    else if (is("disable_busy")) { fail_lock = true; RECOVERY_Disable(); assert(!state().storage_ok && SYSTEM_MODE_IsBootInhibited()); }
    else {
        if (is("stable_write_error")) fail_save = true;
        const uint32_t base = is("rollover") ? UINT32_MAX - 15000U : 0;
        healthy(base);
        uint32_t now = base + 30000U;
        if (is("stable_write_error")) assert(!state().storage_ok && SYSTEM_MODE_Get() == MODE_SAFE);
        else {
            assert(state().ready && state().stable && saves == 2);
            if (is("write_bound")) {
                for (unsigned i = 0; i < 1000; ++i) { RecoveryObservation_t o={.healthy=true}; RECOVERY_Poll(&o, now + i); }
                assert(saves == 2);
            } else if (is("health_gap")) {
                RecoveryObservation_t o={.healthy=true}; RECOVERY_Poll(&o, now + 2001); assert(!state().ready);
            } else if (is("history_bound") || is("fault_write_error")) {
                RecoveryObservation_t o={.active_fault=true,.source=3,.code=0xAF02,.timestamp=42};
                if (is("fault_write_error")) fail_save = true;
                RECOVERY_Poll(&o, now + 1000);
                if (is("fault_write_error")) assert(!state().storage_ok && state().held);
                else {
                    for (unsigned i = 0; i < 1000; ++i) RECOVERY_Poll(&o, now + 1001 + i);
                    assert(saves == 3 && state().fault_boots == 1 && state().fault_code == 0xAF02);
                    o.fatal=true; RECOVERY_Poll(&o, now + 3000);
                    o.uncertain=true; RECOVERY_Poll(&o, now + 4000);
                    o.overflow=true; RECOVERY_Poll(&o, now + 5000);
                    assert(saves == 6 && state().fault_boots == 1 && !state().ready);
                }
            } else if (held) {
                if (is("bad_key")) assert(!RECOVERY_RequestRearm(first.boot_count, 0, now));
                else if (is("stale_token")) assert(!RECOVERY_RequestRearm(first.boot_count + 1, RECOVERY_REARM_KEY, now));
                else if (is("stale_health")) assert(!RECOVERY_RequestRearm(first.boot_count, RECOVERY_REARM_KEY, now + 1501));
                else {
                    if (!strncmp(scenario, "command_", 8)) {
                        uint8_t body[7] = {FDIR_CMD_REARM_BOOT, (uint8_t)(first.boot_count >> 24),
                            (uint8_t)(first.boot_count >> 16), (uint8_t)(first.boot_count >> 8),
                            (uint8_t)first.boot_count, RECOVERY_REARM_KEY, 0};
                        if (is("command_bad_key")) body[5] = 0;
                        RECOVERY_ProcessCommand(is("command_null") ? NULL : body,
                            is("command_short") ? 5 : is("command_long") ? 7 : 6, now);
                        assert(RECOVERY_RequestRearm(first.boot_count, RECOVERY_REARM_KEY, now) != is("command_good"));
                    } else assert(RECOVERY_RequestRearm(first.boot_count, RECOVERY_REARM_KEY, now));
                    assert(saves == 2 && state().held); // Request context never writes flash or frees MODE.
                    assert(!RECOVERY_RequestRearm(first.boot_count, RECOVERY_REARM_KEY, now));
                    RecoveryObservation_t o={.healthy=!is("health_loss")};
                    if (is("release_critical")) assert(SYSTEM_MODE_RecordFault(3,0xAF02,false,42));
                    if (is("release_fatal")) assert(SYSTEM_MODE_RecordFault(3,0xAF02,true,42));
                    if (is("rearm_write_error")) fail_save = true;
                    const RecoveryResult_t result = RECOVERY_Poll(&o, now + 1000);
                    if (is("rearm_write_error")) assert(result == RECOVERY_STORAGE_FAILED && state().held);
                    else if (is("health_loss") || is("release_critical") || is("release_fatal") || is("undeployed") || is("release_race"))
                        assert(result == RECOVERY_REARM_REJECTED && state().held);
                    else {
                        assert(result == RECOVERY_REARMED && !state().held && SYSTEM_MODE_Get() == MODE_SAFE);
                        assert(state().flags & ~RECOVERY_OPEN_BOOT); // History survives ground approval.
                        assert(state().approved_boot == first.boot_count && saves == 3);
                        assert(!RECOVERY_RequestRearm(first.boot_count, RECOVERY_REARM_KEY, now + 1001));
                        assert(SYSTEM_MODE_Set(MODE_NOMINAL)); ++releases;
                    }
                }
            }
        }
    }
    assert(!rheld && !mheld);
    printf("PASS: recovery policy case %s; real MODE gate, modeled persisted bytes, no hardware.\n", scenario);
    return 0;
}

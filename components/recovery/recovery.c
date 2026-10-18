#include "recovery.h"
#include "recovery_port.h"
#include "system_mode.h"
#include "commands.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

static RecoverySnapshot_t s_state;
static bool s_attempted, s_initialized, s_window, s_request;
static uint32_t s_healthy_since, s_last_sample;
static unsigned s_samples;
static uint32_t s_this_boot_history;
static atomic_bool s_disabled = ATOMIC_VAR_INIT(false);

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void put32(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)(n >> 24); p[1] = (uint8_t)(n >> 16); p[2] = (uint8_t)(n >> 8); p[3] = (uint8_t)n;
}
static uint32_t crc32(const uint8_t *p, unsigned length) {
    uint32_t crc = UINT32_MAX;
    for (unsigned i = 0; i < length; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return ~crc;
}
static bool decode(const uint8_t *p) {
    if (memcmp(p, "FSR1", 4) || p[4] != 1 || p[5] != RECOVERY_RECORD_SIZE || p[6] || p[7] ||
            crc32(p, 44) != get32(p + 44)) return false;
    s_state.boot_count = get32(p + 8); s_state.failed_boots = get32(p + 12);
    s_state.flags = get32(p + 16); s_state.reset_reason = get32(p + 20);
    s_state.fault_boot = get32(p + 24); s_state.fault_boots = get32(p + 28);
    s_state.fault_timestamp = get32(p + 32); s_state.approved_boot = get32(p + 36);
    s_state.fault_source = p[40]; s_state.fault_code = (uint16_t)((uint16_t)p[41] << 8 | p[42]);
    return p[43] == 0 && s_state.boot_count != 0 && !(s_state.flags & ~127U) &&
        s_state.fault_boot <= s_state.boot_count && s_state.approved_boot <= s_state.boot_count;
}
static bool save(void) {
    uint8_t p[RECOVERY_RECORD_SIZE] = {0};
    memcpy(p, "FSR1", 4); p[4] = 1; p[5] = RECOVERY_RECORD_SIZE;
    put32(p + 8, s_state.boot_count); put32(p + 12, s_state.failed_boots);
    put32(p + 16, s_state.flags); put32(p + 20, s_state.reset_reason);
    put32(p + 24, s_state.fault_boot); put32(p + 28, s_state.fault_boots);
    put32(p + 32, s_state.fault_timestamp); put32(p + 36, s_state.approved_boot);
    p[40] = s_state.fault_source; p[41] = (uint8_t)(s_state.fault_code >> 8); p[42] = (uint8_t)s_state.fault_code;
    put32(p + 44, crc32(p, 44));
    if (RECOVERY_PORT_Save(p)) return true;
    s_state.storage_ok = s_state.ready = false;
    s_state.held = true; s_request = false;
    SYSTEM_MODE_LatchRecoveryFailure();
    return false;
}

bool RECOVERY_Init(bool storage_available, unsigned reset_reason) {
    if (s_attempted) { RECOVERY_Disable(); return false; }
    s_attempted = true;
    if (!RECOVERY_PORT_InitLock()) { SYSTEM_MODE_LatchRecoveryFailure(); return false; }
    // Sole startup caller; consumers do not yet exist.
    uint8_t p[RECOVERY_RECORD_SIZE];
    const RecoveryLoad_t loaded = storage_available ? RECOVERY_PORT_Load(p) : RECOVERY_LOAD_ERROR;
    if (loaded == RECOVERY_LOAD_ERROR || (loaded == RECOVERY_LOAD_OK && !decode(p)) ||
            s_state.boot_count == UINT32_MAX) {
        s_state.held = true; SYSTEM_MODE_LatchRecoveryFailure();
        s_initialized = true;
        return false; // Preserve invalid/unreadable bytes. No automatic overwrite.
    }
    const bool bad_reset = RECOVERY_PORT_IsBadReset(reset_reason);
    if ((s_state.flags & RECOVERY_OPEN_BOOT) || bad_reset) {
        if (s_state.failed_boots < UINT32_MAX) ++s_state.failed_boots;
    }
    if (bad_reset) s_state.flags |= RECOVERY_BAD_RESET;
    if (s_state.failed_boots >= RECOVERY_FAILED_BOOT_LIMIT) s_state.flags |= RECOVERY_BOOT_LOOP;
    ++s_state.boot_count;
    s_state.reset_reason = reset_reason;
    // Sticky history: after a serious event, approval is PER BOOT. Ground rearm
    // never deletes history or permanently disables the next-boot safety gate.
    s_state.held = (s_state.flags & ~RECOVERY_OPEN_BOOT) != 0 ||
                   s_state.failed_boots >= RECOVERY_FAILED_BOOT_LIMIT;
    if (s_state.held) SYSTEM_MODE_InhibitBoot();
    s_state.flags |= RECOVERY_OPEN_BOOT;
    s_state.storage_ok = true; s_initialized = true;
    return save(); // One durable unfinished-boot marker before core init.
}

void RECOVERY_Disable(void) {
    // Atomic operating gate is set even if this service's lock is unavailable.
    SYSTEM_MODE_LatchRecoveryFailure();
    atomic_store(&s_disabled, true);
    if (!s_initialized || !RECOVERY_PORT_Lock()) return;
    s_state.storage_ok = s_state.ready = false; s_state.held = true;
    s_request = false;
    RECOVERY_PORT_Unlock();
}
bool RECOVERY_GetSnapshot(RecoverySnapshot_t *out) {
    if (!out || !s_initialized || !RECOVERY_PORT_Lock()) return false;
    *out = s_state;
    if (atomic_load(&s_disabled)) { out->held = true; out->ready = out->storage_ok = false; }
    RECOVERY_PORT_Unlock();
    return true;
}
bool RECOVERY_RequestRearm(uint32_t boot, uint8_t key, uint32_t now) {
    if (atomic_load(&s_disabled) || !s_initialized || !RECOVERY_PORT_Lock()) return false;
    const bool accepted = key == RECOVERY_REARM_KEY && boot != 0 && boot == s_state.boot_count &&
        s_state.storage_ok && s_state.held && s_state.ready && !s_request &&
        (uint32_t)(now - s_last_sample) <= RECOVERY_READY_MAX_AGE_MS;
    if (accepted) s_request = true;
    RECOVERY_PORT_Unlock();
    return accepted;
}

RecoveryResult_t RECOVERY_Poll(const RecoveryObservation_t *observation, uint32_t now) {
    if (!observation || atomic_load(&s_disabled) || !s_initialized || !RECOVERY_PORT_Lock()) return RECOVERY_NO_RESULT;
    RecoveryResult_t result = RECOVERY_NO_RESULT;
    if (!s_state.storage_ok) { RECOVERY_PORT_Unlock(); return RECOVERY_NO_RESULT; }
    const bool blocking = observation->active_fault || observation->fatal || observation->uncertain || observation->overflow;
    const uint32_t observed = (blocking ? RECOVERY_FAULT : 0U) |
        (observation->fatal ? RECOVERY_FATAL : 0U) | (observation->uncertain ? RECOVERY_UNCERTAIN : 0U) |
        (observation->overflow ? RECOVERY_OVERFLOW : 0U);
    if (observed & ~s_this_boot_history) {
        if (!s_this_boot_history && s_state.fault_boots < UINT32_MAX) ++s_state.fault_boots;
        s_this_boot_history |= observed;
        s_state.flags |= observed;
        s_state.fault_boot = s_state.boot_count;
        s_state.fault_source = observation->source;
        s_state.fault_code = observation->code;
        s_state.fault_timestamp = observation->timestamp;
        if (!save()) { result = RECOVERY_STORAGE_FAILED; goto done; }
    }
    const bool healthy = observation->healthy && !blocking;
    const bool continuous = s_window && (uint32_t)(now - s_last_sample) <= RECOVERY_SAMPLE_MAX_GAP_MS;
    s_state.ready = false;
    if (!healthy) { s_window = false; s_samples = 0; }
    else {
        if (!continuous) { s_healthy_since = now; s_samples = 0; }
        if (!s_window || (uint32_t)(now - s_last_sample) >= 500U) {
            if (s_samples < 3U) ++s_samples;
        }
        s_window = true;
        s_state.ready = s_samples >= 3U && (uint32_t)(now - s_healthy_since) >= RECOVERY_HEALTH_MS;
    }
    s_last_sample = now;
    if (s_state.ready && !s_state.stable) {
        s_state.flags &= ~RECOVERY_OPEN_BOOT;
        s_state.failed_boots = 0;
        if (!save()) { result = RECOVERY_STORAGE_FAILED; goto done; }
        s_state.stable = true;
    }
    if (s_request) {
        s_request = false;
        if (!s_state.ready || !s_state.held) { result = RECOVERY_REARM_REJECTED; goto done; }
        // Store approval BEFORE removing the RAM gate. The sticky history is
        // never cleared: interrupted/failed commit or next boot still inhibits.
        if (s_state.approved_boot != s_state.boot_count) {
            s_state.approved_boot = s_state.boot_count;
            if (!save()) { result = RECOVERY_STORAGE_FAILED; goto done; }
        }
        if (!SYSTEM_MODE_ReleaseBootInhibit()) { result = RECOVERY_REARM_REJECTED; goto done; }
        s_state.held = false;
        result = RECOVERY_REARMED; // Release leaves global SAFE, not NOMINAL.
    }
done:
    RECOVERY_PORT_Unlock();
    return result;
}

void RECOVERY_ProcessCommand(const uint8_t *payload, uint16_t length, uint32_t now) {
    const bool accepted = payload && length == 6 && payload[0] == FDIR_CMD_REARM_BOOT &&
        RECOVERY_RequestRearm(get32(payload + 1), payload[5], now);
    printf("RECOVERY: Ground rearm request %s; pending is not completion.\n", accepted ? "queued" : "rejected");
}

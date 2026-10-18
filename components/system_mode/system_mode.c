#include "system_mode.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

static SemaphoreHandle_t s_mode_mutex = NULL;
static SystemMode_t s_current_mode = MODE_SAFE;
static bool s_deployment_complete = false;
static SystemFaultRecord_t s_faults[SYSTEM_FAULT_CAPACITY];
static uint16_t s_fault_count;
static uint32_t s_fault_revision;
static bool s_fault_overflow, s_fatal_overflow;
// Independent fallback when the ledger mutex cannot be acquired. Never cleared
// by an ACK, resolve or repeated init; reset is the only recovery for uncertainty.
#define UNCERTAIN_CRITICAL 1U
#define UNCERTAIN_FATAL 2U
static atomic_uint s_fault_uncertainty = ATOMIC_VAR_INIT(0);
static atomic_bool s_boot_inhibit = ATOMIC_VAR_INIT(false);
static atomic_bool s_recovery_failed = ATOMIC_VAR_INIT(false);
static bool system_mode_lock_faults(void);

static bool system_mode_has_fatal_locked(void) {
    if (s_fatal_overflow || (atomic_load(&s_fault_uncertainty) & UNCERTAIN_FATAL)) return true;
    for (uint16_t i = 0; i < s_fault_count; ++i) if (s_faults[i].fatal) return true;
    return false;
}

static bool system_mode_blocked_locked(void) {
    return s_fault_count != 0 || s_fault_overflow || atomic_load(&s_fault_uncertainty) != 0 ||
        atomic_load(&s_boot_inhibit) || atomic_load(&s_recovery_failed);
}

void SYSTEM_MODE_InhibitBoot(void) { atomic_store(&s_boot_inhibit, true); }
void SYSTEM_MODE_LatchRecoveryFailure(void) { atomic_store(&s_recovery_failed, true); }
bool SYSTEM_MODE_IsBootInhibited(void) { return atomic_load(&s_boot_inhibit) || atomic_load(&s_recovery_failed); }
bool SYSTEM_MODE_ReleaseBootInhibit(void) {
    if (!system_mode_lock_faults()) return false;
    const bool permitted = atomic_load(&s_boot_inhibit) && !atomic_load(&s_recovery_failed) && s_deployment_complete &&
        s_fault_count == 0 && !s_fault_overflow && atomic_load(&s_fault_uncertainty) == 0 &&
        s_current_mode != MODE_EMERGENCY && s_current_mode != MODE_CRITICAL;
    if (permitted) { s_current_mode = MODE_SAFE; atomic_store(&s_boot_inhibit, false); }
    xSemaphoreGive(s_mode_mutex);
    return permitted && !atomic_load(&s_recovery_failed);
}

static bool system_mode_can_select_locked(SystemMode_t mode) {
    if (system_mode_has_fatal_locked()) return mode == MODE_EMERGENCY;
    if (system_mode_blocked_locked()) {
        return mode == MODE_SAFE || mode == MODE_CRITICAL || mode == MODE_EMERGENCY;
    }
    return true;
}

static SystemMode_t system_mode_effective_locked(void) {
    if (system_mode_has_fatal_locked()) return MODE_EMERGENCY;
    if (system_mode_blocked_locked() &&
        (s_current_mode == MODE_INITIAL_BOOT || s_current_mode == MODE_NOMINAL ||
         s_current_mode == MODE_DEGRADED)) return MODE_SAFE;
    return s_current_mode;
}

static bool system_mode_lock_faults(void) {
    return s_mode_mutex != NULL && xSemaphoreTake(s_mode_mutex, pdMS_TO_TICKS(10)) == pdTRUE;
}

static bool system_mode_is_valid(SystemMode_t mode) {
    return (unsigned int)mode <= (unsigned int)MODE_CRITICAL;
}

static bool system_mode_requires_deployment(SystemMode_t mode) {
    return mode == MODE_NOMINAL || mode == MODE_DEGRADED;
}

const char *SYSTEM_MODE_ToString(SystemMode_t mode) {
    switch (mode) {
        case MODE_INITIAL_BOOT:
            return "INITIAL_BOOT";
        case MODE_SAFE:
            return "SAFE";
        case MODE_NOMINAL:
            return "NOMINAL";
        case MODE_DEGRADED:
            return "DEGRADED";
        case MODE_EMERGENCY:
            return "EMERGENCY";
        case MODE_CRITICAL:
            return "CRITICAL";
        default:
            return "UNKNOWN";
    }
}

bool SYSTEM_MODE_Init(SystemMode_t initial_mode) {
    if (!system_mode_is_valid(initial_mode) || system_mode_requires_deployment(initial_mode) ||
        s_mode_mutex != NULL) {
        return false;
    }

    s_mode_mutex = xSemaphoreCreateMutex();
    if (s_mode_mutex == NULL) {
        return false;
    }

    s_current_mode = initial_mode;
    s_deployment_complete = false;
    // A fault reported before init leaves its conservative per-boot inhibit set.
    const SystemMode_t initialized_mode = system_mode_effective_locked();
    s_current_mode = initialized_mode;
    printf("SYSTEM MODE: Initialized in %s.\n",
           SYSTEM_MODE_ToString(initialized_mode));
    return true;
}

SystemMode_t SYSTEM_MODE_Get(void) {
    if (s_mode_mutex == NULL) {
        return (atomic_load(&s_fault_uncertainty) & UNCERTAIN_FATAL) ? MODE_EMERGENCY : MODE_SAFE;
    }

    SystemMode_t mode = MODE_SAFE;
    if (xSemaphoreTake(s_mode_mutex, portMAX_DELAY) == pdTRUE) {
        mode = system_mode_effective_locked();
        xSemaphoreGive(s_mode_mutex);
    }

    return mode;
}

bool SYSTEM_MODE_Set(SystemMode_t new_mode) {
    if (!system_mode_is_valid(new_mode) || s_mode_mutex == NULL) {
        return false;
    }

    if (xSemaphoreTake(s_mode_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }

    if (!system_mode_can_select_locked(new_mode) ||
        (system_mode_requires_deployment(new_mode) && !s_deployment_complete)) {
        xSemaphoreGive(s_mode_mutex);
        return false;
    }

    const SystemMode_t previous_mode = system_mode_effective_locked();
    s_current_mode = new_mode;
    xSemaphoreGive(s_mode_mutex);

    if (previous_mode != new_mode) {
        printf("SYSTEM MODE: %s -> %s.\n",
               SYSTEM_MODE_ToString(previous_mode),
               SYSTEM_MODE_ToString(new_mode));
    }

    return true;
}

bool SYSTEM_MODE_TransitionQuiet(SystemMode_t expected_mode, SystemMode_t new_mode) {
    if (!system_mode_is_valid(expected_mode) ||
        !system_mode_is_valid(new_mode) ||
        s_mode_mutex == NULL) {
        return false;
    }

    if (xSemaphoreTake(s_mode_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }

    if (system_mode_effective_locked() != expected_mode || !system_mode_can_select_locked(new_mode) ||
        (system_mode_requires_deployment(new_mode) && !s_deployment_complete)) {
        xSemaphoreGive(s_mode_mutex);
        return false;
    }

    s_current_mode = new_mode;
    xSemaphoreGive(s_mode_mutex);

    return true;
}

bool SYSTEM_MODE_Transition(SystemMode_t expected_mode, SystemMode_t new_mode) {
    if (!SYSTEM_MODE_TransitionQuiet(expected_mode, new_mode)) {
        return false;
    }

    if (expected_mode != new_mode) {
        printf("SYSTEM MODE: %s -> %s.\n",
               SYSTEM_MODE_ToString(expected_mode),
               SYSTEM_MODE_ToString(new_mode));
    }

    return true;
}

bool SYSTEM_MODE_CompleteDeployment(void) {
    if (s_mode_mutex == NULL || xSemaphoreTake(s_mode_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }

    const bool first_completion = !s_deployment_complete;
    const SystemMode_t previous_mode = system_mode_effective_locked();
    s_current_mode = previous_mode;
    s_deployment_complete = true;
    if (first_completion && previous_mode == MODE_INITIAL_BOOT && system_mode_can_select_locked(MODE_NOMINAL)) {
        s_current_mode = MODE_NOMINAL;
    }
    const SystemMode_t completed_mode = s_current_mode;
    xSemaphoreGive(s_mode_mutex);

    // These messages describe this transaction, not a permanent current mode.
    if (first_completion) {
        printf("SYSTEM MODE: Deployment simulation completion recorded.\n");
    }
    if (previous_mode != completed_mode) {
        printf("SYSTEM MODE: %s -> %s.\n",
               SYSTEM_MODE_ToString(previous_mode), SYSTEM_MODE_ToString(completed_mode));
    }
    return true;
}

bool SYSTEM_MODE_IsDeploymentComplete(void) {
    if (s_mode_mutex == NULL || xSemaphoreTake(s_mode_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }
    const bool complete = s_deployment_complete;
    xSemaphoreGive(s_mode_mutex);
    return complete;
}

bool SYSTEM_MODE_WithCurrent(SystemModeAction_t action, void *context) {
    if (action == NULL || s_mode_mutex == NULL) {
        return false;
    }

    if (xSemaphoreTake(s_mode_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }

    action(system_mode_effective_locked(), context);
    xSemaphoreGive(s_mode_mutex);
    return true;
}

bool SYSTEM_MODE_RecordFault(uint8_t source, uint16_t code, bool fatal, uint32_t timestamp) {
    if (!system_mode_lock_faults()) {
        atomic_fetch_or(&s_fault_uncertainty, fatal ? UNCERTAIN_FATAL : UNCERTAIN_CRITICAL);
        return false;
    }
    uint16_t index = 0;
    while (index < s_fault_count && !(s_faults[index].source == source && s_faults[index].code == code)) ++index;
    bool recorded = index < SYSTEM_FAULT_CAPACITY && s_fault_revision != UINT32_MAX;
    if (recorded) {
        if (index == s_fault_count) {
            s_faults[index] = (SystemFaultRecord_t){.source = source, .code = code, .first_timestamp = timestamp};
            ++s_fault_count;
        }
        SystemFaultRecord_t *entry = &s_faults[index];
        entry->fatal = entry->fatal || fatal;
        entry->last_timestamp = timestamp;
        if (entry->reports < UINT32_MAX) ++entry->reports;
        entry->revision = ++s_fault_revision;
    } else {
        s_fault_overflow = true;
        s_fatal_overflow = s_fatal_overflow || fatal;
    }
    // The ledger and safety decision commit together, without a logging gap.
    s_current_mode = system_mode_effective_locked();
    xSemaphoreGive(s_mode_mutex);
    return recorded;
}

bool SYSTEM_MODE_GetFaults(SystemFaultSnapshot_t *out) {
    if (out == NULL || !system_mode_lock_faults()) return false;
    *out = (SystemFaultSnapshot_t){.count = s_fault_count, .overflow = s_fault_overflow,
        .recording_uncertain = atomic_load(&s_fault_uncertainty) != 0};
    memcpy(out->records, s_faults, s_fault_count * sizeof(s_faults[0]));
    xSemaphoreGive(s_mode_mutex);
    return true;
}

static bool system_mode_get_fault_revision(uint8_t source, uint16_t code, uint32_t *revision, TickType_t timeout) {
    if (revision == NULL || s_mode_mutex == NULL || xSemaphoreTake(s_mode_mutex, timeout) != pdTRUE) return false;
    *revision = 0;
    for (uint16_t i = 0; i < s_fault_count; ++i) {
        if (s_faults[i].source == source && s_faults[i].code == code) { *revision = s_faults[i].revision; break; }
    }
    xSemaphoreGive(s_mode_mutex);
    return true;
}

bool SYSTEM_MODE_GetFaultRevision(uint8_t source, uint16_t code, uint32_t *revision) {
    return system_mode_get_fault_revision(source, code, revision, pdMS_TO_TICKS(10));
}

bool SYSTEM_MODE_TryGetFaultRevision(uint8_t source, uint16_t code, uint32_t *revision) {
    return system_mode_get_fault_revision(source, code, revision, 0);
}

bool SYSTEM_MODE_HasBlockingFaults(bool *blocked) {
    if (blocked == NULL || !system_mode_lock_faults()) return false;
    *blocked = system_mode_blocked_locked();
    xSemaphoreGive(s_mode_mutex);
    return true;
}

static bool system_mode_resolve_fault(uint8_t source, uint16_t code, uint32_t revision, TickType_t timeout) {
    if (revision == 0 || s_mode_mutex == NULL || xSemaphoreTake(s_mode_mutex, timeout) != pdTRUE) return false;
    bool resolved = false;
    for (uint16_t i = 0; i < s_fault_count; ++i) {
        if (s_faults[i].source == source && s_faults[i].code == code &&
            !s_faults[i].fatal && s_faults[i].revision == revision) {
            --s_fault_count;
            memmove(&s_faults[i], &s_faults[i + 1], (s_fault_count - i) * sizeof(s_faults[0]));
            s_faults[s_fault_count] = (SystemFaultRecord_t){0};
            resolved = true;
            break;
        }
    }
    xSemaphoreGive(s_mode_mutex);
    return resolved;
}

bool SYSTEM_MODE_ResolveFault(uint8_t source, uint16_t code, uint32_t revision) {
    return system_mode_resolve_fault(source, code, revision, pdMS_TO_TICKS(10));
}

bool SYSTEM_MODE_TryResolveFault(uint8_t source, uint16_t code, uint32_t revision) {
    return system_mode_resolve_fault(source, code, revision, 0);
}

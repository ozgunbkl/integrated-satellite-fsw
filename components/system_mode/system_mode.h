#ifndef SYSTEM_MODE_H
#define SYSTEM_MODE_H

#include <stdbool.h>
#include "system_fault.h"

typedef enum {
    MODE_INITIAL_BOOT,
    MODE_SAFE,
    MODE_NOMINAL,
    MODE_DEGRADED,
    MODE_EMERGENCY,
    MODE_CRITICAL
} SystemMode_t;

// Startup-only. Operating initial modes (NOMINAL/DEGRADED) are rejected so init
// cannot bypass the per-boot deployment gate; production starts in INITIAL_BOOT.
// The initialization message reports the effective mode, including any pre-init fault floor.
bool SYSTEM_MODE_Init(SystemMode_t initial_mode);
SystemMode_t SYSTEM_MODE_Get(void);
// Active critical causes forbid BOOT/NOMINAL/DEGRADED; fatal causes permit only
// EMERGENCY. Get/WithCurrent expose a conservative mode even if recording failed.
bool SYSTEM_MODE_Set(SystemMode_t new_mode);
bool SYSTEM_MODE_Transition(SystemMode_t expected_mode, SystemMode_t new_mode);
// Same guarded transition and deployment checks, without logging. A caller may
// hold its own state mutex BEFORE the mode mutex to commit related bookkeeping;
// it must release its mutex before reporting the result. No callbacks or I/O.
bool SYSTEM_MODE_TransitionQuiet(SystemMode_t expected_mode, SystemMode_t new_mode);
const char *SYSTEM_MODE_ToString(SystemMode_t mode);

// Deployment task only, after its simulation wait has finished. Atomically
// records completion and changes INITIAL_BOOT -> NOMINAL only on first completion.
// Other modes are preserved. Idempotent; false means no completion was recorded
// by this call (uninitialized/lock timeout), so the task must retry.
bool SYSTEM_MODE_CompleteDeployment(void);
// Conservative snapshot: false also means uninitialized/lock unavailable.
bool SYSTEM_MODE_IsDeploymentComplete(void);

// Recovery owner only. Inhibit is usable before init and takes no locks/I/O.
// Ordinary mode commands, init and fault resolution cannot release this gate.
void SYSTEM_MODE_InhibitBoot(void);
// Storage/monitor failure: irreversible for this boot, including concurrent
// approval. This independent latch cannot be removed by ReleaseBootInhibit.
void SYSTEM_MODE_LatchRecoveryFailure(void);
bool SYSTEM_MODE_IsBootInhibited(void);
// Atomically rechecks deployment/empty ledger/uncertainty under MODE. No flash,
// fault clear or automatic NOMINAL transition. Successful release leaves SAFE.
bool SYSTEM_MODE_ReleaseBootInhibit(void);

// Apply a short caller-owned state update while the current mode cannot change.
// Task context only. The callback must not block, log, call any SYSTEM_MODE/FDIR
// API, or acquire other locks. Callers acquire their own state lock BEFORE this
// call (EPS data -> system mode); there must be no reverse lock order.
// False means the callback was not called (NULL, uninitialized, or lock timeout).
typedef void (*SystemModeAction_t)(SystemMode_t mode, void *context);
bool SYSTEM_MODE_WithCurrent(SystemModeAction_t action, void *context);

// FDIR-owned safety ledger, synchronized by the MODE mutex to make record and
// operating-mode gate one transaction. Quiet task-context APIs; no I/O/FDIR lock.
// Record failure conservatively inhibits operations for the rest of this boot.
bool SYSTEM_MODE_RecordFault(uint8_t source, uint16_t code, bool fatal, uint32_t timestamp);
bool SYSTEM_MODE_GetFaults(SystemFaultSnapshot_t *out);
bool SYSTEM_MODE_GetFaultRevision(uint8_t source, uint16_t code, uint32_t *revision);
bool SYSTEM_MODE_HasBlockingFaults(bool *blocked);
// Trusted owner only, after independent health validation. Exact revision only;
// fatal records are never resolved here. No automatic mode change or overflow clear.
bool SYSTEM_MODE_ResolveFault(uint8_t source, uint16_t code, uint32_t revision);
// Same revision/resolve semantics, but a single zero-wait mutex attempt. Used
// by a caller holding its own service lock; no I/O, retry or uncertainty latch.
bool SYSTEM_MODE_TryGetFaultRevision(uint8_t source, uint16_t code, uint32_t *revision);
bool SYSTEM_MODE_TryResolveFault(uint8_t source, uint16_t code, uint32_t revision);

#endif

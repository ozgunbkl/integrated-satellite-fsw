#ifndef RECOVERY_H
#define RECOVERY_H
#include <stdbool.h>
#include <stdint.h>

#define RECOVERY_HEALTH_MS 30000U
#define RECOVERY_SAMPLE_MAX_GAP_MS 2000U
#define RECOVERY_READY_MAX_AGE_MS 1500U
#define RECOVERY_FAILED_BOOT_LIMIT 3U
#define RECOVERY_REARM_KEY 0xA5U
#define RECOVERY_RECORD_SIZE 48U

// Deliberately bounded summary, not a persistent telemetry/event archive.
enum {
    RECOVERY_OPEN_BOOT = 1U, RECOVERY_FAULT = 2U, RECOVERY_FATAL = 4U,
    RECOVERY_UNCERTAIN = 8U, RECOVERY_OVERFLOW = 16U, RECOVERY_BAD_RESET = 32U,
    RECOVERY_BOOT_LOOP = 64U
};
typedef struct {
    bool healthy, active_fault, fatal, uncertain, overflow;
    uint8_t source;
    uint16_t code;
    uint32_t timestamp;
} RecoveryObservation_t;
typedef struct {
    uint32_t boot_count, failed_boots, flags, reset_reason, fault_boot, fault_boots;
    uint32_t fault_timestamp, approved_boot;
    uint16_t fault_code;
    uint8_t fault_source;
    bool held, ready, storage_ok, stable;
} RecoverySnapshot_t;
typedef enum {
    RECOVERY_NO_RESULT, RECOVERY_REARMED, RECOVERY_REARM_REJECTED, RECOVERY_STORAGE_FAILED
} RecoveryResult_t;

// Before MODE/core/tasks. Port must receive the result of shared NVS startup.
// Any missing/corrupt/unwritable state inhibits operation, never erases storage.
bool RECOVERY_Init(bool storage_available, unsigned reset_reason);
void RECOVERY_Disable(void);
bool RECOVERY_GetSnapshot(RecoverySnapshot_t *out);
// Single monitor caller. No application service locks held on entry. Flash I/O
// is confined to startup and this low-priority caller, never a fault reporter/ISR.
RecoveryResult_t RECOVERY_Poll(const RecoveryObservation_t *observation, uint32_t now_ms);
// Queue only; never flash or mode mutation in command context. A final success
// comes from Poll. Token is per-boot, not cryptographic authentication.
bool RECOVERY_RequestRearm(uint32_t boot_count, uint8_t key, uint32_t now_ms);
void RECOVERY_ProcessCommand(const uint8_t *payload, uint16_t length, uint32_t now_ms);
#endif

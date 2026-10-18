// include/watchdog.h

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include "satellite_types.h" // Needed for WatchdogTaskID_t
#include <stdbool.h>
#include <stdint.h>

#define WATCHDOG_TIMEOUT_MS 15000U
#define WATCHDOG_CHECK_MS 5000U
#define WATCHDOG_RECOVERY_WINDOW_MS 30000U
#define WATCHDOG_RECOVERY_MIN_PETS 3U
#define WATCHDOG_RECOVERY_FRESH_MS WATCHDOG_CHECK_MS
#define FAULT_WDT_TIMEOUT_BASE 0xC200U // Base + task ID; old IDs 0..4 preserved.
#define FAULT_WDT_STATE_UNAVAILABLE 0xC20FU

// Startup-only, before tasks. Init allocates a required mutex and configures
// platform recovery (SDK TWDT 15 s, idle cores + panic/reset on ESP32). Arm AFTER
// optional network startup, immediately before task creation. No epoch reset.
bool WATCHDOG_Init(void);
bool WATCHDOG_Arm(void);

// Task context, outside other service locks. Unarmed/invalid IDs are ignored.
// A pet is liveness only; it never clears a timeout/FDIR record by itself.
// A stable post-fault window can qualify owner recovery in WATCHDOG_Check().
void watchdog_pet(WatchdogTaskID_t task_id);

typedef struct {
    uint32_t pet_count;
    uint32_t min_interval_ms;
    uint32_t max_interval_ms;
    uint32_t age_ms;
} WATCHDOG_TaskTiming_t;

// Read-only cumulative heartbeat intervals, NOT CPU time/WCET/deadline proof.
// First pet only establishes the baseline. Zero intervals are valid at one tick.
// Task context; bounded existing mutex. False leaves output unchanged, reports
// no fault, feeds no watchdog, and never clears latches or heartbeat state.
bool WATCHDOG_GetTimingSnapshot(WATCHDOG_TaskTiming_t out[WDT_TASK_COUNT]);

// Supervisor task only. No FDIR/log/I/O under the heartbeat mutex. False means
// unarmed/unavailable; runtime state failure is reported separately, not as a
// fabricated task timeout. Timeout is strictly >15 s, evaluated every 5 s.
// The supervisor feeds SDK recovery only after a completed successful check;
// individual task faults do not request reset. Persistent check failure/stall
// can reset the device and lose ALL per-boot RAM archive/fault/deployment state.
// Owner recovery resolves only a matching critical task-timeout revision after
// >=30 s, >=3 distinct-tick pets, no >15 s gap and a <=5 s-old latest pet.
// It rearms that task only after resolve succeeds; never changes system mode,
// clears other faults/fatal/uncertainty or resets cumulative timing/SDK state.
bool WATCHDOG_Check(void);

// Public API for FSW Initialization (called by main.c to launch the task)
void vSoftwareWatchdogTask(void *pvParameters);

#endif // WATCHDOG_H

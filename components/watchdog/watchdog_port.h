#ifndef WATCHDOG_PORT_H
#define WATCHDOG_PORT_H

#include <stdbool.h>

#define WATCHDOG_RECOVERY_TIMEOUT_MS 15000U

// Platform boundary, task context only, outside all application service locks.
// Init reconfigures the SDK-owned watchdog; it must already exist. Required
// startup fails if this protection cannot be established (no silent fallback).
bool WATCHDOG_PORT_InitRecovery(void);

// Only the supervisor subscribes/feeds itself. Never feed on its behalf from
// another task/timer/ISR. No unregister, restart, fault-clear or archive writes.
bool WATCHDOG_PORT_SubscribeCurrentTask(void);
bool WATCHDOG_PORT_FeedCurrentTask(void);

#endif

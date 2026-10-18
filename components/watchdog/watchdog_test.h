#ifndef WATCHDOG_TEST_H
#define WATCHDOG_TEST_H
#include "satellite_types.h"
#include <stdbool.h>

#if (defined(FSW_TEST_WDT_TASK_LOSS) + defined(FSW_TEST_WDT_SUPERVISOR_STALL) + \
     defined(FSW_TEST_WDT_IDLE_STARVATION) + defined(FSW_TEST_WDT_INTERRUPT_MASK) + \
     defined(FSW_TEST_WDT_GLOBAL_STARVATION) + defined(FSW_TEST_WDT_TASK_RECOVERY)) > 1
#error "Select one watchdog test profile"
#endif
#if defined(FSW_TEST_WDT_TASK_LOSS) || defined(FSW_TEST_WDT_SUPERVISOR_STALL) || \
    defined(FSW_TEST_WDT_IDLE_STARVATION) || defined(FSW_TEST_WDT_INTERRUPT_MASK) || \
    defined(FSW_TEST_WDT_GLOBAL_STARVATION) || defined(FSW_TEST_WDT_TASK_RECOVERY)
#define FSW_WDT_TEST_ACTIVE 1
#endif
#if defined(FSW_WDT_TEST_ACTIVE) && \
    (defined(FSW_TEST_WIFI_TIMEOUT) || defined(FSW_TEST_WIFI_LIFECYCLE) || \
     defined(FSW_TEST_WIFI_INTERFACE_FAILURE) || defined(FSW_TEST_NVS_INIT_FAILURE) || \
     defined(FSW_TEST_NETIF_INIT_FAILURE) || defined(FSW_TEST_EVENT_LOOP_FAILURE))
#error "Do not combine watchdog and network fault profiles"
#endif

#if defined(FSW_TEST_WDT_CORE) && \
    !defined(FSW_TEST_WDT_IDLE_STARVATION) && !defined(FSW_TEST_WDT_INTERRUPT_MASK)
#error "Core selector requires a starvation test profile"
#endif
#if defined(FSW_TEST_WDT_IDLE_STARVATION) || defined(FSW_TEST_WDT_INTERRUPT_MASK)
#ifndef FSW_TEST_WDT_CORE
#define FSW_TEST_WDT_CORE 0
#endif
#if FSW_TEST_WDT_CORE < 0 || FSW_TEST_WDT_CORE > 1
#error "Watchdog test core must be 0 or 1"
#endif
#endif

#if defined(FSW_WDT_TEST_ACTIVE)
// Explicit test images only; not linked into normal firmware. No NVS writes,
// custom ISR, fault clear or helper feed. Starvation profiles deliberately
// monopolize the selected core in a separate worker; IRQ profile masks that core.
void WATCHDOG_TEST_InitRecovery(unsigned reset_reason);
bool WATCHDOG_TEST_DropPet(WatchdogTaskID_t task);
void WATCHDOG_TEST_AfterCheck(void);
#endif
#endif

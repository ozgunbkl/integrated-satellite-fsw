#ifndef MISSION_LIFECYCLE_TEST_H
#define MISSION_LIFECYCLE_TEST_H

// Isolated, simulated-input target scenario. Never a production recovery API.
#if defined(FSW_TEST_MISSION_LIFECYCLE)
#if FSW_TEST_MISSION_LIFECYCLE != 1
#error "Mission lifecycle selector must be 1"
#endif
#if defined(FSW_TEST_STARTUP_FAILURE) || defined(FSW_TEST_WDT_TASK_LOSS) || \
    defined(FSW_TEST_WDT_SUPERVISOR_STALL) || defined(FSW_TEST_WDT_IDLE_STARVATION) || \
    defined(FSW_TEST_WDT_INTERRUPT_MASK) || defined(FSW_TEST_WDT_GLOBAL_STARVATION) || \
    defined(FSW_TEST_WDT_TASK_RECOVERY) || defined(FSW_TEST_WDT_CORE) || \
    defined(FSW_TEST_WIFI_TIMEOUT) || defined(FSW_TEST_WIFI_LIFECYCLE) || \
    defined(FSW_TEST_WIFI_INTERFACE_FAILURE) || defined(FSW_TEST_NVS_INIT_FAILURE) || \
    defined(FSW_TEST_NETIF_INIT_FAILURE) || defined(FSW_TEST_EVENT_LOOP_FAILURE)
#error "Do not combine mission lifecycle and other fault profiles"
#endif
#endif

#endif

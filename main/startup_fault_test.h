#ifndef STARTUP_FAULT_TEST_H
#define STARTUP_FAULT_TEST_H

// Additional opt-in only: three HK caller failures, then real normal startup.
// The durable recovery counter bounds injection; no RTC/namespace clear.
#if defined(FSW_TEST_RECOVERY_BOOT_LOOP)
#if FSW_TEST_RECOVERY_BOOT_LOOP != 1 || !defined(FSW_TEST_STARTUP_FAILURE) || FSW_TEST_STARTUP_FAILURE != 1
#error "Recovery boot-loop test requires value 1 and HK startup profile 1"
#endif
#endif

// Test images only: 1 = HK init false, 2 = CMD_PROC create error,
// 3 = SOLAR_HARV create error. No production entry point or recovery policy.
#if defined(FSW_TEST_STARTUP_FAILURE)
#if FSW_TEST_STARTUP_FAILURE < 1 || FSW_TEST_STARTUP_FAILURE > 3
#error "Startup failure selector must be 1, 2 or 3"
#endif
#if defined(FSW_TEST_WDT_TASK_LOSS) || defined(FSW_TEST_WDT_SUPERVISOR_STALL) || \
    defined(FSW_TEST_WDT_IDLE_STARVATION) || defined(FSW_TEST_WDT_INTERRUPT_MASK) || \
    defined(FSW_TEST_WDT_GLOBAL_STARVATION) || defined(FSW_TEST_WDT_TASK_RECOVERY) || \
    defined(FSW_TEST_WDT_CORE) || defined(FSW_TEST_WIFI_TIMEOUT) || \
    defined(FSW_TEST_WIFI_LIFECYCLE) || defined(FSW_TEST_WIFI_INTERFACE_FAILURE) || \
    defined(FSW_TEST_NVS_INIT_FAILURE) || defined(FSW_TEST_NETIF_INIT_FAILURE) || \
    defined(FSW_TEST_EVENT_LOOP_FAILURE)
#error "Do not combine startup failure and other fault profiles"
#endif
#endif

#endif

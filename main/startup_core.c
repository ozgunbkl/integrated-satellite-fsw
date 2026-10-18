#include "startup_core.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "time_service.h"
#include "system_mode.h"
#include "fdir_service.h"
#include "yamcs_interface.h"
#include "archive_service.h"
#include "hk_service.h"
#include "payload_service.h"
#include "adcs_service.h"
#include "adcs_sim.h"
#include "watchdog.h"

static bool s_init_attempted = false;

void STARTUP_InitCoreServices(void) {
    // app_main is the sole caller, before tasks: this is not a concurrent API.
    if (s_init_attempted) {
        ESP_LOGE("MAIN", "Core services initialization already attempted; refusing repeated initialization.");
        abort();
    }
    s_init_attempted = true;

    TIME_Init();
    if (!SYSTEM_MODE_Init(MODE_INITIAL_BOOT)) {
        ESP_LOGE("MAIN", "Failed to initialize system mode service");
        abort();
    }
    if (!FDIR_Init()) {
        ESP_LOGE("MAIN", "Failed to initialize FDIR service");
        abort();
    }
    if (!WATCHDOG_Init()) {
        ESP_LOGE("MAIN", "Failed to initialize software watchdog service");
        abort();
    }
    if (!YAMCS_Init()) {
        ESP_LOGE("MAIN", "Failed to initialize Yamcs telemetry sender");
        abort();
    }

    // Preserve all initialization calls, state snapshots and readiness markers.
    if (!Archive_Init()) {
        ESP_LOGE("MAIN", "Failed to initialize RAM archive service");
        abort();
    }
    printf("MAIN: RAM archive initialized before task creation.\n");
    if (!HK_Init()) {
        ESP_LOGE("MAIN", "Failed to initialize HK service");
        abort();
    }
    Payload_Init();
    printf("MAIN: HK and payload initialized before task creation (payload state=%u).\n",
           (unsigned)Payload_GetTelemetry().current_state);
    ADCS_Init();
    SIM_SetTrueAngularVelocity(10.0f, -5.0f, 0.5f);
    printf("MAIN: ADCS initialized before task creation (mode=%u).\n",
           (unsigned)ADCS_GetMode());
}

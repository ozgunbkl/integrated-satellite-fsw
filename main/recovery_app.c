#include "recovery_app.h"
#include "recovery.h"
#include "startup_nvs.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "eps_telemetry.h"
#include "fdir_service.h"
#include "watchdog.h"
#include "archive_service.h"
#include "payload_service.h"
#include "system_mode.h"
#include "time_service.h"
#include <math.h>
#include <stdio.h>

void RECOVERY_APP_Prepare(void) {
    const bool initialized = RECOVERY_Init(STARTUP_InitNvs() == ESP_OK, (unsigned)esp_reset_reason());
    RecoverySnapshot_t state;
    if (initialized && RECOVERY_GetSnapshot(&state))
        printf("RECOVERY: boot=%lu failed_boots=%lu held=%u flags=%lu; journal committed before core init.\n",
               (unsigned long)state.boot_count, (unsigned long)state.failed_boots,
               (unsigned)state.held, (unsigned long)state.flags);
    else printf("RECOVERY: Storage/init unavailable; operating modes inhibited; no automatic erase.\n");
}

static RecoveryObservation_t observe_health(void) {
    RecoveryObservation_t observation = {0};
    bool archive_ok = true;
    for (uint16_t index = 0; index < ARCHIVE_SIZE_BYTES / sizeof(ArchiveRecord_t); ++index) {
        ArchiveRecord_t record;
        const ArchiveStatus_t status = Archive_ReadRecord(index, &record);
        if (status == ARCHIVE_ERR_NOT_FOUND || status == ARCHIVE_EMPTY) break;
        if (status != ARCHIVE_OK) { archive_ok = false; break; }
    }
    FDIR_ActiveFaultSnapshot_t faults;
    // Unavailable query is not an empty ledger or a fabricated uncertainty fault.
    if (!FDIR_GetActiveFaults(&faults)) return observation;
    observation.active_fault = faults.count != 0;
    observation.uncertain = faults.recording_uncertain;
    observation.overflow = faults.overflow;
    for (unsigned i = 0; i < faults.count; ++i) {
        if (i == 0 || faults.records[i].fatal) {
            observation.source = faults.records[i].source;
            observation.code = faults.records[i].code;
            observation.timestamp = faults.records[i].last_timestamp;
        }
        observation.fatal = observation.fatal || faults.records[i].fatal;
    }
    if (observation.active_fault || observation.uncertain || observation.overflow || !archive_ok ||
            !SYSTEM_MODE_IsDeploymentComplete()) return observation;
    const SystemMode_t mode = SYSTEM_MODE_Get();
    if (mode == MODE_EMERGENCY || mode == MODE_CRITICAL || mode == MODE_INITIAL_BOOT) return observation;
    if (!xEPSDataMutex || xSemaphoreTake(xEPSDataMutex, pdMS_TO_TICKS(10)) != pdTRUE) return observation;
    const float voltage = g_eps_telemetry.f_BusVoltage;
    if (xSemaphoreGive(xEPSDataMutex) != pdTRUE) return observation;
    if (!isfinite(voltage) || voltage < EPS_VOLTAGE_CRITICAL_LOW ||
            Payload_GetTelemetry().current_state == PL_STATE_ERROR) return observation;
    WATCHDOG_TaskTiming_t timings[WDT_TASK_COUNT];
    if (!WATCHDOG_GetTimingSnapshot(timings)) return observation;
    for (unsigned i = 0; i < WDT_TASK_COUNT; ++i) {
        if (!timings[i].pet_count || timings[i].age_ms > WATCHDOG_TIMEOUT_MS) return observation;
    }
    observation.healthy = true;
    return observation;
}

static void recovery_task(void *argument) {
    (void)argument;
    unsigned cycles = 0;
    bool previously_ready = false;
    for (;;) {
        const RecoveryObservation_t observation = observe_health();
        const RecoveryResult_t result = RECOVERY_Poll(&observation, TIME_GetMilliseconds());
        if (result == RECOVERY_REARMED)
            printf("RECOVERY: Ground rearm COMPLETE; boot gate released; system remains SAFE; history retained.\n");
        else if (result == RECOVERY_REARM_REJECTED)
            printf("RECOVERY: Ground rearm FAILED; health/mode changed; gate retained.\n");
        else if (result == RECOVERY_STORAGE_FAILED)
            printf("RECOVERY: Journal write/read-back FAILED; operating modes inhibited for this boot.\n");
        RecoverySnapshot_t state;
        if (RECOVERY_GetSnapshot(&state) && (state.ready != previously_ready || ++cycles >= 30U)) {
            previously_ready = state.ready; cycles = 0;
            printf("RECOVERY: boot=%lu held=%u ready=%u storage_ok=%u stable=%u flags=%lu stack_free_bytes=%u.\n",
                   (unsigned long)state.boot_count, (unsigned)state.held, (unsigned)state.ready,
                   (unsigned)state.storage_ok, (unsigned)state.stable, (unsigned long)state.flags,
                   (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
bool RECOVERY_APP_Start(void) {
    static bool attempted;
    if (attempted) { RECOVERY_Disable(); return false; }
    attempted = true;
    if (xTaskCreate(recovery_task, "RECOVERY", 6144, NULL, 1, NULL) == pdPASS) return true;
    RECOVERY_Disable();
    printf("RECOVERY: Monitor creation failed; operating modes inhibited.\n");
    return false;
}

#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "adcs_service.h"
#include "commands.h"
#include "fdir_service.h"
#include "adcs_sim.h"
#include "eps_telemetry.h"
#include "time_service.h"
#include "watchdog.h"
#include "freertos/task.h"
#include "eps_service.h"

static int adcs_mutex_slot;
static bool adcs_locked;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &adcs_mutex_slot; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks) {
    assert(mutex == &adcs_mutex_slot && !adcs_locked);
    assert(ticks == portMAX_DELAY || ticks == pdMS_TO_TICKS(10));
    adcs_locked = true;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
    assert(mutex == &adcs_mutex_slot && adcs_locked);
    adcs_locked = false;
    return pdTRUE;
}
bool EPS_RunIfPermitted(EpsOperation_t operation, SystemModeAction_t action, void *context) {
    assert(adcs_locked && (unsigned int)operation <= (unsigned int)EPS_OP_ORBIT_BOOST);
    action(MODE_NOMINAL, context);
    return true;
}

void vADCS_Manager_Task(void *parameters);
void __real_ADCS_Init(void);
void __real_SIM_SetTrueAngularVelocity(float x, float y, float z);

static jmp_buf task_exit;
static unsigned init_calls, spin_setup_calls, watchdog_calls, delay_calls, fault_reports;

void __wrap_ADCS_Init(void) {
    ++init_calls;
    __real_ADCS_Init();
}

void __wrap_SIM_SetTrueAngularVelocity(float x, float y, float z) {
    ++spin_setup_calls;
    assert(x == 10.0f && y == -5.0f && z == 0.5f);
    __real_SIM_SetTrueAngularVelocity(x, y, z);
}

eps_mode_t EPS_GetMode(void) { return EPS_MODE_NOMINAL; }
uint32_t TIME_GetSeconds(void) { return 0; }
void FDIR_ReportFault(FaultReport_t report) { ++fault_reports; }

void watchdog_pet(WatchdogTaskID_t task_id) {
    assert(task_id == WDT_TASK_ADCS);
    ++watchdog_calls;
}

void vTaskDelay(TickType_t ticks) {
    assert(ticks == pdMS_TO_TICKS(100));
    ++delay_calls;
    // End the real task after one complete iteration without editing its loop.
    longjmp(task_exit, 1);
}

int main(void) {
    srand(1); // Reproducible host-only simulation noise.
    ADCS_Init();
    SIM_SetTrueAngularVelocity(10.0f, -5.0f, 0.5f);
    assert(init_calls == 1 && spin_setup_calls == 1);
    assert(ADCS_GetMode() == ADCS_MODE_DETUMBLE);

    ADCSTelemetry_t telemetry;
    ADCS_GetTelemetry(&telemetry);
    assert(telemetry.mode == ADCS_MODE_DETUMBLE && telemetry.actuators_enabled);
    // SIM_ReadGyroscope adds up to +/-0.01; allow a small float rounding margin.
    assert(fabsf(telemetry.angular_velocity.x - 10.0f) <= 0.011f);
    assert(fabsf(telemetry.angular_velocity.y + 5.0f) <= 0.011f);
    assert(fabsf(telemetry.angular_velocity.z - 0.5f) <= 0.011f);
    assert(telemetry.last_torque.x == 0 && telemetry.last_torque.y == 0 &&
           telemetry.last_torque.z == 0);

    // Simulate a command arriving after preparation but before the task starts.
    // This stays entirely inside the host process; no TC is sent to the ESP32.
    const uint8_t early_command[] = {ADCS_CMD_SET_MODE, ADCS_MODE_NOMINAL};
    ADCS_ProcessCommand(early_command, sizeof(early_command));
    assert(ADCS_GetMode() == ADCS_MODE_NOMINAL);

    if (setjmp(task_exit) == 0) {
        vADCS_Manager_Task(NULL);
        assert(!"ADCS task unexpectedly returned instead of reaching its delay");
    }

    assert(init_calls == 1 && spin_setup_calls == 1);
    assert(ADCS_GetMode() == ADCS_MODE_NOMINAL);
    ADCS_GetTelemetry(&telemetry);
    assert(telemetry.mode == ADCS_MODE_NOMINAL && telemetry.actuators_enabled);
    assert(watchdog_calls == 1 && delay_calls == 1 && fault_reports == 0);
    puts("PASS: ADCS DETUMBLE/actuator defaults and initial simulation spin;\n"
         "      early mode command retained by the real task, no repeated init/spin setup, 100 ms watchdog cycle.");
    return 0;
}

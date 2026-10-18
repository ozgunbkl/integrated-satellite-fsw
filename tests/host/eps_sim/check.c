/* Existing normal environment task + pure energy model, no target or faults. */
#include <assert.h>
#include <float.h>
#include <math.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include "eps_sim_model.h"
#include "eps_telemetry.h"
#include "simulation_tasks.h"
#include "freertos/task.h"

static int mutex;
SemaphoreHandle_t xEPSDataMutex = &mutex;
EPS_Telemetry_t g_eps_telemetry;
static bool held;
static TickType_t tick = UINT32_MAX - 2000U;
static unsigned loops, elapsed_ms;
static jmp_buf stop;

BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t timeout) {
    assert(handle == &mutex && !held && timeout == portMAX_DELAY);
    held = true; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
    assert(handle == &mutex && held); held = false; return pdTRUE;
}
TickType_t xTaskGetTickCount(void) { return tick; }
float ADCS_GetFuelLevel(void) { assert(!held); return 100.0f; }
int __wrap_printf(const char *format, ...) { assert(!held); return 0; }
void vTaskDelay(TickType_t delay) {
    assert(!held && delay == pdMS_TO_TICKS(1000));
    const float solar = (elapsed_ms / 60000U) % 2 ? 0 : EPS_SIM_SUN_POWER_W;
    assert(g_eps_telemetry.f_SolarInputPower == solar);
    assert(g_eps_telemetry.f_BusVoltage >= 8.4f && g_eps_telemetry.f_BusVoltage <= 9.0f);
    assert(fabsf(g_eps_telemetry.f_StateOfCharge - EPS_SIM_StateOfCharge(g_eps_telemetry.f_BusVoltage)) < 0.001f);
    if (++loops == 200) longjmp(stop, 1);
    tick += 1250; elapsed_ms += 1250; /* Measured time and tick wrap, not a fake 1 s step. */
}

int main(void) {
    assert(isnan(EPS_SIM_StepVoltage(NAN, 24, 9, 1)));
    assert(isnan(EPS_SIM_StepVoltage(INFINITY, 24, 9, 1)));
    assert(isnan(EPS_SIM_StepVoltage(8.5f, NAN, 9, 1)));
    assert(isnan(EPS_SIM_StepVoltage(8.5f, 24, -1, 1)));
    assert(isnan(EPS_SIM_StepVoltage(8.5f, 24, 9, -1)));
    assert(isnan(EPS_SIM_StateOfCharge(NAN)));
    assert(fabsf(EPS_SIM_StepVoltage(8, 24, 9, 0) - 8) < 0.00001f);
    const float charged = EPS_SIM_StepVoltage(8, 24, 9, 60);
    assert(fabsf(charged - (8.0f + 15.0f * .9f * 60 / 3600 / 20 * 9)) < 0.00001f);
    const float discharged = EPS_SIM_StepVoltage(8, 0, 9, 60);
    assert(fabsf(discharged - (8.0f - 9.0f / .9f * 60 / 3600 / 20 * 9)) < 0.00001f);
    assert(EPS_SIM_StepVoltage(8, FLT_MAX, 0, FLT_MAX) == 9);
    assert(EPS_SIM_StepVoltage(8, 0, FLT_MAX, FLT_MAX) == 0);
    assert(EPS_SIM_StateOfCharge(0) == 0 && EPS_SIM_StateOfCharge(9) == 100);
    float voltage = EPS_SIM_INITIAL_VOLTAGE;
    for (unsigned second = 0; second < 3600; ++second) {
        const float solar = (second / 60) % 2 ? 0 : EPS_SIM_SUN_POWER_W;
        voltage = EPS_SIM_StepVoltage(voltage, solar, 9, 1);
        assert(isfinite(voltage) && voltage > 8.4f && voltage <= 9);
    }
    /* Protection remains meaningful: no sunlight actually depletes the store. */
    voltage = 8.5f;
    for (unsigned second = 0; second < 3600; ++second)
        voltage = EPS_SIM_StepVoltage(voltage, 0, 9, 1);
    assert(voltage < EPS_VOLTAGE_CRITICAL_LOW);
    voltage = 8.5f;
    for (unsigned second = 0; second < 86400; ++second)
        voltage = EPS_SIM_StepVoltage(voltage, 24, 1, 1);
    assert(voltage == 9); /* No more unbounded SAFE voltage. */
    g_eps_telemetry.f_BusVoltage = 8.5f;
    g_eps_telemetry.ul_PDU_Register = PDU_LOAD_PAYLOAD_BIT | PDU_LOAD_HEATER_BIT | PDU_LOAD_COMMS_BIT;
    if (setjmp(stop) == 0) vSolarHarvest_Task(NULL);
    assert(loops == 200 && !held);
    puts("PASS: bounded Wh/dt/SOC model, max-load sun/eclipse integration, dark depletion, invalid inputs; actual solar task snapshots, lock order, measured time and wrap. No target test.");
    return 0;
}

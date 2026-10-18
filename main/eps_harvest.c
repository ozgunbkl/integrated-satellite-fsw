#include "simulation_tasks.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <math.h>
#include "eps_telemetry.h"
#include "eps_sim_model.h"
#include "adcs_service.h"


void vSolarHarvest_Task(void *pvParameters){
    TickType_t last = xTaskGetTickCount();
    double elapsed_s = 0;
    bool is_in_eclipse = false;
    float internal_temp = 25.0f; // Start at room temp

    while(1){
        const TickType_t now = xTaskGetTickCount();
        const float dt_s = (float)(TickType_t)(now - last) * portTICK_PERIOD_MS / 1000.0f;
        last = now;
        elapsed_s += dt_s;

        // Measured elapsed time, not loop count; 60 s sun + 60 s shadow.
        const bool eclipse = fmod(elapsed_s, 120.0) >= 60.0;
        if(eclipse != is_in_eclipse) {
            is_in_eclipse = eclipse;
            printf("\n--- ENV: %s ---\n", is_in_eclipse ? "ENTERING SHADOW" : "ENTERING SUNLIGHT");
        }

        if(xSemaphoreTake(xEPSDataMutex, portMAX_DELAY) == pdTRUE){

            const float solar = is_in_eclipse ? 0.0f : EPS_SIM_SUN_POWER_W;
            g_eps_telemetry.f_SolarInputPower = solar;

            // 3. LOAD CALCULATION (Your original logic)
            float f_TotalLoad = 1.0f; // Essential Comms

            // If Payload bit is SET, add its load
            if (g_eps_telemetry.ul_PDU_Register & PDU_LOAD_PAYLOAD_BIT) {
                f_TotalLoad += 5.0f;
            }

            const bool heater = (g_eps_telemetry.ul_PDU_Register & PDU_LOAD_HEATER_BIT) != 0;

            // If HEATER bit is SET, add load AND increase temp
            if (g_eps_telemetry.ul_PDU_Register & PDU_LOAD_HEATER_BIT) {
                f_TotalLoad += 3.0f;
            }

            const float voltage = EPS_SIM_StepVoltage(g_eps_telemetry.f_BusVoltage, solar, f_TotalLoad, dt_s);
            const float soc = EPS_SIM_StateOfCharge(voltage);
            g_eps_telemetry.f_BusVoltage = voltage;
            g_eps_telemetry.f_StateOfCharge = soc;

            xSemaphoreGive(xEPSDataMutex);

            // Bounded temperature proxy; snapshots only after unlocking EPS.
            const float target = (is_in_eclipse ? -15.0f : 25.0f) + (heater ? 20.0f : 0.0f);
            internal_temp += (target - internal_temp) * (1.0f - expf(-dt_s / 600.0f));
            printf("HARVEST: Solar: %.1fW | Load: %.1fW | Temp: %.2fC | V: %.2fV | Battery: (%.1f%%)| Fuel: %.1f%%\n",
                   solar, f_TotalLoad, internal_temp, voltage, soc, ADCS_GetFuelLevel());
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

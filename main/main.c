// src/cdh_main.c

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "satellite_types.h"
#include "tc_proc.h"
#include "eps_control.h"
#include "task_defs.h"
#include "watchdog.h"
#include "fdir_service.h"
#include "fdir_task.h"
#include "simulation_tasks.h"
#include "eps_telemetry.h"
#include "eps_sim_model.h"
#include "esp_log.h"
#include "wifi_service.h"
#include "startup_core.h"
#include "startup_resources.h"
#include "resource_monitor.h"
#include "recovery_app.h"
#include <stdlib.h>
#include <time.h>


// --- 1. GLOBAL RESOURCE DEFINITIONS (The Single Source of Truth) ---

static const char *TAG = "MAIN";

// EPS Data
EPS_Telemetry_t g_eps_telemetry = {
    .f_BusVoltage = EPS_SIM_INITIAL_VOLTAGE,
    .f_StateOfCharge = EPS_SIM_INITIAL_VOLTAGE / EPS_SIM_MAX_VOLTAGE * 100.0f,
    .f_SolarInputPower = 0.0f,
    .ul_PDU_Register = (PDU_LOAD_PAYLOAD_BIT | PDU_LOAD_HEATER_BIT | PDU_LOAD_COMMS_BIT),
};

// Handles
SemaphoreHandle_t xEPSDataMutex = NULL;
QueueHandle_t xTelemetryQueue = NULL;
QueueHandle_t xCommandQueue = NULL;

// --- 2. EXTERNAL TASK PROTOTYPES ---
extern void vEPS_Manager_Task(void *pvParameters);
extern void vCommandInjectionTask(void *pvParameters);
extern void vDataLoggerTask(void *pvParameters);
extern void vTelemetryGeneratorTask(void *pvParameters);
extern void vADCS_Manager_Task(void *pvParameters); 
extern void vDeploymentTask(void *pvParameters);
extern void vYamcsTcReceiverTask(void *pvParameters);

typedef struct {
    TaskFunction_t function;
    const char *name;
    configSTACK_DEPTH_TYPE stack_depth;
    UBaseType_t priority;
} RequiredTask_t;

static const RequiredTask_t s_required_tasks[] = {
    // SDK-supervised monitor; 4 KiB covers fault reporting and wrapped printf.
    {vSoftwareWatchdogTask, "WDT_MON", 4096, 6},
    {vDeploymentTask, "DEPLOY", 4096, 5},
    {vEPS_Manager_Task, "EPS_MGMT", 4096, configMAX_PRIORITIES - 1},
    {vCommandProcessorTask, "CMD_PROC", 4096, 5},
    {vYamcsTcReceiverTask, "TC_RX", 4096, 4},
    {vEPSMonitoringTask, "EPS_MON", 2048, 4},
    {vFDIRTask, "FDIR", FDIR_TASK_STACK_BYTES, 4},
    {vDataLoggerTask, "DATA_LOG", 4096, 4},
    {vADCS_Manager_Task, "ADCS_MGMT", 4096, 4},
    {vTelemetryGeneratorTask, "TM_GEN", 4096, 3},
    {vCommandInjectionTask, "CMD_INJECT", 4096, 1},
    {vSolarHarvest_Task, "SOLAR_HARV", 4096, 1},
};

static void create_required_tasks(void) {
    const unsigned count = sizeof(s_required_tasks) / sizeof(s_required_tasks[0]);
    for (unsigned created = 0; created < count; ++created) {
        const RequiredTask_t *task = &s_required_tasks[created];
        const BaseType_t result = xTaskCreate(task->function, task->name,
            task->stack_depth, NULL, task->priority, NULL);
        if (result != pdPASS) {
            // Earlier tasks may already have run. No retry, rollback or cleanup:
            // keep their resources intact until the configured abort/panic reset.
            ESP_LOGE(TAG,
                     "Required task creation failed: %s (stack=%lu, priority=%lu, required_created=%u/%u)",
                     task->name, (unsigned long)task->stack_depth,
                     (unsigned long)task->priority, created, count);
            abort();
        }
    }
}

// --- 3. MASTER INITIALIZATION AND LAUNCH ---

void app_main(void) {
    // Keep terminal focused on mission printf output.
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("MAIN", ESP_LOG_INFO);

    printf("--- Integrated Satellite FSW Initialization Started ---\n");

    RECOVERY_APP_Prepare();
    STARTUP_InitCoreServices();

    // Mandatory resources take priority over optional Wi-Fi/log allocations.
    StartupResources_t resources;
    STARTUP_CreateRequiredResources(&resources);
    xTelemetryQueue = resources.telemetry_queue;
    xCommandQueue = resources.command_queue;
    xEPSDataMutex = resources.eps_data_mutex;
    printf("MAIN: Required queues and EPS mutex ready before Wi-Fi/log initialization.\n");

    const esp_err_t network_result = WIFI_Start();
    const bool log_mirror_started = WIFI_IsLogMirrorStarted();
    if (network_result != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi not ready at startup (%s); core startup continues, log mirror skipped.",
                 esp_err_to_name(network_result));
    } else if (!log_mirror_started) {
        ESP_LOGW(TAG, "Log mirror not started; background retry active, core startup continues.");
    }

    // Initialize Random Seed for Solar Simulation
    srand((unsigned int)time(NULL));

    // --- 4. TASK CREATION (Priority Hierarchy) ---

    // Do not charge optional network startup time against mission heartbeats.
    if (!WATCHDOG_Arm()) {
        ESP_LOGE(TAG, "Failed to arm software watchdog before task creation");
        abort();
    }

    // Start the observer before mission consumers/deployment can select NOMINAL.
    // It cannot declare health until deployment and mission heartbeats progress.
    (void)RECOVERY_APP_Start();
    create_required_tasks();

    if (!RESOURCE_MONITOR_Start(xTelemetryQueue, xCommandQueue)) {
        ESP_LOGW(TAG, "Resource diagnostics unavailable; mission tasks continue.");
    }

    printf("MAIN: Core tasks created. Network at startup: %s; log mirror: %s.\n",
           network_result == ESP_OK ? "connected" : "unavailable",
           log_mirror_started ? "started" : "not started");
}

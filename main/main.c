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
#include "eps_telemetry.h"
#include "state_manager.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "lwip/inet.h"
#include <stdlib.h>
#include <time.h>


// --- 1. GLOBAL RESOURCE DEFINITIONS (The Single Source of Truth) ---

#define WIFI_SSID       "SUPERONLINE_Wi-Fi_7KGF"
#define WIFI_PASSWORD   "dz5xykTNUXyz"
#define WIFI_MAX_RETRY  10

static const char *TAG = "MAIN";
static EventGroupHandle_t s_wifi_event_group;
static int s_wifi_retry_count = 0;

#define WIFI_CONNECTED_BIT BIT0

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_wifi_retry_count++;
            ESP_LOGW(TAG, "Wi-Fi reconnect attempt %d/%d", s_wifi_retry_count, WIFI_MAX_RETRY);
        } else {
            ESP_LOGE(TAG, "Wi-Fi connect failed after %d attempts", WIFI_MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        printf("MAIN: Wi-Fi connected. ESP32 IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
        s_wifi_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init_sta(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi event group");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };

    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(15000));

    if ((bits & WIFI_CONNECTED_BIT) == 0U) {
        ESP_LOGE(TAG, "Wi-Fi did not connect within timeout");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

// EPS Data
EPS_Telemetry_t g_eps_telemetry = {
    .f_BusVoltage = 8.5f, 
    .f_StateOfCharge = 50.0f,
    .f_SolarInputPower = 0.0f,
    .ul_PDU_Register = (PDU_LOAD_PAYLOAD_BIT | PDU_LOAD_HEATER_BIT | PDU_LOAD_COMMS_BIT),
};

// System State
SystemMode_t g_current_mode = MODE_NOMINAL;

// Handles
SemaphoreHandle_t xEPSDataMutex = NULL;
SemaphoreHandle_t xModeMutex = NULL;
QueueHandle_t xTelemetryQueue = NULL;
QueueHandle_t xCommandQueue = NULL;

// --- 2. EXTERNAL TASK PROTOTYPES ---
extern void vSolarHarvest_Task(void *pvParameters);
extern void vEPS_Manager_Task(void *pvParameters);
extern void vCommandInjectionTask(void *pvParameters);
extern void vDataLoggerTask(void *pvParameters);
extern void vTelemetryGeneratorTask(void *pvParameters);
extern void vADCS_Manager_Task(void *pvParameters); 
extern void vDeploymentTask(void *pvParameters);
extern void vYamcsTcReceiverTask(void *pvParameters);

// --- 3. MASTER INITIALIZATION AND LAUNCH ---

void app_main(void) {
    // Keep terminal focused on mission printf output.
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("MAIN", ESP_LOG_INFO);

    printf("--- Integrated Satellite FSW Initialization Started ---\n");

    if (wifi_init_sta() != ESP_OK) {
        ESP_LOGE(TAG, "Network init failed. Yamcs UDP telemetry will not work.");
    }

    // Initialize Random Seed for Solar Simulation
    srand((unsigned int)time(NULL));

    // A. Create Queues
    xTelemetryQueue = xQueueCreate(10, sizeof(HK_Telemetry_t));
    if (xTelemetryQueue == NULL) {
        printf("CRITICAL ERROR: Telemetry Queue Failed!\n");
        return;
    }

    xCommandQueue = xQueueCreate(5, sizeof(TelecommandPacket_t));
    if (xCommandQueue == NULL) {
        printf("CRITICAL ERROR: Command Queue Failed!\n");
        return;
    }

    // B. Create Mutexes
    xEPSDataMutex = xSemaphoreCreateMutex();
    if (xEPSDataMutex == NULL) {
        ESP_LOGE("MAIN", "Failed to create EPS Mutex!");
        return;
    }

    xModeMutex = xSemaphoreCreateMutex();
    if (xModeMutex == NULL) {
        printf("CRITICAL ERROR: Mode Mutex Failed!\n");
        return;
    }

    // --- 4. TASK CREATION (Priority Hierarchy) ---

    // [PRIO 6] - System Safety: Watchdog must be highest
    xTaskCreate(vSoftwareWatchdogTask, "WDT_MON", 2048, NULL, 6, NULL);

    xTaskCreate(vDeploymentTask, "DEPLOY", 4096, NULL, 5, NULL);

    // [PRIO configMAX_PRIORITIES - 1] - Power Safety: EPS Manager
    xTaskCreate(vEPS_Manager_Task, "EPS_MGMT", 4096, NULL, configMAX_PRIORITIES - 1, NULL);

    // [PRIO 5] - Communications: Command Processor
    xTaskCreate(vCommandProcessorTask, "CMD_PROC", 4096, NULL, 5, NULL);
    xTaskCreate(vYamcsTcReceiverTask, "TC_RX", 4096, NULL, 4, NULL);

    // [PRIO 4] - Maintenance: Global EPS Monitoring & Data Logging
    xTaskCreate(vEPSMonitoringTask, "EPS_MON", 2048, NULL, 4, NULL); 
    xTaskCreate(vDataLoggerTask, "DATA_LOG", 4096, NULL, 4, NULL);

    xTaskCreate(vADCS_Manager_Task, "ADCS_MGMT", 4096, NULL, 4, NULL);

    // [PRIO 3] - Telemetry Generation
    xTaskCreate(vTelemetryGeneratorTask, "TM_GEN", 4096, NULL, 3, NULL);

    // [PRIO 1] - Simulation Tools: Command Injection & Solar Harvesting
    xTaskCreate(vCommandInjectionTask, "CMD_INJECT", 4096, NULL, 1, NULL);
    xTaskCreate(vSolarHarvest_Task, "SOLAR_HARV", 4096, NULL, 1, NULL);

    printf("--- All Systems Integrated and Running. Start Ground Comms Simulation. ---\n");
}
#ifndef HK_SERVICE_H
#define HK_SERVICE_H

#include <stdint.h>

// Each entry is 5 bytes: 4 for value + 1 for is_valid
#define HK_ENTRY_SIZE   5
#define HK_PACKET_SIZE  (HK_MAX_PARAMS * HK_ENTRY_SIZE)

//  1. Uniuqe IDs for every piece of data
typedef enum {
    HK_ID_BATT_VOLTAGE = 0,
    HK_ID_BATT_TEMP,
    HK_ID_SOLAR_PANEL_X,
    HK_ID_ALTITUDE,
    HK_ID_CPU_LOAD,
    HK_MAX_PARAMS  // Keep track of how many items we have
} hk_param_id_t;

// 2. The structure of a single "Health Entry"
typedef struct {
    uint32_t value;        // The actual data
    uint32_t timestamp;    // When it was last updated (simulated ticks)
    uint8_t is_valid;      // 1 if data is fresh , 0 if stale/error
} hk_entry_t;

typedef struct{
    uint32_t low_limit;
    uint32_t high_limit;
} hk_limits_t;

typedef struct __attribute__((packed)) {
    uint32_t uptime_ms_be;
    uint32_t battery_voltage_be;
    uint32_t battery_soc_be;
    uint32_t battery_temp_c_be;
    uint32_t gyro_x_be;
    uint32_t gyro_y_be;
    uint32_t gyro_z_be;
} MasterHK_t;

int HK_CheckLimits(hk_param_id_t id);

// 3. Public API
void HK_Init(void);
int HK_UpdateParam(hk_param_id_t id, uint32_t value, uint32_t current_time);
int HK_ReadParam(hk_param_id_t id, hk_entry_t *out_entry);
void HK_SetLimits(hk_param_id_t id, uint32_t low, uint32_t high);
int HK_Serialize(uint8_t *buffer, uint16_t buffer_size);
void HK_ProcessCommand(const uint8_t* payload, uint16_t len);
void HK_GenerateReport(void);
void HK_RunPeriodic(uint32_t current_time_ms);
#endif
#ifndef HK_SERVICE_H
#define HK_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

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
    uint32_t timestamp;    // Caller-supplied sample time; no implicit unit conversion.
    uint8_t is_valid;      // 1 after update; no automatic age-based expiry.
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
    uint32_t gyro_x_be; // IEEE-754 float bit pattern in network order; rad/s.
    uint32_t gyro_y_be;
    uint32_t gyro_z_be;
} MasterHK_t;

// Inclusive limits: 0 within, 1 outside, -1 unsampled/unavailable, -2 invalid ID.
int HK_CheckLimits(hk_param_id_t id);

// 3. Public API
// Startup only, before tasks. Resets entries, limits [0,0], interval 5 s and due time 0.
// True only after mutex allocation, bounded (10 ms) acquisition and release succeed.
// False is a required-startup failure; caller must not continue or retry at runtime.
// Allocation/acquisition failure leaves data unchanged; release failure is not rolled back.
// Operations fail/skip if the internal mutex is unavailable. No locks span TX.
bool HK_Init(void);
int HK_UpdateParam(hk_param_id_t id, uint32_t value, uint32_t current_time);
int HK_ReadParam(hk_param_id_t id, hk_entry_t *out_entry);
// Invalid IDs or low > high leave the previous limits unchanged.
void HK_SetLimits(hk_param_id_t id, uint32_t low, uint32_t high);
int HK_Serialize(uint8_t *buffer, uint16_t buffer_size);
void HK_ProcessCommand(const uint8_t* payload, uint16_t len);
void HK_GenerateReport(void);
// Monotonic uint32 milliseconds, wrap-safe provided calls are < 2^32 ms apart.
// At most one attempt per call; failure consumes the slot, force report does not.
// SET_INTERVAL accepts 1..60 seconds; the caller must poll at least every second.
void HK_RunPeriodic(uint32_t current_time_ms);
#endif


#include "hk_service.h"
#include "commands.h"
#include "yamcs_interface.h"
#include "adcs_service.h"
#include "eps_telemetry.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

uint32_t telemetry_interval_ms = 10000; // Default 10 seconds

// This is a private "Database" - not visible outside this file
static hk_entry_t s_telemetry_table[HK_MAX_PARAMS];

static hk_limits_t s_limits_table[HK_MAX_PARAMS];


static uint32_t report_interval_ms = 5000; // Default: 5 seconds
static uint32_t last_report_time = 0;

#define HK_MASTER_APID 5U

#define HK_LOG_INFO(fmt, ...) YAMCS_SendEvent("INFO: " fmt, ##__VA_ARGS__)
#define HK_LOG_WARN(fmt, ...) YAMCS_SendEvent("WARN: " fmt, ##__VA_ARGS__)
#define HK_LOG_ERROR(fmt, ...) YAMCS_SendEvent("ERROR: " fmt, ##__VA_ARGS__)

static uint32_t hk_float_to_be(float value) {
    uint32_t raw = 0;
    memcpy(&raw, &value, sizeof(raw));
    return htonl(raw);
}

static void hk_build_master_packet(MasterHK_t *out_packet,
                                   uint32_t uptime_ms,
                                   float battery_voltage,
                                   float battery_soc,
                                   float battery_temp_c,
                                   Vector3_t gyro) {
    out_packet->uptime_ms_be = htonl(uptime_ms);
    out_packet->battery_voltage_be = hk_float_to_be(battery_voltage);
    out_packet->battery_soc_be = hk_float_to_be(battery_soc);
    out_packet->battery_temp_c_be = hk_float_to_be(battery_temp_c);
    out_packet->gyro_x_be = hk_float_to_be(gyro.x);
    out_packet->gyro_y_be = hk_float_to_be(gyro.y);
    out_packet->gyro_z_be = hk_float_to_be(gyro.z);
}


void HK_Init(void){
    // Clear the table: all values to 0, all timestamps to 0, all is_valid to 0
    memset(s_telemetry_table, 0, sizeof(s_telemetry_table));
}

int HK_UpdateParam(hk_param_id_t id, uint32_t value, uint32_t current_time){
    // Safety check: Don't write the array bounds
    if(id >= HK_MAX_PARAMS){
        return -1;
    }

    s_telemetry_table[id].value = value;
    s_telemetry_table[id].timestamp = current_time;
    s_telemetry_table[id].is_valid = 1; // Mark as fresh data
    return 0; // Success
}

int HK_ReadParam(hk_param_id_t id, hk_entry_t *out_entry){
    // Safety check and null pointer protection
    if(id >= HK_MAX_PARAMS || out_entry == NULL){
        return -1;
    }

    // Copy the internal entry to the output pointer
    *out_entry = s_telemetry_table[id];

    return 0;
}

void HK_SetLimits(hk_param_id_t id, uint32_t low, uint32_t high){
    if(id < HK_MAX_PARAMS){
        s_limits_table[id].low_limit = low;
        s_limits_table[id].high_limit = high;
    }

}

int HK_CheckLimits(hk_param_id_t id) {
    // 1. Safety/Freshness Check
    if (id >= HK_MAX_PARAMS) {
        return -2; // Invalid ID error
    }
    
    if (s_telemetry_table[id].is_valid == 0) {
        return -1; // Data is stale or hasn't been sampled yet
    }

    uint32_t val = s_telemetry_table[id].value;
    
    // 2. Limit Comparison
    if (val < s_limits_table[id].low_limit || val > s_limits_table[id].high_limit) {
        return 1; // ALARM: Out of bounds!
    }

    return 0; // NOMINAL: Everything is within range
}

int HK_Serialize(uint8_t *buffer, uint16_t buffer_size){
    if(buffer_size < HK_PACKET_SIZE) return -1;

    // Did not pack time for simplicity

    uint16_t offset = 0;
    for(int i = 0; i < HK_MAX_PARAMS; i++){
        // Pack 32-bit value (Big Endian)
        buffer[offset++] = (s_telemetry_table[i].value >> 24) & 0xFF;
        buffer[offset++] = (s_telemetry_table[i].value >> 16) & 0xFF;
        buffer[offset++] = (s_telemetry_table[i].value >> 8) &  0xFF;
        buffer[offset++] = (s_telemetry_table[i].value) & 0xFF;

        // Pack 8-bit validity status
        buffer[offset++] = s_telemetry_table[i].is_valid;
    }
    return offset; // Return total bytes packed
}


void HK_GenerateReport(void) {
    MasterHK_t master_hk;
    ADCSTelemetry_t adcs_tm;
    hk_entry_t batt_temp_entry;

    memset(&adcs_tm, 0, sizeof(adcs_tm));
    ADCS_GetTelemetry(&adcs_tm);

    float battery_temp_c = 0.0f;
    if (HK_ReadParam(HK_ID_BATT_TEMP, &batt_temp_entry) == 0 && batt_temp_entry.is_valid) {
        battery_temp_c = (float)batt_temp_entry.value;
    }

    uint32_t uptime_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    hk_build_master_packet(
        &master_hk,
        uptime_ms,
        g_eps_telemetry.f_BusVoltage,
        g_eps_telemetry.f_StateOfCharge,
        battery_temp_c,
        adcs_tm.angular_velocity);

    if (!YAMCS_SendTelemetry(HK_MASTER_APID, &master_hk, sizeof(master_hk))) {
        HK_LOG_ERROR("HK report TX failed (APID=%u)", HK_MASTER_APID);
        return;
    }

    HK_LOG_INFO("HK report sent: uptime_ms=%lu", (unsigned long)uptime_ms);
}

void HK_ProcessCommand(const uint8_t* payload, uint16_t len) {
    if (len < 1) {
        HK_LOG_ERROR("CMD_NACK: HK empty payload");
        return;
    }

    uint8_t command_id = payload[0];

    switch (command_id) {
        case HK_CMD_SET_INTERVAL:
            if (len >= 2) {
                // payload[1] is seconds from Earth
                const uint8_t interval_s = payload[1];
                if (interval_s == 0U || interval_s > 60U) {
                    HK_LOG_WARN("CMD_NACK: HK_SET_INTERVAL invalid=%u", interval_s);
                    break;
                }
                report_interval_ms = interval_s * 1000U;
                HK_LOG_INFO("CMD_ACK: HK_SET_INTERVAL=%u s", interval_s);
                HK_LOG_INFO("HK interval updated to %lu ms", (unsigned long)report_interval_ms);
            } else {
                HK_LOG_WARN("CMD_NACK: HK_SET_INTERVAL missing arg");
            }
            break;

        case HK_CMD_FORCE_REPORT:
            HK_LOG_WARN("HK manual report requested");
            HK_GenerateReport();
            break;

        default:
            HK_LOG_ERROR("HK unknown command 0x%02X", command_id);
            break;
    }
}



void HK_RunPeriodic(uint32_t current_time_ms) {
    if (current_time_ms - last_report_time >= report_interval_ms) {
        HK_GenerateReport();
        last_report_time = current_time_ms;
    }
}
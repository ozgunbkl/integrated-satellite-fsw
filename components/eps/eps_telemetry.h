#ifndef EPS_TELEMETRY_H
#define EPS_TELEMETRY_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stddef.h>

// --- BIT MASKING DEFINITIONS (PDU Register) ---
#define PDU_LOAD_PAYLOAD_BIT      (1 << 0)  // Non-Essential Load
#define PDU_LOAD_HEATER_BIT       (1 << 1)  // Non-Essential Load
#define PDU_LOAD_COMMS_BIT        (1 << 2)  // Essential Load (always on)

// --- FAULT FLAG DEFINITIONS (Telemetry struct flags) ---
typedef struct {
    // Packed bit fields for space efficiency (Total size: 1 byte)
    uint8_t low_voltage_warn : 1; // 1 = Voltage is low or non-finite, needs action
    uint8_t over_current_fault : 1; // 1 = Overcurrent detected
    uint8_t over_temp_fault : 1; // 1 = Battery over temperature
    uint8_t safe_mode_active : 1; // 1 = System has load-shed
    uint8_t : 4; // Unused bits for padding/future use
} __attribute__((packed)) FaultFlags_t; // Forces structure to be minimum size

// --- CORE TELEMETRY STRUCTURE ---
typedef struct {
    float f_BusVoltage;          // Simulated main power bus voltage (Volts)
    float f_StateOfCharge;       // Simulated SOC (0..100%); NAN when bus voltage is non-finite.
    float f_SolarInputPower;     // Simulated power being harvested (Watts)
    uint32_t ul_PDU_Register;    // The register we manipulate using bit masking PDU (power distribution unit)
    FaultFlags_t xFaultFlags;    // Packed fault flags
} EPS_Telemetry_t;

// --- EPS STATE MACHINE DEFINITIONS ---
typedef enum {
    EPS_MODE_NOMINAL = 0,
    EPS_MODE_LOW_VOLTAGE,     //Critical warning, preparing to shed loads
    EPS_MODE_SAFE,            //Minimal power usage, loads are shed
    EPS_MODE_RECOVERY         //Attempting to regain charge
} eps_mode_t;

// --- CRITICAL THRESHOLDS (Simulated) ---
#define EPS_VOLTAGE_CRITICAL_LOW (5.0f) // Threshold to enter SAFE_MODE
#define EPS_VOLTAGE_RECOVERY     (8.5f) // Threshold to exit SAFE_MODE

// --- GLOBAL SHARED DATA DECLARATIONS (Externs) ---
// These tell the compiler that these variables exist somewhere else (in eps_main.c)
extern EPS_Telemetry_t g_eps_telemetry;
extern SemaphoreHandle_t xEPSDataMutex;

// Snapshot under xEPSDataMutex; returns EPS_MODE_SAFE if unavailable.
// The local mode is owned by the manager, not a writable shared global.
eps_mode_t EPS_GetMode(void);


#endif // EPS_TELEMETRY_H

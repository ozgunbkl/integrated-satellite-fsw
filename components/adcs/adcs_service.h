#ifndef ADCS_SERVICE_H
#define ADCS_SERVICE_H

#include "adcs_types.h"
#include <stdbool.h>

// ADCS Specific Error Codes
#define ADCS_ERR_NONE             0x00
#define ADCS_ERR_SATURATION       0xA1  // Demo gyro-rate threshold exceeded
#define ADCS_ERR_DIVERGENCE       0xA2  // Nonfinite simulation input/controller output
#define ADCS_ERR_TIMEOUT          0xA3  // Sensor stopped responding

// ADCS Public API
void ADCS_Init(void);
bool ADCS_SetMode(ADCSMode_t mode);
void ADCS_Process(void);  //Main periodic task
ADCSMode_t ADCS_GetMode(void);

// For Telemetry
typedef struct {
    ADCSMode_t mode;
    Vector3_t angular_velocity; // rad/s
    Quaternion_t attitude;     // Scalar-first, unit quaternion (dimensionless).
    Vector3_t last_torque;     // Arbitrary demo torque, NOT calibrated N*m.
    bool actuators_enabled;
} ADCSTelemetry_t;


void ADCS_GetTelemetry(ADCSTelemetry_t* telemetry);

void ADCS_ProcessCommand(const uint8_t* payload, uint16_t len);

Vector3_t ADCS_GetLastCommand(void);
// Actual simulation actuation/boost is authorized while EPS and mode are locked.
// Physics still advances passively with a zero command when control is inhibited.
void ADCS_StepSimulation(float dt); // Seconds; invalid dt -> no-op, state lock failure -> skip.
bool ADCS_TryOrbitBoost(float km_boost); // km, requires sufficient model fuel.
float ADCS_GetAltitude(void); // km; NAN when the state snapshot is unavailable.
float ADCS_GetFuelLevel(void); // Percent; conservative zero on snapshot lock failure.
#endif

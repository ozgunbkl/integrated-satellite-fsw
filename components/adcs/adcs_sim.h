#ifndef ADCS_SIM_H
#define ADCS_SIM_H

#include "adcs_types.h"
#include <stdbool.h>

// Simulates reading a 3-axis Magnetometer
// Returns a vector in micro-Teslas (uT)
Vector3_t SIM_ReadMagnetometer(void);

// Simulates reading a 3-axis Gyroscope
// Returns body angular velocity in rad/s, with bounded +/-0.01 rad/s noise.
Vector3_t SIM_ReadGyroscope(void);

// Startup/test only, before runtime tasks; x/y/z are rad/s. Nonfinite input ignored.
void SIM_SetTrueAngularVelocity(float x, float y, float z);

Quaternion_t SIM_ReadEstimatedAttitude(void);
void SIM_SetTrueAttitude(float q0, float q1, float q2, float q3);

// Raw single-owner model API, not thread-safe. Runtime access belongs to ADCS service.
// braking_cmd is an arbitrary demo effort, NOT a calibrated A*m^2 or N*m value.
// dt is seconds; nonpositive/nonfinite or nonfinite-result steps do not mutate state.
void SIM_UpdatePhysics(Vector3_t braking_cmd, float dt);

// Short, silent simulation mutation; caller owns authorization and diagnostics.
bool SIM_ApplyBoost(float km_boost);
float SIM_GetAltitude(void);
float SIM_GetFuelLevel(void); // Remaining model fuel, percent (0..100).

#endif

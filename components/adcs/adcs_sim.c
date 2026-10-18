#include "adcs_sim.h"
#include "adcs_units.h"
#include "adcs_math.h"
#include <stdlib.h>
#include <math.h>

// Internal "True" state of the simulation
static Vector3_t true_angular_velocity = {0.5f, -0.2f, 0.1f}; // rad/s
static Quaternion_t true_attitude = {1.0f, 0.0f, 0.0f, 0.0f}; // Identity (Perfectly aligned)
static Vector3_t local_mag_sim = {20.0f, 0.0f, 40.0f};

static float g_altitude_km = 548.26f; 
static const float altitude_decay_km_per_s = 0.05f; // Toy decay, not an orbit propagator.

// Resource Management
static float g_fuel_level = 100.0f;     // Percent (%)


// Bounded absolute noise in the caller's units (not a percentage).
static float add_noise(float value, float amlitude) {
    float noise = ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f;  // -1 to 1
    return value + (noise * amlitude);
}

Vector3_t SIM_ReadMagnetometer(void) {
    // Sampling does not advance the plant; only SIM_UpdatePhysics changes the field.
    Vector3_t mag = local_mag_sim;
    Vector3_Normalize(&mag);
    mag.x = add_noise(mag.x * 45.0f, ADCS_MAG_NOISE_UT);
    mag.y = add_noise(mag.y * 45.0f, ADCS_MAG_NOISE_UT);
    mag.z = add_noise(mag.z * 45.0f, ADCS_MAG_NOISE_UT);
    return mag;
}

Vector3_t SIM_ReadGyroscope(void) {
    Vector3_t gyro;
    // Returns the "True" velocity plus some sensor bias/noise
    gyro.x = add_noise(true_angular_velocity.x, ADCS_GYRO_NOISE_RAD_S);
    gyro.y = add_noise(true_angular_velocity.y, ADCS_GYRO_NOISE_RAD_S);
    gyro.z = add_noise(true_angular_velocity.z, ADCS_GYRO_NOISE_RAD_S);
    return gyro;
}

void SIM_SetTrueAngularVelocity(float x, float y, float z) {
    if (!isfinite(x) || !isfinite(y) || !isfinite(z)) return;
    true_angular_velocity.x = x;
    true_angular_velocity.y = y;
    true_angular_velocity.z = z;
}

Quaternion_t SIM_ReadEstimatedAttitude(void) {
    return true_attitude;
}

void SIM_SetTrueAttitude(float q0, float q1, float q2, float q3) {
    if (!isfinite(q0) || !isfinite(q1) || !isfinite(q2) || !isfinite(q3)) return;
    Quaternion_t attitude = {q0, q1, q2, q3};
    Quat_Normalize(&attitude);
    const float norm2 = attitude.q0*attitude.q0 + attitude.q1*attitude.q1 +
                        attitude.q2*attitude.q2 + attitude.q3*attitude.q3;
    if (!isfinite(norm2) || fabsf(norm2 - 1.0f) > 1e-4f) return;
    true_attitude = attitude;
}


// This function simulates the physical interaction in space

void SIM_UpdatePhysics(Vector3_t braking_cmd, float dt) {
    if (!isfinite(dt) || dt <= 0.0f || !isfinite(braking_cmd.x) ||
        !isfinite(braking_cmd.y) || !isfinite(braking_cmd.z)) return;
    const float steps = dt / ADCS_CYCLE_S;
    const float damping = powf(0.95f, steps);
    const float effect = 0.2f * steps;
    Vector3_t velocity = {
        (true_angular_velocity.x - braking_cmd.x * effect) * damping,
        (true_angular_velocity.y - braking_cmd.y * effect) * damping,
        (true_angular_velocity.z - braking_cmd.z * effect) * damping
    };
    float altitude = g_altitude_km - altitude_decay_km_per_s * dt;
    Vector3_t field = local_mag_sim;
    field.x += true_angular_velocity.y * 0.01f * steps;
    field.y -= true_angular_velocity.x * 0.01f * steps;
    // Commit together, so invalid/overflowed steps cannot partly poison the plant.
    if (!isfinite(velocity.x) || !isfinite(velocity.y) || !isfinite(velocity.z) ||
        !isfinite(altitude) || !isfinite(field.x) || !isfinite(field.y)) return;
    Vector3_Normalize(&field);
    field.x *= 45.0f; field.y *= 45.0f; field.z *= 45.0f;
    if (altitude < 0.0f) altitude = 0.0f;
    true_angular_velocity = velocity;
    local_mag_sim = field;
    g_altitude_km = altitude;
}


float SIM_GetAltitude(void) {
    return g_altitude_km;
}


bool SIM_ApplyBoost(float km_boost) {
    if (!isfinite(km_boost) || km_boost <= 0.0f) return false;
    const float fuel_cost = (km_boost / 10.0f) * 1.5f; // Percent, 1.5% per 10 km.
    const float altitude = g_altitude_km + km_boost;
    if (!isfinite(fuel_cost) || !isfinite(altitude) || fuel_cost <= 0.0f ||
        altitude <= g_altitude_km || fuel_cost > g_fuel_level ||
        g_fuel_level <= 0.0f) return false;
    g_altitude_km = altitude;
    g_fuel_level -= fuel_cost;
    return true;
}

float SIM_GetFuelLevel(void) {
    return g_fuel_level;
}

#ifndef ADCS_UNITS_H
#define ADCS_UNITS_H

// Common units for the simulation, controller and task (not hardware calibration).
#define ADCS_CYCLE_MS 100U
#define ADCS_CYCLE_S (ADCS_CYCLE_MS / 1000.0f)
#define ADCS_RAD_PER_DEG 0.017453292519943295f
#define ADCS_MAX_RATE_RAD_S (3000.0f * ADCS_RAD_PER_DEG)
#define ADCS_STABLE_RATE_RAD_S 0.25f
#define ADCS_GYRO_NOISE_RAD_S 0.01f
#define ADCS_MAG_NOISE_UT 0.5f

#endif

#ifndef ADCS_CONTROL_H
#define ADCS_CONTROL_H

#include "adcs_types.h"

// Demo B-Dot effort, not a calibrated magnetic moment (A*m^2).
// mag_now/mag_prev are uT; dt is seconds. Nonfinite/nonpositive dt -> zero effort.
Vector3_t Control_BDot(Vector3_t mag_now, Vector3_t mag_prev, float dt);
// Scalar-first error quaternion, omega in rad/s; output is arbitrary demo torque.
// The service converts torque sign to the mock plant's braking convention.
Vector3_t Control_PD(Quaternion_t q_error, Vector3_t omega);

#endif

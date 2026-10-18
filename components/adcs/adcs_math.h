#ifndef ADCS_MATH_H
#define ADCS_MATH_H

#include "adcs_types.h"
#include <math.h>
#include <stdbool.h>

// Normalization: null -> no-op; norm <=1e-6 or nonfinite -> unchanged.
// Finite large inputs use scaled arithmetic to avoid norm overflow.
// Vector Operations
void Vector3_Normalize(Vector3_t* v);
float Vector3_Dot(Vector3_t a, Vector3_t b);
Vector3_t Vector3_Cross(Vector3_t a, Vector3_t b);

// Ensures the quaternion has a magnitude of 1.0
void Quat_Normalize(Quaternion_t* q);

// Scalar-first Hamilton product; res may alias either operand. Null -> no-op.
void Quat_Multiply(const Quaternion_t* q_left, const Quaternion_t* q_right, Quaternion_t* res);

// Right-handed active rotation matrix for a finite UNIT quaternion (normalize first).
// q and -q represent the same rotation. Null -> no-op; no invalid-input recovery.
void Quat_To_RotationMatrix(const Quaternion_t* q, float R[3][3]);

void Quat_Conjugate(const Quaternion_t* q, Quaternion_t* res);

#endif

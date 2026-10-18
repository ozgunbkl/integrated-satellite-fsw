#include "adcs_math.h"
#include <math.h>

#define EPSILON 1e-6f

// Scale before squaring so finite large components do not overflow their norm.
// Degenerate/nonfinite inputs remain unchanged for the caller to diagnose.
static bool normalize_values(float *values, unsigned count) {
    float scale = 0.0f;
    for (unsigned i = 0; i < count; ++i) {
        if (!isfinite(values[i])) return false;
        scale = fmaxf(scale, fabsf(values[i]));
    }
    if (scale == 0.0f) return false;
    float sum = 0.0f;
    for (unsigned i = 0; i < count; ++i) {
        const float value = values[i] / scale;
        sum += value * value;
    }
    const float norm = sqrtf(sum);
    if (scale <= EPSILON && scale * norm <= EPSILON) return false;
    for (unsigned i = 0; i < count; ++i) values[i] = (values[i] / scale) / norm;
    return true;
}

// --- Vector Math ---

void Vector3_Normalize(Vector3_t* v) {
    if (!v) return;
    float values[] = {v->x, v->y, v->z};
    if (normalize_values(values, 3)) *v = (Vector3_t){values[0], values[1], values[2]};
}

float Vector3_Dot(Vector3_t a, Vector3_t b) {
    return (a.x * b.x + a.y * b.y + a.z * b.z);
}

Vector3_t Vector3_Cross(Vector3_t a, Vector3_t b) {
    Vector3_t res;
    res.x = a.y * b.z - a.z * b.y;
    res.y = a.z * b.x - a.x * b.z;
    res.z = a.x * b.y - a.y * b.x;
    return res;
}

// --- Quaternion Math ---

void Quat_Normalize(Quaternion_t* q) {
    if (!q) return;
    float values[] = {q->q0, q->q1, q->q2, q->q3};
    if (normalize_values(values, 4)) *q = (Quaternion_t){values[0], values[1], values[2], values[3]};
}

void Quat_Multiply(const Quaternion_t* a, const Quaternion_t* b, Quaternion_t* res) {
    if (!a || !b || !res) return;
    // Standard Hamilton product
    const Quaternion_t product = {
        a->q0*b->q0 - a->q1*b->q1 - a->q2*b->q2 - a->q3*b->q3,
        a->q0*b->q1 + a->q1*b->q0 + a->q2*b->q3 - a->q3*b->q2,
        a->q0*b->q2 - a->q1*b->q3 + a->q2*b->q0 + a->q3*b->q1,
        a->q0*b->q3 + a->q1*b->q2 - a->q2*b->q1 + a->q3*b->q0
    };
    *res = product; // Safe when res aliases either operand.
}

void Quat_Conjugate(const Quaternion_t* q, Quaternion_t* res) {
    if (!q || !res) return;
    res->q0 = q->q0;   // Scalar part remains the same
    res->q1 = -q->q1;  // Vector part is negated
    res->q2 = -q->q2;
    res->q3 = -q->q3;
}

void Quat_To_RotationMatrix(const Quaternion_t* q, float R[3][3]) {
    if (!q || !R) return;
    const float w = q->q0, x = q->q1, y = q->q2, z = q->q3;
    R[0][0] = 1.0f - 2.0f * (y*y + z*z);
    R[0][1] = 2.0f * (x*y - w*z);
    R[0][2] = 2.0f * (x*z + w*y);
    R[1][0] = 2.0f * (x*y + w*z);
    R[1][1] = 1.0f - 2.0f * (x*x + z*z);
    R[1][2] = 2.0f * (y*z - w*x);
    R[2][0] = 2.0f * (x*z - w*y);
    R[2][1] = 2.0f * (y*z + w*x);
    R[2][2] = 1.0f - 2.0f * (x*x + y*y);
}


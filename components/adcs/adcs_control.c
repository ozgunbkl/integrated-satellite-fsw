#include "adcs_control.h"
#include <math.h>

#define BDOT_GAIN 2.5f  // Tuning constant
#define KP_GAIN 10.0f    // Proportional gain
#define KD_GAIN 5.0f     // Derivative gain

Vector3_t Control_BDot(Vector3_t mag_now, Vector3_t mag_prev, float dt) {
    Vector3_t b_dot;
    Vector3_t dipole_cmd;

    // 1. Calculate rate of change(Derivative)
    if(isfinite(dt) && dt > 0.0f){
        b_dot.x = (mag_now.x - mag_prev.x) / dt;
        b_dot.y = (mag_now.y - mag_prev.y) / dt;
        b_dot.z = (mag_now.z - mag_prev.z) / dt;
    } 
    else {
        b_dot.x = 0.0;
        b_dot.y = 0.0;
        b_dot.z = 0.0;
    }

    // 2. Control Law: m = -k * b_dot
    dipole_cmd.x = -BDOT_GAIN * b_dot.x;
    dipole_cmd.y = -BDOT_GAIN * b_dot.y;
    dipole_cmd.z = -BDOT_GAIN * b_dot.z;

    return dipole_cmd;
}


Vector3_t Control_PD(Quaternion_t q_error, Vector3_t omega){
    Vector3_t torque_cmd;

    // Command = (Kp * Error_Vector) - (Kd * Angular_Velocity)
    // Note: q1, q2, q3 represent the vector part of the quaternion
    torque_cmd.x = (KP_GAIN * q_error.q1) - (KD_GAIN * omega.x);
    torque_cmd.y = (KP_GAIN * q_error.q2) - (KD_GAIN * omega.y);
    torque_cmd.z = (KP_GAIN * q_error.q3) - (KD_GAIN * omega.z);

    return torque_cmd;
}


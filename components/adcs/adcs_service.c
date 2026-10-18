#include "adcs_service.h"
#include "adcs_sim.h"
#include "adcs_control.h"
#include "adcs_math.h"
#include "adcs_units.h"
#include"adcs_types.h"
#include "fdir_service.h"
#include "time_service.h"
#include "commands.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "eps_service.h"

static Vector3_t g_last_dipole_cmd = {0.0f, 0.0f, 0.0f};


static ADCSMode_t current_mode = ADCS_MODE_SAFE;
static bool actuators_enabled = false;
static Vector3_t last_mag_reading = {0, 0, 0};
static Vector3_t current_torque = {0, 0, 0};
static SemaphoreHandle_t s_adcs_mutex = NULL;
static bool mag_history_valid = false;

static bool adcs_lock(void) {
    return s_adcs_mutex != NULL && xSemaphoreTake(s_adcs_mutex, pdMS_TO_TICKS(10)) == pdTRUE;
}

static void adcs_clear_output_locked(void) {
    g_last_dipole_cmd = (Vector3_t){0, 0, 0};
    current_torque = (Vector3_t){0, 0, 0};
    last_mag_reading = (Vector3_t){0, 0, 0};
    mag_history_valid = false;
}

static void adcs_inhibit_locked(void) {
    current_mode = ADCS_MODE_SAFE;
    actuators_enabled = false;
    adcs_clear_output_locked();
}

static EpsOperation_t adcs_operation_locked(void) {
    return current_mode == ADCS_MODE_DETUMBLE ? EPS_OP_DETUMBLE : EPS_OP_POINTING;
}

static void adcs_permission_only(SystemMode_t mode, void *context) { }


void ADCS_Init(void) {
    if (s_adcs_mutex == NULL) s_adcs_mutex = xSemaphoreCreateMutex();
    if (s_adcs_mutex == NULL || xSemaphoreTake(s_adcs_mutex, portMAX_DELAY) != pdTRUE) {
        printf("ADCS: Required state mutex initialization failed.\n");
        abort();
    }
    current_mode = ADCS_MODE_DETUMBLE;  // Start with stopping the spin
    actuators_enabled = true;
    adcs_clear_output_locked();
    xSemaphoreGive(s_adcs_mutex);
    printf("ADCS: Initialized in DETUMBLE mode.\n");
}

static void adcs_set_mode_action(SystemMode_t system_mode, void *context) {
    current_mode = *(const ADCSMode_t *)context;
    adcs_clear_output_locked();
    actuators_enabled = true;
}

bool ADCS_SetMode(ADCSMode_t mode) {
    // SUN_SAFE/PAYLOAD_POINT have no implemented controller; never accept a stale output mode.
    if (mode != ADCS_MODE_SAFE && mode != ADCS_MODE_DETUMBLE && mode != ADCS_MODE_NOMINAL) return false;
    if (!adcs_lock()) return false;
    bool accepted = false;
    if (mode == ADCS_MODE_SAFE) {
        adcs_inhibit_locked();
        accepted = true;
    } else if (current_mode != ADCS_MODE_SAFE || mode == ADCS_MODE_DETUMBLE) {
        accepted = EPS_RunIfPermitted(mode == ADCS_MODE_DETUMBLE ? EPS_OP_DETUMBLE : EPS_OP_POINTING,
                                      adcs_set_mode_action, &mode);
        if (!accepted) adcs_inhibit_locked();
    }
    xSemaphoreGive(s_adcs_mutex);
    return accepted;
}

typedef struct {
    Vector3_t command;
    Vector3_t magnetometer;
    bool stable;
} AdcsFrame_t;

static void adcs_publish_frame(SystemMode_t mode, void *context) {
    const AdcsFrame_t *frame = context;
    if (current_mode == ADCS_MODE_DETUMBLE) {
        g_last_dipole_cmd = frame->command;
        current_torque = (Vector3_t){0, 0, 0};
        if (frame->stable && mode != MODE_INITIAL_BOOT) current_mode = ADCS_MODE_NOMINAL;
    } else {
        current_torque = frame->command;
        g_last_dipole_cmd = (Vector3_t){0, 0, 0};
    }
    last_mag_reading = frame->magnetometer;
    mag_history_valid = true;
    actuators_enabled = true;
}

void ADCS_Process(void) {
    if (!adcs_lock()) return;
    if ((current_mode != ADCS_MODE_DETUMBLE && current_mode != ADCS_MODE_NOMINAL) ||
        !EPS_RunIfPermitted(adcs_operation_locked(), adcs_permission_only, NULL)) {
        adcs_inhibit_locked();
        xSemaphoreGive(s_adcs_mutex);
        return;
    }
    Vector3_t gyro = SIM_ReadGyroscope();
    Vector3_t mag_now = SIM_ReadMagnetometer();
    const bool invalid_sensor = !isfinite(gyro.x) || !isfinite(gyro.y) || !isfinite(gyro.z) ||
        !isfinite(mag_now.x) || !isfinite(mag_now.y) || !isfinite(mag_now.z);
    // Keep the intended 3000 deg/s limit, converted to the rad/s sensor contract.
    const bool saturation = fabsf(gyro.x) > ADCS_MAX_RATE_RAD_S ||
        fabsf(gyro.y) > ADCS_MAX_RATE_RAD_S || fabsf(gyro.z) > ADCS_MAX_RATE_RAD_S;
    AdcsFrame_t frame = {.command = {0, 0, 0}, .magnetometer = mag_now, .stable = false};
    if (current_mode == ADCS_MODE_DETUMBLE) {
        const float spin = sqrtf(gyro.x * gyro.x + gyro.y * gyro.y + gyro.z * gyro.z);
        frame.stable = spin < ADCS_STABLE_RATE_RAD_S;
        // A fresh first sample primes B-Dot history instead of commanding a stale derivative.
        if (!frame.stable && mag_history_valid) frame.command = Control_BDot(mag_now, last_mag_reading, ADCS_CYCLE_S);
    } else {
        Quaternion_t q_current = SIM_ReadEstimatedAttitude();
        Quaternion_t q_target = {1, 0, 0, 0}, q_curr_inv, q_error;
        Quat_Conjugate(&q_current, &q_curr_inv);
        Quat_Multiply(&q_target, &q_curr_inv, &q_error);
        frame.command = Control_PD(q_error, gyro);
    }
    const bool invalid_frame = invalid_sensor || !isfinite(frame.command.x) ||
        !isfinite(frame.command.y) || !isfinite(frame.command.z);
    if (saturation || invalid_frame || !EPS_RunIfPermitted(adcs_operation_locked(), adcs_publish_frame, &frame)) {
        adcs_inhibit_locked();
    }
    xSemaphoreGive(s_adcs_mutex);
    if (saturation || invalid_frame) {
        FaultReport_t fault = {.source = SRC_ADCS, .severity = FAULT_WARNING,
            .fault_code = invalid_frame ? ADCS_ERR_DIVERGENCE : ADCS_ERR_SATURATION,
            .timestamp = TIME_GetSeconds()};
        FDIR_ReportFault(fault);
        printf("ADCS: Invalid/saturated simulation input or output; control inhibited.\n");
    }
}

void ADCS_GetTelemetry(ADCSTelemetry_t* telemetry) {
    if (telemetry == NULL) return;
    *telemetry = (ADCSTelemetry_t){.mode = ADCS_MODE_SAFE};
    if (!adcs_lock()) return;
    if (actuators_enabled && !EPS_RunIfPermitted(adcs_operation_locked(), adcs_permission_only, NULL)) {
        adcs_inhibit_locked();
    }
    telemetry->mode = current_mode;
    telemetry->angular_velocity = SIM_ReadGyroscope();
    telemetry->attitude = SIM_ReadEstimatedAttitude();
    telemetry->last_torque = current_torque;
    telemetry->actuators_enabled = actuators_enabled;
    xSemaphoreGive(s_adcs_mutex);
}


// This function is CALLED by the Router when a packet for ADCS arrives
void ADCS_ProcessCommand(const uint8_t* payload, uint16_t len) {
    if (payload == NULL || len < 1) {
        printf("ADCS ERROR: Command packet too short!\n");
        return;
    }

    uint8_t command_id = payload[0]; // The first byte is the "What to do"

    switch (command_id) {
        case ADCS_CMD_SET_MODE:
            if (len >= 2) {
                // The second byte (payload[1]) is the mode value
                ADCSMode_t requested_mode = (ADCSMode_t)payload[1]; 
                const bool accepted = ADCS_SetMode(requested_mode);
                printf("ADCS: SetMode command %s.\n", accepted ? "accepted" : "rejected");
            } else {
                printf("ADCS: SetMode command rejected; missing mode.\n");
            }
            break;

        case ADCS_CMD_RESET_ESTIMATION:
            // Logic to reset the filters/quaternions
            printf("ADCS: Reset estimation is a stub; no filter state changed.\n");
            break;

        default:
            printf("ADCS: Unknown Command ID 0x%02X recieved.\n", command_id);
            break;
    }
}


Vector3_t ADCS_GetLastCommand(void) {
    Vector3_t command = {0, 0, 0};
    if (!adcs_lock()) return command;
    if (!actuators_enabled || !EPS_RunIfPermitted(adcs_operation_locked(), adcs_permission_only, NULL)) {
        adcs_inhibit_locked();
    } else {
        command = current_mode == ADCS_MODE_NOMINAL ? current_torque : g_last_dipole_cmd;
    }
    xSemaphoreGive(s_adcs_mutex);
    return command;
}

// Body for the ADCS Mode Getter
ADCSMode_t ADCS_GetMode(void) {
    if (!adcs_lock()) return ADCS_MODE_SAFE;
    const ADCSMode_t mode = current_mode;
    xSemaphoreGive(s_adcs_mutex);
    return mode;
}

static void adcs_physics_action(SystemMode_t mode, void *context) {
    const float dt = *(const float *)context;
    Vector3_t braking = g_last_dipole_cmd;
    if (current_mode == ADCS_MODE_NOMINAL) {
        // PD produces torque; the legacy toy plant subtracts braking effort.
        braking = (Vector3_t){-current_torque.x, -current_torque.y, -current_torque.z};
    }
    SIM_UpdatePhysics(braking, dt);
}

void ADCS_StepSimulation(float dt) {
    if (!isfinite(dt) || dt <= 0.0f || !adcs_lock()) return;
    if (!actuators_enabled || !EPS_RunIfPermitted(adcs_operation_locked(), adcs_physics_action, &dt)) {
        adcs_inhibit_locked();
        SIM_UpdatePhysics((Vector3_t){0, 0, 0}, dt);
    }
    xSemaphoreGive(s_adcs_mutex);
}

float ADCS_GetAltitude(void) {
    if (!adcs_lock()) return NAN;
    const float altitude = SIM_GetAltitude();
    xSemaphoreGive(s_adcs_mutex);
    return altitude;
}

float ADCS_GetFuelLevel(void) {
    if (!adcs_lock()) return 0.0f;
    const float fuel = SIM_GetFuelLevel();
    xSemaphoreGive(s_adcs_mutex);
    return fuel;
}

typedef struct { float distance; bool applied; } AdcsBoost_t;
static void adcs_boost_action(SystemMode_t mode, void *context) {
    AdcsBoost_t *boost = context;
    if (current_mode == ADCS_MODE_NOMINAL && actuators_enabled && SIM_GetAltitude() < 500.0f) {
        boost->applied = SIM_ApplyBoost(boost->distance);
    }
}

bool ADCS_TryOrbitBoost(float km_boost) {
    if (!isfinite(km_boost) || km_boost <= 0 || !adcs_lock()) return false;
    AdcsBoost_t boost = {.distance = km_boost, .applied = false};
    (void)EPS_RunIfPermitted(EPS_OP_ORBIT_BOOST, adcs_boost_action, &boost);
    xSemaphoreGive(s_adcs_mutex);
    return boost.applied;
}

#ifndef EPS_SIM_MODEL_H
#define EPS_SIM_MODEL_H

#include <math.h>

/* Demonstration model, NOT calibrated battery chemistry. A 20 Wh equivalent
 * store maps linearly to a 0..9 V bus proxy. Sun/shadow last 60 s each; 24 W
 * sunlight supports comms 1 W + payload 5 W + heater 3 W with explicit losses.
 * No controller manufactures solar energy or disables protection.
 */
#define EPS_SIM_CAPACITY_WH 20.0f
#define EPS_SIM_MAX_VOLTAGE 9.0f
#define EPS_SIM_SUN_POWER_W 24.0f
#define EPS_SIM_INITIAL_VOLTAGE 8.5f

static inline float EPS_SIM_StateOfCharge(float voltage) {
    if (!isfinite(voltage)) return NAN;
    if (voltage <= 0.0f) return 0.0f;
    if (voltage >= EPS_SIM_MAX_VOLTAGE) return 100.0f;
    return voltage / EPS_SIM_MAX_VOLTAGE * 100.0f;
}

static inline float EPS_SIM_StepVoltage(float voltage, float solar_w, float load_w, float dt_s) {
    /* Invalid observations stay invalid; never mask NaN/Inf as healthy power. */
    if (!isfinite(voltage) || !isfinite(solar_w) || !isfinite(load_w) ||
        !isfinite(dt_s) || solar_w < 0 || load_w < 0 || dt_s < 0) return NAN;
    double net_w = (double)solar_w - load_w;
    net_w = net_w >= 0 ? net_w * 0.90 : net_w / 0.90;
    const double energy_wh = (double)voltage / EPS_SIM_MAX_VOLTAGE * EPS_SIM_CAPACITY_WH +
                             (double)net_w * dt_s / 3600.0;
    if (energy_wh <= 0) return 0;
    if (energy_wh >= EPS_SIM_CAPACITY_WH) return EPS_SIM_MAX_VOLTAGE;
    return (float)(energy_wh / EPS_SIM_CAPACITY_WH * EPS_SIM_MAX_VOLTAGE);
}

#endif

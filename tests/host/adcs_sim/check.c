#include "adcs_sim.h"
#include "adcs_units.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void near(float a, float b, float tolerance) { assert(isfinite(a) && fabsf(a-b) < tolerance); }
static Vector3_t gyro(void) { srand(7); return SIM_ReadGyroscope(); }
static Vector3_t mag(void) { srand(7); return SIM_ReadMagnetometer(); }
static void same(Vector3_t a, Vector3_t b) { near(a.x,b.x,1e-6f); near(a.y,b.y,1e-6f); near(a.z,b.z,1e-6f); }

int main(void) {
    SIM_SetTrueAngularVelocity(1,-2,0.5f);
    Vector3_t sample=gyro(); near(sample.x,1,0.0101f); near(sample.y,-2,0.0101f); near(sample.z,0.5f,0.0101f);
    const Vector3_t field=mag(); const float altitude=SIM_GetAltitude();
    for (unsigned i=0;i<30;++i) { (void)SIM_ReadMagnetometer(); (void)SIM_ReadGyroscope(); }
    same(mag(),field); same(gyro(),sample); assert(SIM_GetAltitude()==altitude);
    SIM_SetTrueAngularVelocity(NAN,0,0); same(gyro(),sample);
    SIM_SetTrueAngularVelocity(INFINITY,0,0); same(gyro(),sample);

    const float invalid_dt[]={0,-0.1f,NAN,INFINITY,FLT_MAX};
    for (unsigned i=0;i<sizeof(invalid_dt)/sizeof(invalid_dt[0]);++i) {
        SIM_UpdatePhysics((Vector3_t){0},invalid_dt[i]);
        same(gyro(),sample); same(mag(),field); assert(SIM_GetAltitude()==altitude);
    }
    SIM_UpdatePhysics((Vector3_t){NAN,0,0},0.1f);
    SIM_UpdatePhysics((Vector3_t){FLT_MAX,0,0},10);
    same(gyro(),sample); same(mag(),field); assert(SIM_GetAltitude()==altitude);

    SIM_UpdatePhysics((Vector3_t){0},0.2f); const Vector3_t full=gyro();
    near(full.x,0.9025f,0.0101f); near(SIM_GetAltitude(),altitude-0.01f,2e-4f);
    SIM_SetTrueAngularVelocity(1,-2,0.5f);
    const float before_halves=SIM_GetAltitude();
    SIM_UpdatePhysics((Vector3_t){0},0.1f); SIM_UpdatePhysics((Vector3_t){0},0.1f);
    same(gyro(),full); near(SIM_GetAltitude(),before_halves-0.01f,2e-4f);
    SIM_SetTrueAngularVelocity(1,0,0);
    SIM_UpdatePhysics((Vector3_t){5,0,0},ADCS_CYCLE_S);
    near(gyro().x,0,0.0101f);

    SIM_SetTrueAttitude(2,0,0,0); Quaternion_t q=SIM_ReadEstimatedAttitude(); assert(q.q0==1 && q.q1==0);
    SIM_SetTrueAttitude(0,0,0,0); assert(SIM_ReadEstimatedAttitude().q0==1);
    SIM_SetTrueAttitude(NAN,0,0,0); assert(SIM_ReadEstimatedAttitude().q0==1);
    SIM_SetTrueAttitude(0,FLT_MAX,0,0); q=SIM_ReadEstimatedAttitude(); assert(q.q0==0 && q.q1==1);

    float fuel=SIM_GetFuelLevel(), height=SIM_GetAltitude();
    const float invalid_boost[]={0,-1,NAN,INFINITY,FLT_MAX,FLT_MIN,1000};
    for (unsigned i=0;i<sizeof(invalid_boost)/sizeof(invalid_boost[0]);++i) {
        assert(!SIM_ApplyBoost(invalid_boost[i]));
        assert(SIM_GetFuelLevel()==fuel && SIM_GetAltitude()==height);
    }
    assert(SIM_ApplyBoost(10)); near(SIM_GetFuelLevel(),98.5f,1e-5f);
    near(SIM_GetAltitude(),height+10,1e-4f);
    unsigned burns=1;
    while (SIM_ApplyBoost(10)) { ++burns; assert(burns<100); }
    assert(burns==66); near(SIM_GetFuelLevel(),1,1e-5f);
    height=SIM_GetAltitude(); assert(!SIM_ApplyBoost(10)); assert(SIM_GetAltitude()==height);
    puts("PASS: rad/s and uT sampling, dt-scaled plant, invalid-state atomicity, normalized attitude and fuel-limited km boosts.");
    return 0;
}

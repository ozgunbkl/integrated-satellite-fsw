#include "adcs_math.h"
#include "adcs_control.h"
#include "adcs_units.h"
#include <assert.h>
#include <float.h>
#include <stdio.h>

static void near(float actual, float expected) { assert(isfinite(actual) && fabsf(actual - expected) < 2e-5f); }
static void vector(Vector3_t a, Vector3_t b) { near(a.x,b.x); near(a.y,b.y); near(a.z,b.z); }
static void quaternion(Quaternion_t a, Quaternion_t b) { near(a.q0,b.q0); near(a.q1,b.q1); near(a.q2,b.q2); near(a.q3,b.q3); }

int main(void) {
    Vector3_t v = {3,4,0}; Vector3_Normalize(&v); vector(v, (Vector3_t){0.6f,0.8f,0});
    v = (Vector3_t){FLT_MAX,FLT_MAX,0}; Vector3_Normalize(&v);
    vector(v, (Vector3_t){sqrtf(0.5f),sqrtf(0.5f),0});
    v = (Vector3_t){0}; Vector3_Normalize(&v); vector(v, (Vector3_t){0});
    v = (Vector3_t){1e-8f,0,0}; Vector3_Normalize(&v); assert(v.x == 1e-8f);
    v = (Vector3_t){NAN,1,0}; Vector3_Normalize(&v); assert(isnan(v.x) && v.y == 1);
    Vector3_Normalize(NULL); Quat_Normalize(NULL);
    near(Vector3_Dot((Vector3_t){1,2,3}, (Vector3_t){-2,4,1}), 9);
    vector(Vector3_Cross((Vector3_t){1,0,0}, (Vector3_t){0,1,0}), (Vector3_t){0,0,1});

    Quaternion_t q = {2,0,0,0}; Quat_Normalize(&q); quaternion(q, (Quaternion_t){1,0,0,0});
    q = (Quaternion_t){FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX}; Quat_Normalize(&q);
    quaternion(q, (Quaternion_t){0.5f,0.5f,0.5f,0.5f});
    q = (Quaternion_t){0}; Quat_Normalize(&q); quaternion(q, (Quaternion_t){0});
    q = (Quaternion_t){INFINITY,1,0,0}; Quat_Normalize(&q); assert(isinf(q.q0) && q.q1 == 1);
    Quaternion_t a = {1,2,3,4}, b = {-2,1,-3,2}, product;
    Quat_Multiply(&a,&b,&product); quaternion(product, (Quaternion_t){-3,15,-9,-15});
    Quaternion_t alias = a; Quat_Multiply(&alias,&b,&alias); quaternion(alias,product);
    alias = b; Quat_Multiply(&a,&alias,&alias); quaternion(alias,product);
    Quaternion_t conjugate; q = (Quaternion_t){0.5f,0.5f,0.5f,0.5f};
    Quat_Conjugate(&q,&conjugate); Quat_Multiply(&q,&conjugate,&product);
    quaternion(product,(Quaternion_t){1,0,0,0});
    Quat_Conjugate(&q,&q); quaternion(q,conjugate);

    const float s = sqrtf(0.5f);
    const Quaternion_t rotation = {s,0,0,s}, opposite = {-s,0,0,-s};
    float R[3][3], N[3][3]; Quat_To_RotationMatrix(&rotation,R); Quat_To_RotationMatrix(&opposite,N);
    near(R[0][0],0); near(R[1][0],1); near(R[2][0],0); // Active +90 degree Z rotation.
    for (unsigned i=0;i<3;++i) for (unsigned j=0;j<3;++j) {
        near(R[i][j],N[i][j]);
        float dot=0; for (unsigned k=0;k<3;++k) dot += R[k][i]*R[k][j];
        near(dot, i==j ? 1 : 0);
    }
    Quat_To_RotationMatrix(NULL,R); Quat_To_RotationMatrix(&rotation,NULL);
    const Vector3_t now={2,4,6}, previous={1,2,3};
    vector(Control_BDot(now,previous,0.5f),(Vector3_t){-5,-10,-15});
    const float invalid_dt[]={0,-1,NAN,INFINITY};
    for (unsigned i=0;i<sizeof(invalid_dt)/sizeof(invalid_dt[0]);++i)
        vector(Control_BDot(now,previous,invalid_dt[i]),(Vector3_t){0});
    vector(Control_PD((Quaternion_t){1,0,0,0}, (Vector3_t){1,-2,3}), (Vector3_t){-5,10,-15});
    near(ADCS_CYCLE_S,0.1f); near(180*ADCS_RAD_PER_DEG,3.14159265f);
    assert(ADCS_MAX_RATE_RAD_S > 52.35f && ADCS_MAX_RATE_RAD_S < 52.37f);
    puts("PASS: scaled norms, vector algebra, alias-safe Hamilton product, unit rotation matrix and demo controller units.");
    return 0;
}

/*
 * Clean-room libultra-style axis-angle rotation helpers for the matching
 * target.
 */

#include "guint.h"

#define GU_DEG_TO_RAD (3.14159265358979323846f / 180.0f)

void guRotateF(float mf[4][4], float angle, float x, float y, float z)
{
    float radians;
    float s;
    float c;
    float one_minus_c;

    guNormalize(&x, &y, &z);

    radians = angle * GU_DEG_TO_RAD;
    s = sinf(radians);
    c = cosf(radians);
    one_minus_c = 1.0f - c;

    guMtxIdentF(mf);

    mf[0][0] = c + x * x * one_minus_c;
    mf[0][1] = x * y * one_minus_c + z * s;
    mf[0][2] = x * z * one_minus_c - y * s;

    mf[1][0] = y * x * one_minus_c - z * s;
    mf[1][1] = c + y * y * one_minus_c;
    mf[1][2] = y * z * one_minus_c + x * s;

    mf[2][0] = z * x * one_minus_c + y * s;
    mf[2][1] = z * y * one_minus_c - x * s;
    mf[2][2] = c + z * z * one_minus_c;
}

void guRotate(Mtx *m, float angle, float x, float y, float z)
{
    Matrix mf;

    guRotateF(mf, angle, x, y, z);
    guMtxF2L(mf, m);
}

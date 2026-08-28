/*
 * Clean-room libultra-style alignment matrix helpers for the matching target.
 */

#include "guint.h"

#define GU_DEG_TO_RAD (3.14159265358979323846f / 180.0f)

void guAlignF(float mf[4][4], float angle, float x, float y, float z)
{
    float h;
    float inv_h;
    float s;
    float c;
    float radians;

    guNormalize(&x, &y, &z);
    guMtxIdentF(mf);

    h = sqrtf(x * x + z * z);
    if (h <= 0.0f) {
        return;
    }

    radians = angle * GU_DEG_TO_RAD;
    s = sinf(radians);
    c = cosf(radians);
    inv_h = 1.0f / h;

    mf[0][0] = (-z * c - s * y * x) * inv_h;
    mf[1][0] = (z * s - c * y * x) * inv_h;
    mf[2][0] = -x;

    mf[0][1] = s * h;
    mf[1][1] = c * h;
    mf[2][1] = -y;

    mf[0][2] = (c * x - s * y * z) * inv_h;
    mf[1][2] = (-s * x - c * y * z) * inv_h;
    mf[2][2] = -z;
}

void guAlign(Mtx *m, float angle, float x, float y, float z)
{
    Matrix mf;

    guAlignF(mf, angle, x, y, z);
    guMtxF2L(mf, m);
}

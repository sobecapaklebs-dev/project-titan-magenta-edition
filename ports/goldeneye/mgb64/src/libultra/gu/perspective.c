/*
 * Clean-room libultra-style perspective projection helpers for the matching
 * target.
 */

#include "guint.h"
#include <ultratypes.h>

#define GU_DEG_TO_RAD (3.14159265358979323846f / 180.0f)

static void guScaleMatrix(float mf[4][4], float scale)
{
    int row;
    int col;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            mf[row][col] *= scale;
        }
    }
}

void guPerspectiveF(float mf[4][4], u16 *perspNorm, float fovy, float aspect, float near, float far, float scale)
{
    float half_angle;
    float cotangent;
    float depth;

    half_angle = fovy * (GU_DEG_TO_RAD * 0.5f);
    cotangent = cosf(half_angle) / sinf(half_angle);
    depth = near - far;

    guMtxIdentF(mf);
    mf[0][0] = cotangent / aspect;
    mf[1][1] = cotangent;
    mf[2][2] = (near + far) / depth;
    mf[2][3] = -1.0f;
    mf[3][2] = (2.0f * near * far) / depth;
    mf[3][3] = 0.0f;

    guScaleMatrix(mf, scale);

    if (perspNorm != (u16 *)NULL) {
        if (near + far <= 2.0f) {
            *perspNorm = (u16)0xffff;
        } else {
            *perspNorm = (u16)((2.0f * 65536.0f) / (near + far));
            if (*perspNorm == 0) {
                *perspNorm = (u16)1;
            }
        }
    }
}

void guPerspective(Mtx *m, u16 *perspNorm, float fovy, float aspect, float near, float far, float scale)
{
    Matrix mf;

    guPerspectiveF(mf, perspNorm, fovy, aspect, near, far, scale);
    guMtxF2L(mf, m);
}

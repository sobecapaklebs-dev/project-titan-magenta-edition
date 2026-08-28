/*
 * Clean-room libultra-style orthographic projection helpers for the matching
 * target.
 */

#include "guint.h"

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

void guOrthoF(float mf[4][4], float l, float r, float b, float t, float n, float f, float scale)
{
    float width;
    float height;
    float depth;

    width = r - l;
    height = t - b;
    depth = f - n;

    guMtxIdentF(mf);
    mf[0][0] = 2.0f / width;
    mf[1][1] = 2.0f / height;
    mf[2][2] = -2.0f / depth;
    mf[3][0] = -(r + l) / width;
    mf[3][1] = -(t + b) / height;
    mf[3][2] = -(f + n) / depth;

    guScaleMatrix(mf, scale);
}

void guOrtho(Mtx *m, float l, float r, float b, float t, float n, float f, float scale)
{
    Matrix mf;

    guOrthoF(mf, l, r, b, t, n, f, scale);
    guMtxF2L(mf, m);
}

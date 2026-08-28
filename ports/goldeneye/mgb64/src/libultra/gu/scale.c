/*
 * Clean-room libultra-style scale matrix helpers for the matching target.
 */

#include "guint.h"

void guScaleF(float mf[4][4], float x, float y, float z)
{
    guMtxIdentF(mf);

    mf[0][0] = x;
    mf[1][1] = y;
    mf[2][2] = z;
}

void guScale(Mtx *m, float x, float y, float z)
{
    Matrix mf;

    guScaleF(mf, x, y, z);
    guMtxF2L(mf, m);
}

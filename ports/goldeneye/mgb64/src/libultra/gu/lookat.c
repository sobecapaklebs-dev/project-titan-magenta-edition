/*
 * Clean-room libultra-style look-at matrix helpers for the matching target.
 */

#include "guint.h"

static void guNormalizeVector(float *x, float *y, float *z)
{
    float length_squared;
    float inv_length;

    length_squared = (*x * *x) + (*y * *y) + (*z * *z);
    if (length_squared <= 0.0f) {
        return;
    }

    inv_length = 1.0f / sqrtf(length_squared);
    *x *= inv_length;
    *y *= inv_length;
    *z *= inv_length;
}

void guLookAtF(float mf[4][4], float xEye, float yEye, float zEye,
               float xAt, float yAt, float zAt,
               float xUp, float yUp, float zUp)
{
    float xLook;
    float yLook;
    float zLook;
    float xRight;
    float yRight;
    float zRight;

    xLook = xEye - xAt;
    yLook = yEye - yAt;
    zLook = zEye - zAt;
    guNormalizeVector(&xLook, &yLook, &zLook);

    xRight = yUp * zLook - zUp * yLook;
    yRight = zUp * xLook - xUp * zLook;
    zRight = xUp * yLook - yUp * xLook;
    guNormalizeVector(&xRight, &yRight, &zRight);

    xUp = yLook * zRight - zLook * yRight;
    yUp = zLook * xRight - xLook * zRight;
    zUp = xLook * yRight - yLook * xRight;
    guNormalizeVector(&xUp, &yUp, &zUp);

    guMtxIdentF(mf);

    mf[0][0] = xRight;
    mf[1][0] = yRight;
    mf[2][0] = zRight;
    mf[3][0] = -(xEye * xRight + yEye * yRight + zEye * zRight);

    mf[0][1] = xUp;
    mf[1][1] = yUp;
    mf[2][1] = zUp;
    mf[3][1] = -(xEye * xUp + yEye * yUp + zEye * zUp);

    mf[0][2] = xLook;
    mf[1][2] = yLook;
    mf[2][2] = zLook;
    mf[3][2] = -(xEye * xLook + yEye * yLook + zEye * zLook);
}

void guLookAt(Mtx *m, float xEye, float yEye, float zEye,
              float xAt, float yAt, float zAt,
              float xUp, float yUp, float zUp)
{
    Matrix mf;

    guLookAtF(mf, xEye, yEye, zEye, xAt, yAt, zAt, xUp, yUp, zUp);
    guMtxF2L(mf, m);
}

/*
 * Clean-room libultra-style vector normalization helper for the matching
 * target. A zero-length vector is left unchanged.
 */

#include "guint.h"

void guNormalize(float *x, float *y, float *z)
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

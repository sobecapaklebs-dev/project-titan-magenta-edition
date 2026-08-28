#ifndef LIBULTRA_GU_GUINT_H
#define LIBULTRA_GU_GUINT_H

/*
 * Clean-room internal declarations shared by the matching-target GU utility
 * sources.
 */

#include "mbi.h"
#include "gu.h"

typedef union du {
    struct {
        unsigned int hi;
        unsigned int lo;
    } word;
    double d;
} du;

typedef union fu {
    unsigned int i;
    float f;
} fu;

#ifndef __GL_GL_H__
typedef float Matrix[4][4];
#endif

#ifndef ROUND
#define ROUND(d) (int)(((d) >= 0.0) ? ((d) + 0.5) : ((d) - 0.5))
#endif
#ifndef ABS
#define ABS(d) (((d) > 0) ? (d) : -(d))
#endif

extern float __libm_qnan_f;
signed short sins(unsigned short x);
signed short coss(unsigned short x);

#endif

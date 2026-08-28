/*
 * Clean-room single-precision cosine approximation for the matching target.
 */

/* WEB-056 (dormant): as in sinf.c — this TU is not compiled into any build
 * target, so sinf/cosf already resolve to libm/musl with no game-side shadow to
 * break. The guard only keeps the source consistent with the <math.h> rename
 * pattern if libultra/gu is ever restored to the build. Native unchanged. */
#ifdef __EMSCRIPTEN__
#define sinf ge007_sinf
#define cosf ge007_cosf
#endif

#include "guint.h"

#define GU_HALF_PI 1.57079632679489661923f

float cosf(float angle)
{
    return sinf(angle + GU_HALF_PI);
}

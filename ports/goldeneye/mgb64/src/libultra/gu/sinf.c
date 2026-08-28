/*
 * Clean-room single-precision sine approximation for the matching target.
 */

/* WEB-056 (dormant). This TU is NOT compiled into any build target today
 * (src/libultra/gu is absent from CMakeLists.txt), so the engine's sinf/cosf
 * already resolve to libm/musl on both native and web — `nm` shows them
 * UNDEFINED in ge007/ge007_web — and there is no game-side symbol shadowing
 * SDL's internal libm to break. This guard therefore does nothing in the
 * current build; it only keeps the source self-consistent with the
 * <math.h> rename pattern IF libultra/gu is ever wired back in to restore the
 * N64 sinf/cosf. To make that faithful math actually reach the game on wasm you
 * must ALSO add the matching `#define sinf/cosf` to a caller-reaching header
 * (as <math.h> does for acosf/asinf/atan2f) — renaming only the definition
 * would leave the N64 copy unused, not incorrect. Native never defines
 * __EMSCRIPTEN__. */
#ifdef __EMSCRIPTEN__
#define sinf ge007_sinf
#define cosf ge007_cosf
#endif

#include "guint.h"

#define GU_PI        3.14159265358979323846
#define GU_HALF_PI   (GU_PI * 0.5)
#define GU_TWO_PI    (GU_PI * 2.0)

static double guReduceRadians(double angle)
{
    int turns;

    if (angle != angle) {
        return angle;
    }

    if (angle > 268435456.0 || angle < -268435456.0) {
        return 0.0;
    }

    turns = (int)(angle / GU_TWO_PI);
    angle -= (double)turns * GU_TWO_PI;

    while (angle > GU_PI) {
        angle -= GU_TWO_PI;
    }
    while (angle < -GU_PI) {
        angle += GU_TWO_PI;
    }

    return angle;
}

float sinf(float angle)
{
    double x;
    double x2;
    double polynomial;

    x = guReduceRadians((double)angle);
    if (x != x) {
        return angle;
    }

    if (x > GU_HALF_PI) {
        x = GU_PI - x;
    } else if (x < -GU_HALF_PI) {
        x = -GU_PI - x;
    }

    x2 = x * x;
    polynomial = 1.0
        + x2 * (-1.0 / 6.0
        + x2 * (1.0 / 120.0
        + x2 * (-1.0 / 5040.0
        + x2 * (1.0 / 362880.0
        + x2 * (-1.0 / 39916800.0
        + x2 * (1.0 / 6227020800.0))))));

    return (float)(x * polynomial);
}

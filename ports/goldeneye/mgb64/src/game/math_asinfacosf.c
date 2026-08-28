#include <ultra64.h>
/* <ultra64.h> -> <math.h> and math_asinfacosf.h both carry the WEB-056
 * __EMSCRIPTEN__ rename, so the acosf/asinf definitions below are emitted as
 * ge007_acosf/ge007_asinf on wasm (matching every caller) and unchanged on
 * native. See <math.h> for the rationale. */
#include "math_asinacos.h"
#include "math_asinfacosf.h"

f32 acosf(f32 cosinef)
{
    s16 cosines;

    if (1.0f <= cosinef) {
        cosines = 0x7FFF;
    } else if (cosinef <= -1.0f) {
        cosines = -0x7FFF;
    } else {
        cosines = (cosinef * 32767.0f);
    }

    return (acos(cosines) * M_PI_F) / 65535.0f;
}

f32 asinf(f32 sinef)
{
    s16 sines;

    if (1.0f <= sinef) {
        sines = 0x7FFF;
    } else if (sinef <= -1.0f) {
        sines = -0x7FFF;
    } else {
        sines = (sinef * 32767.0f);
    }

    return (asin(sines) * M_PI_F) / 65535.0f;
}

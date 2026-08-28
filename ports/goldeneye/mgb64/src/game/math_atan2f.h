#ifndef _MATH_ATAN2F_H_
#define _MATH_ATAN2F_H_

#include <ultra64.h>

/* WEB-056: carry the wasm-only rename structurally (see <math.h> for the full
 * rationale) so a declaration of this game-defined atan2f can never be obtained
 * without the matching rename, regardless of include order. Same token as the
 * master block in <math.h>; native (no __EMSCRIPTEN__) is unchanged. */
#ifdef __EMSCRIPTEN__
#define atan2f ge007_atan2f
#endif

f32 atan2f(f32 y, f32 x);

#endif

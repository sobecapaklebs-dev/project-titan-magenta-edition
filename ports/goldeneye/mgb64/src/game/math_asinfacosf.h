#ifndef _MATH_ASINFACOSF_H_
#define _MATH_ASINFACOSF_H_

#include <ultra64.h>

/* WEB-056: carry the wasm-only rename structurally, so a declaration of these
 * game-defined float functions can never be obtained without the matching
 * rename even if this header is ever included before <math.h>. Same tokens as
 * the master block in <math.h> (a same-token #define is a benign redefinition);
 * see that header for the full rationale. Native (no __EMSCRIPTEN__) unchanged. */
#ifdef __EMSCRIPTEN__
#define acosf ge007_acosf
#define asinf ge007_asinf
#endif

f32 acosf(f32 cosinef);
f32 asinf(f32 sinef);

#endif

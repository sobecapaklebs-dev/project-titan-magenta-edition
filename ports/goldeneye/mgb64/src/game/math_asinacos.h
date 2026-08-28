#ifndef _MATH_ASINACOS_H_
#define _MATH_ASINACOS_H_

#include <ultra64.h>

/* GoldenEye's fixed-point acos/asin (s16 angle -> u16/s16 sin table index)
 * deliberately reuse the libm names. On native this is fine: the game and
 * SDL/libm are separate dynamically-linked images, so each binds its own
 * `acos`/`asin`. Under wasm everything statically links into ONE module, so
 * the game's (i32->i32) fixed-point definition collides with SDL's libm
 * (f64->f64) — a hard function-signature mismatch. Rename the game's symbols
 * for the wasm build only; native keeps the retail names (and the shim
 * math.h, which deliberately omits acos/asin, stays untouched). This macro
 * must be seen by BOTH the definition TU (math_asinacos.c) and every caller,
 * so all include this header. */
#ifdef __EMSCRIPTEN__
#define acos ge007_fixed_acos
#define asin ge007_fixed_asin
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wincompatible-library-redeclaration"
#endif
u16 acos(s16 arg0);
s16 asin(s16 arg0);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif

#ifndef _MATH_EXT_H_
#define _MATH_EXT_H_

/* wasm libm de-shadowing (WEB-056). The port defines a few libm float entry
 * points itself as faithful N64-precision approximations: acosf/asinf
 * (math_asinfacosf.c, routed through the game's s16 acos/asin table) and atan2f
 * (math_atan2f.c, the retail formulation). On native this is harmless — the
 * engine and SDL/libm are separate dynamically-linked images, so each binds its
 * own acosf/asinf/atan2f. Under wasm EVERYTHING statically links into ONE
 * module, and these strong game symbols then also satisfy SDL's *internal* libm
 * references: SDL's audio resamplers and controller math would silently swap
 * musl's implementations for the N64 table approximations. Rename the game's
 * copies to ge007_-prefixed symbols for the wasm build only. Game translation
 * units keep calling the faithful N64 versions under the new names, while SDL
 * and musl resolve plain acosf/asinf/atan2f to musl's own (more correct for
 * SDL); the game's own call graph is unchanged, only the symbol name.
 *
 * This macro must be visible to BOTH each definition TU (math_asinfacosf.c,
 * math_atan2f.c) and every caller, so it lives in the one header the entire
 * engine funnels through: <ultra64.h> includes <math.h> (this file), so any TU
 * that includes either — which every acosf/asinf/atan2f caller does, always
 * before the narrower math_asinfacosf.h / math_atan2f.h — carries the rename.
 * Native never defines __EMSCRIPTEN__, so the preprocessor path (and thus every
 * native object's symbol table) is byte-identical to before.
 *
 * NOT renamed here: sinf/cosf. Unlike the three above, the port does NOT compile
 * a game-side sinf/cosf — src/libultra/gu/sinf.c and cosf.c exist but are never
 * added to any build target, so `nm` shows them UNDEFINED in the engine and they
 * already resolve to libm/musl on both native and web. There is no shadow to
 * break, and renaming their callers would only leave ge007_sinf/ge007_cosf
 * undefined at link. See sinf.c / cosf.c for the dormant (arms only if that
 * source is ever wired into the build) companion guard. */
#ifdef __EMSCRIPTEN__
#define acosf  ge007_acosf
#define asinf  ge007_asinf
#define atan2f ge007_atan2f
#endif

#ifdef NATIVE_PORT
float sqrtf(float);
double sqrt(double);
float sinf(float);
double sin(double);
float cosf(float);
double cos(double);
float fabsf(float);
double fabs(double);
float floorf(float);
double floor(double);
float ceilf(float);
double ceil(double);
/* Final-review follow-up: platform_sdl.c's web mouse-unscale calls lroundf;
 * this shadow header wins the include path there, so declare it (previously
 * only clang's builtin recovery supplied the correct `long (float)` type). */
long lroundf(float);
float atan2f(float, float);
float acosf(float);
float asinf(float);
float tanf(float);
float powf(float, float);
double pow(double, double);
float fmodf(float, float);
float logf(float);
double log(double);
long lround(double);
#else
#define M_E 2.7182818284590452354
#define M_LOG2E 1.4426950408889634074
#define M_LOG10E 0.43429448190325182765
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962
#define M_1_PI 0.31830988618379067154
#define M_2_PI 0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2 1.41421356237309504880
#define M_SQRT1_2 0.70710678118654752440
#define FLT_EPSILON 1.19209290E-07F

float sqrtf(float);
double sqrt(double);
float sinf(float);
double sin(double);
float cosf(float);
double cos(double);
#endif

/* Standard <math.h> macros. This shadow header REPLACES the system <math.h> (it
 * declares its own libm prototypes instead of #include-ing it), so it must also
 * supply the standard macros that framework/library headers expect when THEY
 * pull in <math.h> and this shadow wins the search path — notably macOS Metal's
 * MTLAccelerationStructureTypes.h references INFINITY under the Xcode 15.4 SDK,
 * which broke the ge007_lib universal build (gfx_metal.mm). Guarded, so any
 * system/earlier definition wins; the game references none of these (grep: 0
 * INFINITY/NAN/HUGE_VAL uses in src/), so every game/decomp TU is byte-identical. */
#ifndef INFINITY
#define INFINITY  (__builtin_inff())
#endif
#ifndef NAN
#define NAN       (__builtin_nanf(""))
#endif
#ifndef HUGE_VALF
#define HUGE_VALF (__builtin_huge_valf())
#endif
#ifndef HUGE_VAL
#define HUGE_VAL  (__builtin_huge_val())
#endif

#ifndef M_PI_F
#define M_PI_F 3.1415927f
#endif
#define M_MINUS_PI_F -3.1415927f

#ifndef M_TAU
#define M_TAU 6.28318530717958647692
#endif
#ifndef M_TAU_F
#define M_TAU_F 6.2831855f
#endif

#define M_HALF_PI (M_PI_F / 2)
#define M_THREE_HALF_PI (3 * M_HALF_PI)

#define M_U16_MAX_VALUE_F 65536.0f
#define M_U32_MAX_VALUE_F 4294967296.0f

#define SECS_TO_TIMER60(SECS) ((SECS) * GAME_TICKRATE)
#define MINS_TO_TIMER60(MINS) (SECS_TO_TIMER60((MINS) * GAME_TICKRATE))
#define DEG2BYTE(DEG) (char)(256.0f / 360.0f * (DEG))
#define RAD2BYTE(RAD) (char)(256.0f / M_TAU_F * (RAD))
#define DegToRad(DEG) (float)((DEG) * M_TAU_F / 360.0f)
#define DegToRad1Fact(DEG) (float)((DEG) * (float)(M_TAU / 360.0))
#define mDegToHalfRad(x) (((x) * M_PI_F) / 360.0f)
#define RadToDeg(RAD) (float)((RAD) * (360.0f / M_TAU_F))
#define ByteToRadian(Byte) (((Byte) * M_TAU_F) * (1.0f / 256.0f))

#ifndef ABS
#define ABS(x) ((x) < 0 ? -(x) : (x))
#endif
#ifndef SGN
#define SGN(x) ((x) < 0 ? -1 : (x) > 0 ? 1 : 0)
#endif
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif
#ifndef SQR
#define SQR(x) ((x) * (x))
#endif

#define IDO_POINT_ONE 0.10000001f

#endif

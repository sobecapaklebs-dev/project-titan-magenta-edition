#ifndef _ULTRA64_TYPES_H_
#define _ULTRA64_TYPES_H_

/*
 * Clean-room compatibility aliases for the integer and floating-point types
 * used by the original N64 interfaces. Keep this header limited to primitive
 * type names and simple constants so both native and matching-target code can
 * include it without pulling in broader SDK declarations.
 */

#if defined(_LANGUAGE_C) || defined(_LANGUAGE_C_PLUS_PLUS)

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef signed char s8;
typedef short s16;
typedef int s32;
typedef long long s64;

typedef volatile unsigned char vu8;
typedef volatile unsigned short vu16;
typedef volatile unsigned int vu32;
typedef volatile unsigned long long vu64;

typedef volatile signed char vs8;
typedef volatile short vs16;
typedef volatile int vs32;
typedef volatile long long vs64;

typedef float f32;
typedef double f64;

#endif /* _LANGUAGE_C || _LANGUAGE_C_PLUS_PLUS */

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifdef TARGET_N64
typedef u32 size_t;
typedef s32 ssize_t;
typedef u32 uintptr_t;
typedef s32 intptr_t;
typedef s32 ptrdiff_t;
#else
#include <stddef.h>
#include <stdint.h>
typedef ptrdiff_t ssize_t;
#endif

#endif /* _ULTRA64_TYPES_H_ */

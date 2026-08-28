#ifndef _ULTRA64_H_
#define _ULTRA64_H_

#include <math.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif

#include <PR/ultratypes.h>

#ifdef NATIVE_PORT
// PC native port: use platform abstraction layer
#include <platform.h>
#include <PR/libaudio.h>
#else
// N64: use original SDK headers
#include <PR/os.h>
#include <PR/gu.h>
#include <PR/mbi.h>
#include <PR/libaudio.h>
#include <PR/sptask.h>
#include <PR/ucode.h>
#endif

#endif

// GLOBAL_ASM is a no-op for non-matching / port builds
#if defined(NONMATCHING) || defined(__INTELLISENSE__)
#define GLOBAL_ASM(...)
#endif

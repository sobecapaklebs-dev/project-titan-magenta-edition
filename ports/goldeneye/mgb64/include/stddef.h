#ifdef NATIVE_PORT
#include_next <stddef.h>
#else
#ifndef MGB64_STDDEF_H
#define MGB64_STDDEF_H

#include <sgidefs.h>

#if (_MIPS_SZPTR == 64)
typedef __int64_t ptrdiff_t;
#else
typedef int ptrdiff_t;
#endif

#if !defined(_SIZE_T) && !defined(_SIZE_T_)
#define _SIZE_T
#if (_MIPS_SZLONG == 64)
typedef unsigned long size_t;
#else
typedef unsigned int size_t;
#endif
#endif

#ifndef _WCHAR_T
#define _WCHAR_T
#if (_MIPS_SZLONG == 64)
typedef __int32_t wchar_t;
#else
typedef long wchar_t;
#endif
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(TYPE, MEMBER) ((size_t)&(((TYPE *)0)->MEMBER))

#endif /* MGB64_STDDEF_H */

#endif

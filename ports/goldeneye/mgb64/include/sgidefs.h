#ifndef MGB64_SGIDEFS_H
#define MGB64_SGIDEFS_H

#define _MIPS_ISA_MIPS1 1
#define _MIPS_ISA_MIPS2 2
#define _MIPS_ISA_MIPS3 3
#define _MIPS_ISA_MIPS4 4

#define _MIPS_SIM_ABI32 1
#define _MIPS_SIM_NABI32 2
#define _MIPS_SIM_ABI64 3

#ifndef _MIPS_FPSET
#define _MIPS_FPSET 16
#endif

#ifndef _MIPS_ISA
#define _MIPS_ISA _MIPS_ISA_MIPS2
#endif

#ifndef _MIPS_SIM
#define _MIPS_SIM _MIPS_SIM_ABI32
#endif

#ifndef _MIPS_SZINT
#define _MIPS_SZINT 32
#endif

#ifndef _MIPS_SZLONG
#define _MIPS_SZLONG 32
#endif

#ifndef _MIPS_SZPTR
#define _MIPS_SZPTR 32
#endif

#if defined(_LANGUAGE_C) || defined(_LANGUAGE_C_PLUS_PLUS)

typedef int __int32_t;
typedef unsigned int __uint32_t;

#if (_MIPS_SZLONG == 64)
typedef long __int64_t;
typedef unsigned long __uint64_t;
#elif defined(_LONGLONG)
typedef long long __int64_t;
typedef unsigned long long __uint64_t;
#else
typedef struct {
    int hi32;
    int lo32;
} __int64_t;
typedef struct {
    unsigned int hi32;
    unsigned int lo32;
} __uint64_t;
#endif

#if (_MIPS_SZPTR == 64)
typedef __int64_t __psint_t;
typedef __uint64_t __psunsigned_t;
#else
typedef __int32_t __psint_t;
typedef __uint32_t __psunsigned_t;
#endif

#if (_MIPS_SZPTR == 64) || (_MIPS_SZLONG == 64) || (_MIPS_SZINT == 64)
typedef __int64_t __scint_t;
typedef __uint64_t __scunsigned_t;
#else
typedef __int32_t __scint_t;
typedef __uint32_t __scunsigned_t;
#endif

#endif

#endif

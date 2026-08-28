#ifdef NATIVE_PORT
#include_next <stdlib.h>
#else
#ifndef MGB64_STDLIB_H
#define MGB64_STDLIB_H

#include <stddef.h>
#include <sgidefs.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0
#define RAND_MAX 32767

typedef struct {
    int quot;
    int rem;
} div_t;

typedef struct {
    long quot;
    long rem;
} ldiv_t;

#ifdef _LONGLONG
typedef struct {
    long long quot;
    long long rem;
} lldiv_t;
#endif

#ifndef _SSIZE_T
#define _SSIZE_T
#if (_MIPS_SZLONG == 64)
typedef long ssize_t;
#else
typedef int ssize_t;
#endif
#endif

extern int atoi(const char *str);
extern long atol(const char *str);
extern int rand(void);
extern void srand(unsigned int seed);

extern void *calloc(size_t count, size_t size);
extern void free(void *ptr);
extern void *malloc(size_t size);
extern void *realloc(void *ptr, size_t size);

extern void abort(void);
extern int atexit(void (*func)(void));
extern void exit(int status);
extern char *getenv(const char *name);

extern int abs(int value);
extern long labs(long value);
extern div_t div(int numer, int denom);
extern ldiv_t ldiv(long numer, long denom);
#ifdef _LONGLONG
extern long long llabs(long long value);
extern lldiv_t lldiv(long long numer, long long denom);
#endif /* MGB64_STDLIB_H */

#endif

#endif

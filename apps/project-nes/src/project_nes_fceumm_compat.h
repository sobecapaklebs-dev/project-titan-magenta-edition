#ifndef PROJECT_NES_FCEUMM_COMPAT_H
#define PROJECT_NES_FCEUMM_COMPAT_H

#include <stddef.h>
#include <inttypes.h>

/*
 * PS2SDK's EE inttypes.h does not provide the C99 printf format macros that
 * libretro-common expects.  Keep the compatibility in Titan's adapter layer
 * instead of modifying the frozen FCEUmm/libretro source.
 *
 * R5900 EE ABI used by PS2DEV:
 *   int64_t/uint64_t -> long long / unsigned long long
 *   uintptr_t        -> 32-bit unsigned integer
 */
#if defined(_EE)
#ifndef PRId64
#define PRId64 "lld"
#endif
#ifndef PRIu64
#define PRIu64 "llu"
#endif
#ifndef PRIuPTR
#define PRIuPTR "u"
#endif
#endif

void *pn_fceumm_malloc(size_t size);
void *pn_fceumm_calloc(size_t count, size_t size);
void *pn_fceumm_realloc(void *ptr, size_t size);
void pn_fceumm_free(void *ptr);

#endif

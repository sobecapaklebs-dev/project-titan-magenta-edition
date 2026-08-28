/**
 * byteswap.h — Byte-swapping utilities for big-endian ROM data on little-endian PC.
 *
 * N64 is big-endian (MIPS). All data structures loaded from the ROM
 * are in big-endian byte order. On little-endian PC, multi-byte fields
 * must be byte-swapped after loading.
 */
#ifndef _PLATFORM_BYTESWAP_H_
#define _PLATFORM_BYTESWAP_H_

#include <stdint.h>

#ifdef NATIVE_PORT

static inline uint32_t bswap32(uint32_t x) {
    return __builtin_bswap32(x);
}

static inline uint16_t bswap16(uint16_t x) {
    return __builtin_bswap16(x);
}

/* Read a big-endian 32-bit value from a memory address */
static inline int32_t read_be32(const void *p) {
    uint32_t v;
    __builtin_memcpy(&v, p, 4);
    return (int32_t)bswap32(v);
}

/* Read a big-endian 16-bit value from a memory address */
static inline int16_t read_be16(const void *p) {
    uint16_t v;
    __builtin_memcpy(&v, p, 2);
    return (int16_t)bswap16(v);
}

/* Read a big-endian 32-bit unsigned value */
static inline uint32_t read_be32u(const void *p) {
    uint32_t v;
    __builtin_memcpy(&v, p, 4);
    return bswap32(v);
}

/* ===== Type-safe swap macros (C11 _Generic) =====
 *
 * GE_SWAPPED(x)  — return byte-swapped copy of x (big-endian → host)
 * GE_SWAP(x)     — in-place: x = GE_SWAPPED(x)
 *
 * Uses _Generic to dispatch to the correct width automatically.
 * Catches type mismatches at compile time instead of silent truncation. */

static inline int32_t bswap_s32(int32_t x) {
    return (int32_t)__builtin_bswap32((uint32_t)x);
}

static inline int16_t bswap_s16(int16_t x) {
    return (int16_t)__builtin_bswap16((uint16_t)x);
}

static inline float bswap_f32(float x) {
    uint32_t v;
    __builtin_memcpy(&v, &x, 4);
    v = __builtin_bswap32(v);
    __builtin_memcpy(&x, &v, 4);
    return x;
}

#define GE_SWAPPED(x) _Generic((x),                    \
    uint32_t: bswap32,                                  \
    int32_t:  bswap_s32,                                \
    uint16_t: bswap16,                                  \
    int16_t:  bswap_s16,                                \
    float:    bswap_f32                                  \
)(x)

#define GE_SWAP(x)  ((x) = GE_SWAPPED(x))

#else
/* On N64 (big-endian), no swapping needed */
#define read_be32(p) (*(const int32_t *)(p))
#define read_be16(p) (*(const int16_t *)(p))
#define read_be32u(p) (*(const uint32_t *)(p))
#define GE_SWAPPED(x) (x)
#define GE_SWAP(x)    ((void)0)
#endif

#endif /* _PLATFORM_BYTESWAP_H_ */

/*
 * Tiny compiler-runtime compatibility for the PS2 IOP freestanding IRX.
 *
 * The IOP build links with -nostdlib. GCC may lower variable 64-bit logical
 * right shifts on MIPS32 to __lshrdi3. Pulling the whole libgcc into the IRX
 * is unnecessary and makes the standalone driver's runtime dependencies less
 * explicit, so provide the one helper the generated code requires.
 *
 * This implementation uses only 32-bit operations and assumes the PS2 IOP's
 * little-endian O32 ABI, where the low 32-bit word precedes the high word.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <types.h>

typedef union TitanU64Words {
    unsigned long long value;
    struct {
        u32 lo;
        u32 hi;
    } word;
} TitanU64Words;

unsigned long long __lshrdi3(unsigned long long value, int shift)
{
    TitanU64Words in, out;
    unsigned int s;

    in.value = value;
    out.word.lo = 0;
    out.word.hi = 0;

    if (shift <= 0)
        return value;
    if (shift >= 64)
        return out.value;

    s = (unsigned int)shift;
    if (s >= 32) {
        out.word.lo = in.word.hi >> (s - 32);
    } else {
        out.word.lo = (in.word.lo >> s) | (in.word.hi << (32 - s));
        out.word.hi = in.word.hi >> s;
    }

    return out.value;
}

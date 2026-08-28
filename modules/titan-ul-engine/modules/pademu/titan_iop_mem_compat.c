/*
 * IOP compiler-libcall compatibility for PADEMU BLE.
 *
 * GCC may lower aggregate copies/zeroing to memcpy/memset even with
 * -fno-builtin.  PADEMU deliberately uses SMSUTILS for memory primitives and
 * therefore does not import sysclib memcpy/memset when USE_SMSUTILS is set.
 * Export local libc-shaped shims so compiler-generated calls resolve inside
 * the IRX and are forwarded to the already-imported SMSUTILS routines.
 */
#include "sys_utils.h"

void *memcpy(void *dst, const void *src, unsigned n)
{
    mips_memcpy(dst, src, n);
    return dst;
}

void *memset(void *dst, int value, unsigned n)
{
    mips_memset(dst, value, n);
    return dst;
}

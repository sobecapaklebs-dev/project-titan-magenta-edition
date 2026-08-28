/*
 * setup_pnames.h — pure re-layout of the setup file's padnames / boundpadnames
 * tables (FID-0037).
 *
 * proplvreset2 (src/game/prop.c) re-lays-out the GoldenEye stage setup file for
 * 64-bit hosts because several N64 structs change size when pointers widen. Two of
 * the ten header tables were left permanently NULL (prop.c:2538, "TODO byte-swap
 * pname structs"): padnames (setup header word 8) and boundpadnames (word 9).
 *
 * On disk each is an array of big-endian 32-bit file-local offsets, terminated by
 * a zero entry; every non-zero offset points (relative to the file base) at a
 * NUL-terminated pad-name string. Retail resolves the header table then relocates
 * each entry's offset into a real pointer (prop.c:3849-3865 resolve the table,
 * 4000-4014 relocate each `.p`). Because N64 `struct pname` is 4 bytes but the
 * host union {char*; s32} is 8, the table cannot be cast in place — it must be
 * re-laid-out exactly like the pads / boundpads.
 *
 * These helpers are pure and ROM-free (they operate on a caller-supplied byte
 * image), so the byte-swap + relocation is unit-testable without a setup file.
 */
#ifndef SETUP_PNAMES_H
#define SETUP_PNAMES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Byte-swapped table offset from the 10-word big-endian setup header, word
 * `index` (8 = padnames, 9 = boundpadnames).
 *   legacy == 0 (faithful default): BE32(base + index*4).
 *   legacy != 0 (the port defect):  0  — reproduces prop.c:2538 leaving it NULL.
 * Returns 0 when base is NULL.
 */
uint32_t setupPnamesTableOffset(const uint8_t *base, int index, int legacy);

/*
 * Count the null-terminated 4-byte big-endian offset entries at
 * base + table_off (excludes the terminating zero entry). Returns 0 when
 * table_off == 0 or base is NULL. `limit` caps the scan (overflow guard).
 */
size_t setupPnamesCount(const uint8_t *base, uint32_t table_off, size_t limit);

/*
 * Resolve entry i to a host string pointer: base + BE32(base + table_off + 4*i),
 * or NULL when that entry's offset is 0 (or the table is absent). Caller
 * guarantees i < setupPnamesCount(...).
 */
const char *setupPnamesResolve(const uint8_t *base, uint32_t table_off, size_t i);

#ifdef __cplusplus
}
#endif

#endif /* SETUP_PNAMES_H */

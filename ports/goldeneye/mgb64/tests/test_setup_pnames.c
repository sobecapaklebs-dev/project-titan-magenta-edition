/*
 * test_setup_pnames.c — ROM-free regression lane for FID-0037.
 *
 * The setup file's padnames (header word 8) and boundpadnames (word 9) tables are
 * arrays of big-endian 32-bit file-local offsets, zero-terminated, each pointing
 * at a NUL-terminated pad-name string. The port left both NULL (prop.c:2538);
 * retail resolves the table and relocates each entry (prop.c:3849-3865 / 4000-4014).
 *
 * This pins the byte-swap (big-endian, not host/little-endian), the zero
 * terminator, the base+offset relocation, and the legacy negative control (table
 * NULL). It fails on revert if: the offset read stops byte-swapping, the header
 * word index shifts, the terminator handling breaks, or the
 * GE007_NO_PADNAMES_FIX legacy path stops nulling the table.
 */
#include "setup_pnames.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

/* Store a big-endian u32 into a byte image (mirrors the on-disk N64 layout). */
static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8);
    p[3] = (uint8_t)(v >>  0);
}

int main(void)
{
    /*
     * Synthetic setup image:
     *   header: 10 big-endian words. Word 8 = padnames table off = 0x30.
     *                                Word 9 = boundpadnames off  = 0x00 (absent).
     *   padnames table @0x30: three offsets [0x50, 0x60, 0x00 terminator].
     *   strings: @0x50 "AC",  @0x60 "DEF".
     */
    uint8_t img[0x80];
    memset(img, 0, sizeof(img));

    put_be32(img + 8 * 4, 0x30);   /* header word 8: padnames table */
    put_be32(img + 9 * 4, 0x00);   /* header word 9: boundpadnames absent */

    put_be32(img + 0x30, 0x50);    /* padnames[0] -> "AC" */
    put_be32(img + 0x34, 0x60);    /* padnames[1] -> "DEF" */
    put_be32(img + 0x38, 0x00);    /* padnames[2] terminator */

    memcpy(img + 0x50, "AC", 3);
    memcpy(img + 0x60, "DEF", 4);

    /* --- Faithful (legacy == 0) --- */
    {
        uint32_t off = setupPnamesTableOffset(img, 8, 0);
        size_t n;
        const char *s0, *s1;

        CHECK(off == 0x30, "faithful: padnames table offset byte-swapped from header word 8");

        n = setupPnamesCount(img, off, 1000);
        CHECK(n == 2, "faithful: two entries before the zero terminator");

        s0 = setupPnamesResolve(img, off, 0);
        s1 = setupPnamesResolve(img, off, 1);
        CHECK(s0 != NULL && strcmp(s0, "AC") == 0, "faithful: entry 0 relocates to 'AC'");
        CHECK(s1 != NULL && strcmp(s1, "DEF") == 0, "faithful: entry 1 relocates to 'DEF'");
        CHECK(s0 == (const char *)(img + 0x50), "faithful: entry 0 == base + offset");

        /* boundpadnames header word is 0 -> absent table (no entries). */
        CHECK(setupPnamesTableOffset(img, 9, 0) == 0, "faithful: boundpadnames header word 9 is 0");
        CHECK(setupPnamesCount(img, 0, 1000) == 0, "faithful: absent table has 0 entries");
    }

    /* --- Legacy negative control (GE007_NO_PADNAMES_FIX) --- */
    {
        uint32_t off = setupPnamesTableOffset(img, 8, 1);
        CHECK(off == 0, "legacy: padnames table forced to 0 (port defect: table NULL)");
        CHECK(setupPnamesCount(img, off, 1000) == 0, "legacy: no entries when table is NULL");
    }

    /* --- Fail-on-revert: faithful != legacy --- */
    CHECK(setupPnamesTableOffset(img, 8, 0) != setupPnamesTableOffset(img, 8, 1),
          "fix changes the resolved table offset (faithful != legacy)");
    CHECK(setupPnamesCount(img, setupPnamesTableOffset(img, 8, 0), 1000) !=
          setupPnamesCount(img, setupPnamesTableOffset(img, 8, 1), 1000),
          "fix changes the entry count (populated vs empty)");

    /* --- Byte-swap guard: a little-endian read would mis-resolve --- */
    {
        /* If the reader stopped byte-swapping, header word 8 (BE 0x00000030)
         * read little-endian would be 0x30000000 — far out of the image. The
         * faithful reader must yield exactly 0x30. */
        CHECK(setupPnamesTableOffset(img, 8, 0) == 0x30,
              "byte-swap: BE header read yields 0x30, not the LE 0x30000000");
    }

    if (g_failures == 0) {
        printf("PASS: setup_pnames\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}

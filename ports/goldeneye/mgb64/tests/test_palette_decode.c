/* IA16/RGBA16 TLUT decode lane (DAM_PARITY_DEEP_DIVE 2026-07-17 §4.7).
 *
 * Pins the N64 IA16 TLUT byte order in the run-dl CI import path: intensity is
 * the HIGH byte, alpha the LOW byte. The shipped decoder was byte-swapped, so
 * an opaque-dark entry (0x00FF: I=0, A=255) decoded as white-with-alpha-0 and
 * every IA16-palette CI texture (the Dam monitor text screens) collapsed to
 * flat vertex shade under G_CC_MODULATEIA. Reverting the byte order reddens
 * this test.
 */
#include <assert.h>
#include <stdio.h>

#include "../src/platform/fast3d/gfx_palette.h"

static int check(uint16_t entry, bool ia16,
                 uint8_t r, uint8_t g, uint8_t b, uint8_t a, const char *label)
{
    uint8_t out[4];
    gfx_palette_entry_to_rgba32(entry, ia16, out);
    if (out[0] != r || out[1] != g || out[2] != b || out[3] != a) {
        fprintf(stderr,
                "FAIL %s: entry=0x%04x ia16=%d -> got (%u,%u,%u,%u) want (%u,%u,%u,%u)\n",
                label, entry, (int)ia16, out[0], out[1], out[2], out[3], r, g, b, a);
        return 1;
    }
    return 0;
}

int main(void)
{
    int fails = 0;

    /* IA16: I in the HIGH byte, A in the LOW byte. */
    fails += check(0x00FF, true, 0, 0, 0, 255, "ia16 opaque black (monitor screen bg)");
    fails += check(0xFF00, true, 255, 255, 255, 0, "ia16 transparent white");
    fails += check(0xFFFF, true, 255, 255, 255, 255, "ia16 opaque white (monitor text)");
    fails += check(0x80FF, true, 128, 128, 128, 255, "ia16 opaque mid grey");
    fails += check(0x0000, true, 0, 0, 0, 0, "ia16 transparent black");

    /* RGBA16 (5551): r=11..15 g=6..10 b=1..5 a=0, 5->8 bit via (v<<3)|(v>>2). */
    fails += check(0xFFFF, false, 255, 255, 255, 255, "rgba16 opaque white");
    fails += check(0x0001, false, 0, 0, 0, 255, "rgba16 opaque black");
    fails += check(0x0000, false, 0, 0, 0, 0, "rgba16 transparent black");
    fails += check((uint16_t)((0x1F << 11) | 1), false, 255, 0, 0, 255, "rgba16 pure red");
    fails += check((uint16_t)((0x10 << 6) | 1), false, 0, 132, 0, 255, "rgba16 mid green");

    if (fails) {
        fprintf(stderr, "test_palette_decode: %d failure(s)\n", fails);
        return 1;
    }
    printf("test_palette_decode: OK\n");
    return 0;
}

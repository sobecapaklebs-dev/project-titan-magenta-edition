/*
 * spectrum_settile.h — SETTILE low-word for the Spectrum easter-egg screen DL.
 *
 * spectrum_draw_screen (src/game/spectrum.c) builds the ZX-Spectrum emulator
 * screen display list by hand. It emits two G_SETTILE commands: the palette-load
 * SETTILE (w0 = 0xF5000300) and the per-tile SETTILE (w0 = 0xF5100000) inside the
 * 3x4 loop. Both must set the SETTILE *tile* field (w1 bits 26..24) to
 * 7 = G_TX_LOADTILE, because the two loads that follow each SETTILE both name
 * tile 7: the palette LOADTLUT is 0xF0000000 / 0x0703C000 and the per-tile
 * LOADBLOCK is 0xF3000000 / 0x073FF200 (tile field = 7 in each).
 *
 * Retail ASM holds the SETTILE low word (w1) in $s1 as 0x07000000
 * (src/game/spectrum.c:1547 `lui $s1,0x700`) and stores it at both sites
 * (sw $s1 at 1550 for the palette SETTILE and at 1639 inside the loop). The port
 * C body transcribed it as 0x00070000 — a digit shift — which decodes to tile=0
 * plus stray cmt=1 (bit 18) and maskt=12 (bits 17..14): the SETTILE configures
 * tile 0 while the loads still target tile 7. FID-0107 (sibling of FID-0104).
 *
 * This helper is pure and ROM-free so the corrected constant is unit-testable.
 */
#ifndef SPECTRUM_SETTILE_H
#define SPECTRUM_SETTILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SETTILE low word (w1) for both Spectrum-screen SETTILE commands.
 *   legacy == 0 (faithful default): 0x07000000  — tile field 7 (G_TX_LOADTILE).
 *   legacy != 0 (the port defect):  0x00070000  — tile 0 + stray cmt/maskt.
 */
uint32_t spectrumSettileLoadTileW1(int legacy);

#ifdef __cplusplus
}
#endif

#endif /* SPECTRUM_SETTILE_H */

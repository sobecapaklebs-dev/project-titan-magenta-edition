/**
 * platform_gbi.h — GBI (Graphics Binary Interface) for the native port.
 *
 * Strategy: Include the original gbi.h directly. It already has
 * endianness and pointer-size awareness via IS_BIG_ENDIAN and IS_64_BIT
 * from platform_info.h. The GBI macros build display lists in memory
 * using the same Gfx struct layout - the GBI translator reads them at
 * render time.
 *
 * We also include gbi_extension.h for Rare's custom G_TRI4/G_SETTEX.
 */
#ifndef _PLATFORM_GBI_H_
#define _PLATFORM_GBI_H_

/* The original gbi.h needs these from sptask.h but we handle them in platform_os.h */
/* It also needs mbi.h which is the same file (mbi.h includes gbi.h) */

/* Include the original N64 GBI — it works on PC with the right defines */
#include <PR/mbi.h>

/* Include Rare's custom GBI extensions */
#include <gbi_extension.h>

/* Segment macros — on PC, segments are identity-mapped */
#undef SEGMENT_OFFSET
#define SEGMENT_OFFSET(seg)     0
/* Note: SPSEGMENT_MODEL_VTX is an enum in bondconstants.h, do not define as macro */

/* RSP ucode references — stubs for PC */
extern long long int rspbootTextStart[];
extern long long int rspbootTextEnd[];
extern long long int gsp3DTextStart[];
extern long long int gsp3DDataStart[];
extern long long int gsp3DDataEnd[];
extern long long int aspMainTextStart[];
extern long long int aspMainTextEnd[];
extern long long int aspMainDataStart[];
extern long long int aspMainDataEnd[];

/* ===== 64-bit pointer safety overrides ===== */
/*
 * Gwords.w1 is uintptr_t, so gDma1p already stores full 64-bit pointers.
 * We override key macros to use base GBI (non-F3DEX2) encoding so that
 * both game-built and ROM-baked display lists parse identically.
 * Base GBI uses gDma1p encoding:
 *   w0 = cmd(8) | param(8) | length(16)
 *   w1 = address (uintptr_t — full pointer preserved)
 */
#include "gfx_ptr.h"  /* for gfx_segment_table (used by gSPSegment) */

/* Override gSPSegment — store full pointer in shadow segment table */
#undef gSPSegment
#define gSPSegment(pkt, segment, base) {                         \
    Gfx *_g = (Gfx *)(pkt);                                     \
    _g->words.w0 = (_SHIFTL(G_MOVEWORD, 24, 8) |                \
                     _SHIFTL((segment)*4, 8, 16) |               \
                     _SHIFTL(G_MW_SEGMENT, 0, 8));               \
    _g->words.w1 = (uintptr_t)(base);              \
    gfx_segment_table[(segment)] = (uintptr_t)(base);            \
}

/* Override gSPVertex — store vertex pointer
 * Base GBI: w0 = VTX(8) | ((n-1)<<4|v0)(8) | sizeof(Vtx)*n(16) */
#undef gSPVertex
#define gSPVertex(pkt, v, n, v0) {                               \
    Gfx *_g = (Gfx *)(pkt);                                     \
    _g->words.w0 = (_SHIFTL(G_VTX, 24, 8) |                     \
                     _SHIFTL(((n)-1)<<4|(v0), 16, 8) |           \
                     _SHIFTL(sizeof(Vtx)*(n), 0, 16));           \
    _g->words.w1 = (uintptr_t)(v);                 \
    GFX_DL_REGISTER_PTR(_g->words.w1);                          \
}

/* Override gSPMatrix — store matrix pointer
 * GoldenEye uses the direct base-GBI param byte encoding in its hardcoded
 * display lists, so native-built DLs must match that representation. */
#undef gSPMatrix
#define gSPMatrix(pkt, m, param) {                               \
    Gfx *_g = (Gfx *)(pkt);                                     \
    uintptr_t _mptr = (uintptr_t)(m);                            \
    _g->words.w0 = (_SHIFTL(G_MTX, 24, 8) |                     \
                     _SHIFTL((param), 16, 8) |                   \
                     _SHIFTL(sizeof(Mtx), 0, 16));               \
    _g->words.w1 = _mptr;                                        \
    GFX_DL_REGISTER_PTR(_mptr);                                 \
}

/* Override gSPDisplayList — store sub-DL pointer
 * Base GBI: w0 = DL(8) | G_DL_PUSH(8) | 0(16) */
#undef gSPDisplayList
#define gSPDisplayList(pkt, dl) {                                \
    Gfx *_g = (Gfx *)(pkt);                                     \
    _g->words.w0 = (_SHIFTL(G_DL, 24, 8) |                      \
                     _SHIFTL(G_DL_PUSH, 16, 8));                 \
    _g->words.w1 = (uintptr_t)(dl);                \
    GFX_DL_REGISTER_PTR(_g->words.w1);                          \
}

/* Override gSPBranchList — store branch pointer
 * Base GBI: w0 = DL(8) | G_DL_NOPUSH(8) | 0(16) */
#undef gSPBranchList
#define gSPBranchList(pkt, dl) {                                 \
    Gfx *_g = (Gfx *)(pkt);                                     \
    _g->words.w0 = (_SHIFTL(G_DL, 24, 8) |                      \
                     _SHIFTL(G_DL_NOPUSH, 16, 8));               \
    _g->words.w1 = (uintptr_t)(dl);                \
    GFX_DL_REGISTER_PTR(_g->words.w1);                          \
}

/* Override gDPSetTextureImage — store texture address */
#undef gDPSetTextureImage
#define gDPSetTextureImage(pkt, fmt, siz, width, img) {          \
    Gfx *_g = (Gfx *)(pkt);                                     \
    _g->words.w0 = (_SHIFTL(G_SETTIMG, 24, 8) |                 \
                     _SHIFTL(fmt, 21, 3) |                       \
                     _SHIFTL(siz, 19, 2) |                       \
                     _SHIFTL((width)-1, 0, 12));                 \
    _g->words.w1 = (uintptr_t)(img);               \
    GFX_DL_REGISTER_PTR(_g->words.w1);                          \
}

/* Override gSPViewport — store viewport pointer
 * Base GBI: w0 = MOVEMEM(8) | G_MV_VIEWPORT(8) | sizeof(Vp)(16) */
#undef gSPViewport
#define gSPViewport(pkt, v) {                                    \
    Gfx *_g = (Gfx *)(pkt);                                     \
    _g->words.w0 = (_SHIFTL(G_MOVEMEM, 24, 8) |                 \
                     _SHIFTL(G_MV_VIEWPORT, 16, 8) |             \
                     _SHIFTL(sizeof(Vp), 0, 16));                \
    _g->words.w1 = (uintptr_t)(v);                              \
    GFX_DL_REGISTER_PTR(_g->words.w1);                          \
}

/* P10.35 — ILP32 MOVEMEM host-pointer correctness.
 *
 * The base gbi.h gSPLight/gSPLookAt* macros write their source pointer
 * directly into Gfx.words.w1 through gDma1p/gDma2p.  On a 32-bit target,
 * a raw host pointer is indistinguishable from an N64 segmented token unless
 * it is recorded in gfx_ptr's registry.  platform_gbi.h already did this for
 * VTX/MTX/DL/texture-image/viewport, but LIGHT and LOOKAT were omitted.
 *
 * P10.34 isolated the hardware freeze to:
 *   gSPNumLights -> gSPLight -> G_MOVEMEM -> base-light memcpy
 * where seg_addr() had resolved an unregistered w1 and the source became an
 * invalid/unclassified address.  Preserve the exact GBI encoding and add the
 * missing pointer registration.  This is a no-op on LP64.
 */
#undef gSPLight
#ifdef F3DEX_GBI_2
#define gSPLight(pkt, l, n) {                                    \
    Gfx *_g = (Gfx *)(pkt);                                      \
    uintptr_t _lptr = (uintptr_t)(l);                            \
    _g->words.w0 = (_SHIFTL(G_MOVEMEM, 24, 8) |                 \
                     _SHIFTL(((sizeof(Light))-1)/8, 19, 5) |     \
                     _SHIFTL((((n)*24+24)/8), 8, 8) |           \
                     _SHIFTL(G_MV_LIGHT, 0, 8));                \
    _g->words.w1 = _lptr;                                       \
    GFX_DL_REGISTER_PTR(_lptr);                                 \
}
#else
#define gSPLight(pkt, l, n) {                                    \
    Gfx *_g = (Gfx *)(pkt);                                      \
    uintptr_t _lptr = (uintptr_t)(l);                            \
    _g->words.w0 = (_SHIFTL(G_MOVEMEM, 24, 8) |                 \
                     _SHIFTL((((n)-1)*2+G_MV_L0), 16, 8) |      \
                     _SHIFTL(sizeof(Light), 0, 16));             \
    _g->words.w1 = _lptr;                                       \
    GFX_DL_REGISTER_PTR(_lptr);                                 \
}
#endif

#undef gSPLookAtX
#undef gSPLookAtY
#ifdef F3DEX_GBI_2
#define gSPLookAtX(pkt, l) {                                     \
    Gfx *_g = (Gfx *)(pkt);                                      \
    uintptr_t _lptr = (uintptr_t)(l);                            \
    _g->words.w0 = (_SHIFTL(G_MOVEMEM, 24, 8) |                 \
                     _SHIFTL(((sizeof(Light))-1)/8, 19, 5) |     \
                     _SHIFTL((G_MVO_LOOKATX/8), 8, 8) |         \
                     _SHIFTL(G_MV_LIGHT, 0, 8));                \
    _g->words.w1 = _lptr;                                       \
    GFX_DL_REGISTER_PTR(_lptr);                                 \
}
#define gSPLookAtY(pkt, l) {                                     \
    Gfx *_g = (Gfx *)(pkt);                                      \
    uintptr_t _lptr = (uintptr_t)(l);                            \
    _g->words.w0 = (_SHIFTL(G_MOVEMEM, 24, 8) |                 \
                     _SHIFTL(((sizeof(Light))-1)/8, 19, 5) |     \
                     _SHIFTL((G_MVO_LOOKATY/8), 8, 8) |         \
                     _SHIFTL(G_MV_LIGHT, 0, 8));                \
    _g->words.w1 = _lptr;                                       \
    GFX_DL_REGISTER_PTR(_lptr);                                 \
}
#else
#define gSPLookAtX(pkt, l) {                                     \
    Gfx *_g = (Gfx *)(pkt);                                      \
    uintptr_t _lptr = (uintptr_t)(l);                            \
    _g->words.w0 = (_SHIFTL(G_MOVEMEM, 24, 8) |                 \
                     _SHIFTL(G_MV_LOOKATX, 16, 8) |             \
                     _SHIFTL(sizeof(Light), 0, 16));             \
    _g->words.w1 = _lptr;                                       \
    GFX_DL_REGISTER_PTR(_lptr);                                 \
}
#define gSPLookAtY(pkt, l) {                                     \
    Gfx *_g = (Gfx *)(pkt);                                      \
    uintptr_t _lptr = (uintptr_t)(l);                            \
    _g->words.w0 = (_SHIFTL(G_MOVEMEM, 24, 8) |                 \
                     _SHIFTL(G_MV_LOOKATY, 16, 8) |             \
                     _SHIFTL(sizeof(Light), 0, 16));             \
    _g->words.w1 = _lptr;                                       \
    GFX_DL_REGISTER_PTR(_lptr);                                 \
}
#endif

#endif /* _PLATFORM_GBI_H_ */

/*
 * mp_beam_rawcast.c — pure legacy raw-offset arithmetic for FID-0094. See the
 * header for the authoritative VERSION_US ASM sites. ROM-free / SDL-free.
 */
#include "mp_beam_rawcast.h"

size_t mpBeamLegacyFiringOffset(int hand) {
    /* 0x875 for hand 0; +936 per hand (0xC1D for hand 1). */
    return (size_t)MP_BEAM_N64_HAND0_FIRING
         + (size_t)MP_BEAM_N64_HAND_STRIDE * (size_t)hand;
}

size_t mpBeamLegacyHandSrcOffset(int hand, int word) {
    return (size_t)MP_BEAM_N64_HAND_STRIDE * (size_t)hand
         + (size_t)MP_BEAM_N64_HANDSRC_BASE
         + 4u * (size_t)word;
}

/**
 * mixer.h — RSP audio microcode emulator interface for the PC port.
 *
 * On N64, the Acmd macros in abi.h build command packets that are sent
 * to the RSP for hardware execution.  On PC, we replace those macros
 * with direct calls to C implementations that emulate the RSP audio
 * microcode in software.  This lets the entire libultra audio synthesis
 * chain (synthesizer → filters → envmixer → resampler → ADPCM decoder)
 * run unchanged, producing correct audio output.
 *
 * Adapted from fgsfdsfgs/perfect_dark port/src/mixer.c, adjusted for
 * GoldenEye's old libaudio API (aSetBuffer-based calling pattern).
 */

#ifndef MIXER_H
#define MIXER_H

#include <stdint.h>

/* State types from abi.h (already typedef'd before we get here) */

/* ===== C implementation function declarations ===== */

void mixerInit(void);

void mixerSegment(unsigned int seg, unsigned int base);
void mixerSetBuffer(unsigned int flags, unsigned int dmemin,
                    unsigned int dmemout, unsigned int count);
void mixerClearBuffer(unsigned int dmem, unsigned int count);
void mixerLoadBuffer(const void *addr);
void mixerLoadBufferSwap16(const void *addr);
void mixerSaveBuffer(void *addr);
void mixerADPCMdec(unsigned int flags, void *state);
void mixerResample(unsigned int flags, unsigned int pitch, void *state);
void mixerEnvMixer(unsigned int flags, void *state);
void mixerInterleave(unsigned int left, unsigned int right);
void mixerMix(unsigned int flags, int16_t gain,
              unsigned int dmemi, unsigned int dmemo);
void mixerSetVolume(unsigned int flags, int16_t vol,
                    int16_t voltgt, int16_t volrate);
void mixerSetLoop(void *state);
void mixerLoadADPCM(unsigned int count, const void *addr);
void mixerDMEMMove(unsigned int dmemin, unsigned int dmemout,
                   unsigned int count);
void mixerPoleFilter(unsigned int flags, int16_t gain, void *state);

typedef struct PortMixerStats {
    uint32_t adpcmDecCalls;
    uint32_t adpcmClampHits;
    uint32_t resampleCalls;
    uint32_t resampleClampHits;
    uint32_t envMixerCalls;
    uint32_t envMixerSampleFrames;
    uint32_t envMixerClampHits;
    uint32_t envSampleXor;
    uint32_t mixCalls;
    uint32_t mixClampHits;
    uint32_t poleFilterCalls;
    uint32_t poleFilterSampleFrames;
    uint32_t poleFilterClampHits;
    uint32_t poleSampleXor;
    uint32_t poleFilterPeak;
    uint32_t saveBufferCalls;
    uint32_t saveBufferBytes;
    uint32_t saveBufferDmemoutCalls;
} PortMixerStats;

void mixerGetStats(PortMixerStats *out);

/* ===== Replace abi.h packet-building macros =====
 *
 * The pkt parameter is kept so that ptr++ still increments the command
 * list pointer (filters rely on the return value for sizing).  The
 * Acmd memory is allocated but unused on the port.
 */

#undef aSegment
#undef aClearBuffer
#undef aSetBuffer
#undef aLoadBuffer
#undef aSaveBuffer
#undef aADPCMdec
#undef aResample
#undef aEnvMixer
#undef aInterleave
#undef aMix
#undef aSetVolume
#undef aSetLoop
#undef aLoadADPCM
#undef aDMEMMove
#undef aPoleFilter

/* (void)(pkt) ensures ptr++ side effects are evaluated even though
 * the Acmd packet data isn't used on the port. */
#define aSegment(pkt, s, b)          ((void)(pkt), mixerSegment((s), (b)))
#define aClearBuffer(pkt, d, c)      ((void)(pkt), mixerClearBuffer((d), (c)))
#define aSetBuffer(pkt, f, i, o, c)  ((void)(pkt), mixerSetBuffer((f), (i), (o), (c)))
#define aLoadBuffer(pkt, s)          ((void)(pkt), mixerLoadBuffer((const void *)(s)))
#define aLoadBufferSwap16(pkt, s)    ((void)(pkt), mixerLoadBufferSwap16((const void *)(s)))
#define aSaveBuffer(pkt, s)          ((void)(pkt), mixerSaveBuffer((void *)(s)))
#define aADPCMdec(pkt, f, s)         ((void)(pkt), mixerADPCMdec((f), (void *)(s)))
#define aResample(pkt, f, p, s)      ((void)(pkt), mixerResample((f), (p), (void *)(s)))
#define aEnvMixer(pkt, f, s)         ((void)(pkt), mixerEnvMixer((f), (void *)(s)))
#define aInterleave(pkt, l, r)       ((void)(pkt), mixerInterleave((l), (r)))
#define aMix(pkt, f, g, i, o)        ((void)(pkt), mixerMix((f), (int16_t)(g), (i), (o)))
#define aSetVolume(pkt, f, v, t, r)  ((void)(pkt), mixerSetVolume((f), (int16_t)(v), (int16_t)(t), (int16_t)(r)))
#define aSetLoop(pkt, a)             ((void)(pkt), mixerSetLoop((void *)(a)))
#define aLoadADPCM(pkt, c, d)        ((void)(pkt), mixerLoadADPCM((c), (const void *)(d)))
#define aDMEMMove(pkt, i, o, c)      ((void)(pkt), mixerDMEMMove((i), (o), (c)))
#define aPoleFilter(pkt, f, g, s)    ((void)(pkt), mixerPoleFilter((f), (int16_t)(g), (void *)(s)))

#endif /* MIXER_H */

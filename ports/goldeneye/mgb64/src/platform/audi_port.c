/**
 * audi_port.c — Port-specific audio manager for GoldenEye PC.
 *
 * Replaces the N64 audio thread model with synchronous per-frame
 * audio synthesis. amCreateAudioManager initializes the libultra
 * synthesizer. portAudioFrame() is called each frame to drive
 * the synthesis chain via alAudioFrame() + mixer.c.
 */
#ifdef NATIVE_PORT

#include <ultra64.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audi.h"
#include "audio_pc.h"
#include "audio_queue_controller.h"
#include "mixer.h"
#include "port_env.h"
#include "snd.h"

/* libaudio.h is now ungated, all types available via ultra64.h */
extern int g_deterministic;
/* PERF-035: predicate exposed by stubs.c — true while the SDL AI queue sits
 * below the same 12-frame web cap osAiSetNextBuffer enforces. Used by the
 * pre-load prefill so it fills to (but never past) the cap. */
extern int osAiQueueBelowLimit(void);

#define OUTPUT_RATE                    0x5622
#ifdef REFRESH_PAL
#define MAYBE_FRAME_RATE               50
#else
#define MAYBE_FRAME_RATE               60
#endif
#define FRAMES_PER_FIELD_AS_POW2       1
#define EXTRA_SAMPLES                  0x25
#define NUMBER_OUTPUT_BUFFERS          3
#define NUMBER_ACMD_LISTS              2
#define MAX_ACMD_SIZE                  3000
#define NUMBER_DMA_BUFFERS             64
#define AUDIO_DMA_MAX_BUFFER_LENGTH    0x400  /* 1024: must cover MAX_RATIO * AL_MAX_RSP_SAMPLES * 2 + padding */
#define AUDIO_DMA_ALLOC_PADDING        64    /* extra bytes so alignment overreads land in zeroed memory */
#define AUDIO_DMA_IO_QUEUE_SIZE        64
#define AUDIO_DMA_QUEUE_SIZE           66
#define PORT_AUDIO_CUSTOM_FX_SECTION_COUNT 6
#define PORT_AUDIO_FX_MS               *(((s32)((f32)44.1f)) & ~0x7)
/* H1 output low-pass (§2.4/§4.2): default-OFF one-pole DAC-coloration filter.
 * Now driven by two LIVE settings (Audio.OutputFilter / Audio.OutputFilterAlpha,
 * registered in audio_pc.c) instead of a latched env + a hardcoded alpha. The
 * ares boot capture (W6.E1.T2) matched the direct libaudio path within budget
 * with the filter OFF, so it stays default-OFF and is NOT in --remaster; the knob
 * is retained. Legacy GE007_ENABLE/DISABLE_LIBAUDIO_LOWPASS still honored below. */

static s32 s_portAudioCustomFxParams[PORT_AUDIO_CUSTOM_FX_SECTION_COUNT * 8 + 2] = {
    6,     160 PORT_AUDIO_FX_MS,
    0,       4 PORT_AUDIO_FX_MS,   9830,  -9830,      0,        0,     0,       0,
    4 PORT_AUDIO_FX_MS,     8 PORT_AUDIO_FX_MS,   9830,  -9830, 0x2B84,        0,     0,  0x2500,
    20 PORT_AUDIO_FX_MS,   64 PORT_AUDIO_FX_MS,  16384, -16384, 0x11EB,        0,     0,  0x3000,
    80 PORT_AUDIO_FX_MS,  140 PORT_AUDIO_FX_MS,  16384, -16384, 0x11EB,        0,     0,  0x3500,
    84 PORT_AUDIO_FX_MS,  120 PORT_AUDIO_FX_MS,   8192,  -8192,      0,        0,     0,  0x4000,
    0,     148 PORT_AUDIO_FX_MS,  13000, -13000,      0,   0x017C,   0xA,  0x4500
};

/* Test-only accessor for tests/test_audio_fx_params.c (AUDIT-backlog W3.5):
 * exposes the file-local s_portAudioCustomFxParams table's shape/values
 * without changing its storage class. Not called by production code. */
const s32 *portAudioTestGetCustomFxParams(s32 *out_len) {
    if (out_len) {
        *out_len = (s32)(sizeof(s_portAudioCustomFxParams) / sizeof(s_portAudioCustomFxParams[0]));
    }
    return s_portAudioCustomFxParams;
}

typedef struct {
    ALLink node;
    int    startAddr;
    u32    lastFrame;
    u8    *ptr;
} DMABuffer;

typedef struct {
    union { u8 initialized; s32 _align; } u;
    DMABuffer *firstUsed;
    DMABuffer *firstFree;
} DMAState;

typedef struct {
    s16 *data;
    s16  frameSamples;
} AudioInfo;

static struct {
    Acmd      *cmdList[NUMBER_ACMD_LISTS];
    AudioInfo *audioInfo[NUMBER_OUTPUT_BUFFERS];
    ALGlobals  g;
} g_PortAudioMgr;

static DMAState  g_DmaState;
static DMABuffer g_DmaBuffers[NUMBER_DMA_BUFFERS];
static u32       g_FrameSize;
static u32       g_NominalFrameSizeBase;
static u32       g_NominalFrameSizeRemainder;
static u32       g_NominalFrameSizeQuantum;
static u32       g_NominalFrameSizeAccumulator;
static u32       g_MaxFrameSize;
static PortAudioPumpRate g_AudioPumpRate;
static u32       g_AudioPumpLastCount;
static u32       g_AudioPumpBaseSamples;
static s32       g_AudioPumpLastCountValid;
static s32       g_CommandLength;
static u32       g_CurrentAcmdList;
static u32       g_AudioFrameCount;
static u32       g_NextDMa;
static OSIoMesg  g_DmaIOMessageBuffer[AUDIO_DMA_IO_QUEUE_SIZE];
static OSMesgQueue g_DmaMessageQueue;
static OSMesg    g_DmaMessageBuffer[AUDIO_DMA_QUEUE_SIZE];
static s32       g_portAudioReady;
static AudioInfo *g_lastInfo;
/* PERF-052: the AudioInfo whose synth is currently in flight on the worker
 * (kicked but not yet joined+tailed). NULL = no job pending (first enable frame,
 * or non-threaded mode). Main-thread-only. */
static AudioInfo *s_synthPendingInfo;
static ALFxId    g_portAudioFxType;
static u8        g_portAudioFxCustom;
static s32       g_LibaudioLowPassState[2];
static s32       g_LibaudioLowPassInitialized;
static s32       g_LibaudioLowPassEnabled;      /* legacy env cache (-1 = unread)   */

/* H1 LIVE settings (registered in audio_pc.c::portAudioRegisterConfig). Re-read
 * each frame in portAudioApplyLibaudioLowPass — no latching. */
s32 g_portAudioOutputFilter      = 0;
s32 g_portAudioOutputFilterAlpha = PORT_AUDIO_OUTPUT_FILTER_ALPHA_DEFAULT;

/* PERF-010: audio-queue occupancy target (audio frames). Registered as the LIVE
 * Audio.QueueTargetFrames in audio_pc.c; re-read each audio frame by the
 * non-deterministic occupancy controller in portAudioFrame(). Default 1.5 is
 * byte-identical to the historical fixed target. */
f32 g_portAudioQueueTargetFrames = 1.5f;

static u32 portAudioAlign16(u32 samples) {
    return samples & ~0xfU;
}

static void portAudioResetPumpRate(void) {
    const u32 nominal = portAudioAlign16(g_FrameSize / 2);
    portAudioPumpRateReset(&g_AudioPumpRate, nominal);
    g_AudioPumpLastCount = 0;
    g_AudioPumpBaseSamples = nominal;
    g_AudioPumpLastCountValid = 0;
}

static u32 portAudioMeasurePumpBase(u32 max_samples) {
    const u32 now = osGetCount();
    const u32 nominal = portAudioAlign16(g_FrameSize / 2);

    if (!g_AudioPumpLastCountValid) {
        g_AudioPumpLastCount = now;
        g_AudioPumpLastCountValid = 1;
        g_AudioPumpBaseSamples = nominal;
        return nominal;
    }

    g_AudioPumpBaseSamples = portAudioPumpRateObserve(
        &g_AudioPumpRate,
        now - g_AudioPumpLastCount, /* unsigned subtraction handles counter wrap */
        osClockRate,
        OUTPUT_RATE,
        nominal,
        max_samples);
    g_AudioPumpLastCount = now;
    return g_AudioPumpBaseSamples;
}

static int portAudioPumpRateAdaptiveEnabled(void) {
    static int disabled = -1;
    if (disabled < 0) {
        disabled = port_env_bool(
            "GE007_NO_AUDIO_PUMP_EMA", 0,
            "FID-0141 negative control: restore the fixed 60 Hz audio-controller base, which thins the queue cushion below 40 fps.");
    }
    return !disabled;
}

static s16 portAudioClampS16(s32 value) {
    if (value < -0x8000) return -0x8000;
    if (value > 0x7fff) return 0x7fff;
    return (s16)value;
}

static u32 portAudioNextNominalFrameSize(void) {
    u32 samples = g_NominalFrameSizeBase;

    if (samples == 0) {
        return g_FrameSize;
    }

    if (g_NominalFrameSizeRemainder != 0 && g_NominalFrameSizeQuantum != 0) {
        g_NominalFrameSizeAccumulator += g_NominalFrameSizeRemainder;
        if (g_NominalFrameSizeAccumulator >= g_NominalFrameSizeQuantum) {
            samples += 0x10;
            g_NominalFrameSizeAccumulator -= g_NominalFrameSizeQuantum;
        }
    }

    return samples;
}

static void portAudioApplyLibaudioLowPass(s16 *samples, s32 sampleFrames) {
    s32 i;
    s32 alpha;
    int enabled;

    if (samples == NULL || sampleFrames <= 0) {
        return;
    }

    /* Legacy env alias for pre-existing A/B scripts. Env is immutable per run, so
     * cache it once (-1 = unread). Honored IN ADDITION to Audio.OutputFilter so
     * old GE007_ENABLE/DISABLE_LIBAUDIO_LOWPASS invocations keep working. */
    if (g_LibaudioLowPassEnabled < 0) {
        const char *enabled_env = getenv("GE007_ENABLE_LIBAUDIO_LOWPASS");
        const char *disabled_env = getenv("GE007_DISABLE_LIBAUDIO_LOWPASS");
        g_LibaudioLowPassEnabled =
            (enabled_env != NULL && *enabled_env != '\0' && *enabled_env != '0' &&
             (disabled_env == NULL || *disabled_env == '\0' || *disabled_env == '0'))
                ? 1
                : 0;
    }

    /* LIVE: re-read the setting every frame (lock-free s32 read). */
    enabled = (g_portAudioOutputFilter != 0) || g_LibaudioLowPassEnabled;
    if (!enabled) {
        g_LibaudioLowPassInitialized = 0; /* re-seed cleanly on the next enable */
        return;
    }

    alpha = g_portAudioOutputFilterAlpha;
    if (alpha < 1) {
        alpha = 1;
    } else if (alpha > 32767) {
        alpha = 32767;
    }

    if (!g_LibaudioLowPassInitialized) {
        g_LibaudioLowPassState[0] = (s32)samples[0] << 15;
        g_LibaudioLowPassState[1] = (s32)samples[1] << 15;
        g_LibaudioLowPassInitialized = 1;
    }

    for (i = 0; i < sampleFrames; i++) {
        s32 ch;

        for (ch = 0; ch < 2; ch++) {
            s32 *state = &g_LibaudioLowPassState[ch];
            s32 target = (s32)samples[(i << 1) + ch] << 15;
            int64_t delta = (int64_t)target - (int64_t)*state;

            *state += (s32)((delta * alpha) >> 15);
            samples[(i << 1) + ch] = portAudioClampS16(*state >> 15);
        }
    }
}

/* W6.E3.T1 §4.3: final-mix master-volume scaler. Applied to the fully-mixed
 * stereo buffer (music + SFX) at the tail of portAudioFrame, just before the
 * queue write, so Audio.MasterVolume governs the real libaudio output (the dead
 * master fix). Q15 integer multiply with identity-bypass at unity (32768) => the
 * default (Audio.MasterVolume=1.0) is byte-identical. LIVE: re-reads the setting
 * every frame, so a menu change is audible without restart. */
static void portAudioApplyMasterVolume(s16 *samples, s32 sampleFrames) {
    const s32 q15 = portAudioGetMasterVolumeQ15();
    s32 i;
    s32 n;

    if (samples == NULL || sampleFrames <= 0) {
        return;
    }
    if (q15 >= 32768) {
        return; /* unity: byte-identical bypass */
    }

    n = sampleFrames << 1; /* stereo interleaved samples */
    for (i = 0; i < n; i++) {
        s32 s = ((s32)samples[i] * q15) >> 15;
        samples[i] = portAudioClampS16(s);
    }
}

static intptr_t amDmaCallback(s32 addr, s32 len, void *state) {
    void *freeBuffer;
    s32 delta;
    DMABuffer *dmaPtr, *lastDmaPtr;
    s32 addrEnd, buffEnd;
    (void)state;

    /* DMA from ROM — addr is a ROM offset */

    lastDmaPtr = NULL;
    dmaPtr = g_DmaState.firstUsed;
    delta = addr & 0x1;
    addrEnd = addr + len;

    while (dmaPtr) {
        buffEnd = dmaPtr->startAddr + AUDIO_DMA_MAX_BUFFER_LENGTH;
        if ((u32)dmaPtr->startAddr > (u32)addr) break;
        if (addrEnd <= buffEnd) {
            dmaPtr->lastFrame = g_AudioFrameCount;
            freeBuffer = (dmaPtr->ptr + addr) - dmaPtr->startAddr;
            return (intptr_t)freeBuffer;
        }
        lastDmaPtr = dmaPtr;
        dmaPtr = (DMABuffer *)dmaPtr->node.next;
    }

    dmaPtr = g_DmaState.firstFree;
    if (!dmaPtr) {
        if (!lastDmaPtr) lastDmaPtr = g_DmaState.firstUsed;
        if (!lastDmaPtr) return 0;
        return (intptr_t)(lastDmaPtr->ptr) + delta;
    }

    g_DmaState.firstFree = (DMABuffer *)dmaPtr->node.next;
    alUnlink((ALLink *)dmaPtr);

    if (lastDmaPtr)
        alLink((ALLink *)dmaPtr, (ALLink *)lastDmaPtr);
    else if (g_DmaState.firstUsed) {
        lastDmaPtr = g_DmaState.firstUsed;
        g_DmaState.firstUsed = dmaPtr;
        dmaPtr->node.next = (ALLink *)lastDmaPtr;
        dmaPtr->node.prev = 0;
        lastDmaPtr->node.prev = (ALLink *)dmaPtr;
    } else {
        g_DmaState.firstUsed = dmaPtr;
        dmaPtr->node.next = 0;
        dmaPtr->node.prev = 0;
    }

    freeBuffer = dmaPtr->ptr;
    addr -= delta;
    dmaPtr->startAddr = addr;
    dmaPtr->lastFrame = g_AudioFrameCount;

    osPiStartDma(&g_DmaIOMessageBuffer[g_NextDMa++], OS_MESG_PRI_HIGH,
                 OS_READ, (u32)addr, freeBuffer,
                 AUDIO_DMA_MAX_BUFFER_LENGTH, &g_DmaMessageQueue);

    return (intptr_t)freeBuffer + delta;
}

static ALDMAproc amDmaNew(DMAState **state) {
    if (!g_DmaState.u.initialized) {
        g_DmaState.firstUsed = NULL;
        g_DmaState.firstFree = g_DmaBuffers;
        g_DmaState.u.initialized = 1;
    }
    *state = &g_DmaState;
    return (ALDMAproc)&amDmaCallback;
}

static void amClearDmaBuffers(void) {
    u32 i;
    OSMesg m = 0;
    DMABuffer *dmaPtr, *nextPtr;
    for (i = 0; i < g_NextDMa; i++)
        osRecvMesg(&g_DmaMessageQueue, &m, OS_MESG_NOBLOCK);
    dmaPtr = g_DmaState.firstUsed;
    while (dmaPtr) {
        nextPtr = (DMABuffer *)dmaPtr->node.next;
        if (dmaPtr->lastFrame + 1 < g_AudioFrameCount) {
            if (g_DmaState.firstUsed == dmaPtr)
                g_DmaState.firstUsed = (DMABuffer *)dmaPtr->node.next;
            alUnlink((ALLink *)dmaPtr);
            if (g_DmaState.firstFree)
                alLink((ALLink *)dmaPtr, (ALLink *)g_DmaState.firstFree);
            else {
                g_DmaState.firstFree = dmaPtr;
                dmaPtr->node.next = 0;
                dmaPtr->node.prev = 0;
            }
        }
        dmaPtr = nextPtr;
    }
    g_NextDMa = 0;
    g_AudioFrameCount++;
}

static void portAudioMeasureOutput(const s16 *samples, s32 sampleFrames,
                                   u32 *peakOut, u32 *railHitsOut) {
    s32 i;
    s32 totalSamples = sampleFrames * 2;
    u32 peak = 0;
    u32 railHits = 0;

    for (i = 0; i < totalSamples; i++) {
        s32 v = samples[i];
        u32 amp;

        if (v == -32768 || v == 32767) {
            railHits++;
        }

        amp = (v < 0) ? (u32)-v : (u32)v;
        if (amp > peak) {
            peak = amp;
        }
    }

    *peakOut = peak;
    *railHitsOut = railHits;
}

void amCreateAudioManager(ALSynConfig *alconf) {
    u32 j;
    f32 fsize;
    u32 exactFrameNumerator;

    printf("[AUDIO-PORT] Initializing audio synthesizer...\n");
    mixerInit();

    alconf->dmaproc = (ALDMANew)&amDmaNew;
    alconf->outputRate = OUTPUT_RATE;

    fsize = (f32)((alconf->outputRate << FRAMES_PER_FIELD_AS_POW2) / (f32)MAYBE_FRAME_RATE);
    g_FrameSize = (u32)fsize;
    if (g_FrameSize < fsize) g_FrameSize++;
    if (g_FrameSize & 0xf) g_FrameSize = (g_FrameSize & ~0xf) + 0x10;
    g_MaxFrameSize = g_FrameSize + EXTRA_SAMPLES + 0x10;
    exactFrameNumerator = alconf->outputRate << FRAMES_PER_FIELD_AS_POW2;
    g_NominalFrameSizeBase =
        portAudioAlign16(exactFrameNumerator / MAYBE_FRAME_RATE);
    g_NominalFrameSizeRemainder =
        exactFrameNumerator - (g_NominalFrameSizeBase * MAYBE_FRAME_RATE);
    g_NominalFrameSizeQuantum = 0x10 * MAYBE_FRAME_RATE;
    g_NominalFrameSizeAccumulator = 0;
    portAudioResetPumpRate();
    g_LibaudioLowPassState[0] = 0;
    g_LibaudioLowPassState[1] = 0;
    g_LibaudioLowPassInitialized = 0;
    g_LibaudioLowPassEnabled = -1;

    if (getenv("GE007_DISABLE_NATIVE_REVERB") != NULL) {
        alconf->fxType = AL_FX_NONE;
        alconf->params = NULL;
    } else if (alconf->fxType == AL_FX_CUSTOM) {
        /* The sole caller (musicSeqPlayerInit, music.c) never assigns
         * ALSynConfig.params — decomp-faithful, as retail relied on a zeroed
         * stack for that slot. The port owns the CUSTOM FX table, so bind it
         * unconditionally rather than trusting the caller's uninitialized field.
         *
         * Native (LP64): the slot reads NULL, so the old `params == NULL` guard
         * already substituted s_portAudioCustomFxParams — this change is inert.
         * wasm32 (ILP32): the narrower ALSynConfig layout leaves the params slot
         * holding stack garbage (observed 0x1). The old `== NULL` guard skipped
         * substitution, so native_fx_params_for_config() later returned 0x1 and
         * alFxNew dereferenced address 1 (params[0]) -> first-frame segfault.
         * Binding unconditionally fixes wasm and preserves the native result. */
        alconf->params = s_portAudioCustomFxParams;
    }
    g_portAudioFxType = alconf->fxType;
    g_portAudioFxCustom = (alconf->fxType == AL_FX_CUSTOM && alconf->params != NULL) ? 1 : 0;
    alInit(&g_PortAudioMgr.g, alconf);

    for (j = 0; j < NUMBER_OUTPUT_BUFFERS; j++) {
        g_PortAudioMgr.audioInfo[j] = alHeapAlloc(alconf->heap, 1, sizeof(AudioInfo));
        g_PortAudioMgr.audioInfo[j]->data = alHeapAlloc(alconf->heap, 1, g_MaxFrameSize * 4);
    }

    osCreateMesgQueue(&g_DmaMessageQueue, g_DmaMessageBuffer, AUDIO_DMA_IO_QUEUE_SIZE);

    g_DmaBuffers[0].node.prev = NULL;
    g_DmaBuffers[0].node.next = NULL;
    for (j = 0; (s32)j < NUMBER_DMA_BUFFERS - 1; j++) {
        alLink((ALLink *)&g_DmaBuffers[j+1], (ALLink *)&g_DmaBuffers[j]);
        /* Use malloc for DMA buffers to avoid heap overflow into other allocations */
        g_DmaBuffers[j].ptr = (u8 *)calloc(1, AUDIO_DMA_MAX_BUFFER_LENGTH + AUDIO_DMA_ALLOC_PADDING);
    }
    g_DmaBuffers[j].ptr = (u8 *)calloc(1, AUDIO_DMA_MAX_BUFFER_LENGTH + AUDIO_DMA_ALLOC_PADDING);

    for (j = 0; j < NUMBER_ACMD_LISTS; j++)
        g_PortAudioMgr.cmdList[j] = alHeapAlloc(alconf->heap, 1, MAX_ACMD_SIZE * sizeof(Acmd));

    g_CurrentAcmdList = 0;
    g_AudioFrameCount = 0;
    g_NextDMa = 0;
    g_portAudioReady = 1;
    g_lastInfo = NULL;

    printf("[AUDIO-PORT] Synthesizer ready: rate=%d, frameSize=%d fxType=%u custom=%u\n",
           alconf->outputRate, g_FrameSize,
           (unsigned int)g_portAudioFxType,
           (unsigned int)g_portAudioFxCustom);
}

void amStartAudioThread(void) {
    /* No thread on port — audio runs synchronously via portAudioFrame() */
}

/* PERF-035: pre-load audio prefill. Fills the SDL AI queue to its web cap so
 * the first ~200ms of a stage load has audio while the main thread is busy.
 * Drives portAudioFrame() in a bounded loop — SAFE here ONLY because it runs
 * BEFORE the stage pool is reset (ChrRecord slots still valid, so the FID-0089
 * voice-dispose write-back targets live memory). HARD no-op on native and under
 * --deterministic (tape gate byte-identical); the 16-iteration ceiling is only a
 * runaway guard, the queue-cap predicate is what prevents overshoot. */
void portAudioPrefillQueue(void) {
#ifdef __EMSCRIPTEN__
    if (g_deterministic) return;
    if (!g_portAudioReady) return;
    int i = 0;
    while (i < 16 && osAiQueueBelowLimit()) {
        portAudioFrame();
        i++;
    }
    /* The tight prefill loop and the following synchronous level load are not
     * rendered-frame pump intervals. Re-prime so neither contaminates the live
     * low-FPS estimator when gameplay resumes. */
    portAudioResetPumpRate();
#endif
}

/* PERF-060 (FID-0089-safe). A GE007_MUTE run with no signal consumer sends its
 * synthesized output straight to silence (WEB-045 mute ramp in osAiSetNextBuffer),
 * so running the heavy N64 music synth every frame is pure waste on every headless
 * CI lane. When active, skip alAudioFrame and emit a same-size silence frame.
 *
 * The scope is provably DSP-only. alAudioFrame (audio_compat.c) synthesizes MUSIC
 * from the prebuilt audio command list into the output PCM; it touches NO PortVoice
 * or ChrRecord state. The FID-0089 voice lifecycle — SFX voice sample-advance ->
 * exhaustion (audio_pc.c portAudioMixActiveVoices sets voice->active = 0) ->
 * portAudioVoiceIsActive -> sndGetPlayingState -> sndDisposeSound -> the
 * ChrRecord.ptr_SEbuffer* write-back — runs entirely inside portAudioMixSfxIntoBuffer,
 * which STILL executes in full below. amClearDmaBuffers (hence g_AudioFrameCount and
 * the DMA recycle cadence), the occupancy controller, and osAiSetNextBuffer are all
 * unchanged. So the per-frame call cadence and every voice-lifecycle side effect are
 * byte-for-byte identical; only the music DSP is elided.
 *
 * Gated OFF whenever any signal consumer is armed (GE007_AUDIO_DUMP /
 * GE007_MUSIC_AUDIO_DUMP / GE007_AUDIO_TRACE / GE007_VERBOSE) and under
 * --deterministic, so every dump / hash / tape baseline is untouched by construction.
 * The env predicate is latched once; the live portAudioIsMuted() gate lets a runtime
 * unmute restore full synthesis mid-run. */
static int portAudioSynthSkipEnvLatched(void) {
    static int cached = -1;
    if (cached < 0) {
        cached = (getenv("GE007_MUTE") != NULL)
              && (getenv("GE007_AUDIO_DUMP") == NULL)
              && (getenv("GE007_MUSIC_AUDIO_DUMP") == NULL)
              && (getenv("GE007_AUDIO_TRACE") == NULL)
              && (getenv("GE007_VERBOSE") == NULL) ? 1 : 0;
    }
    return cached;
}

static int portAudioSynthSkipActive(void) {
    extern int portAudioIsMuted(void);
    if (g_deterministic) return 0;
    if (!portAudioSynthSkipEnvLatched()) return 0;
    return portAudioIsMuted();
}

/* ---- PERF-052 pipeline helpers ------------------------------------------- */
#ifndef __EMSCRIPTEN__
extern int  portAudioSynthWorkerRunning(void);   /* audio_pc.c */
extern void portAudioSynthWorkerKick(s16 *buf, s32 frameSamples, int skip);
extern void portAudioSynthWorkerJoin(void);
#endif
static void portAudioFrameTail(AudioInfo *info);  /* defined below */

/* Occupancy-targeted frame sizing (replaces the N64 subtraction formula).
 *
 * Treat the SDL queue as explicit occupancy, not a fake DMA remainder. Keep the
 * queue near a modest target and scale production linearly:
 * - below target: nudge upward toward g_MaxFrameSize
 * - above target: reduce below realtime to drain
 *
 * FID-0141 residual: the production base follows a 1/4-rate EMA of the actual
 * wall-clock pump interval, clamped to [60 Hz nominal, synth maximum]. Thus a
 * weak machine sustaining 40/30 fps keeps the same configured occupancy cushion
 * instead of balancing at target minus 2*(device consumption - 60 Hz base).
 * The deterministic branch never samples the wall clock and remains unchanged. */
static void portAudioSizeFrame(AudioInfo *info) {
    if (g_deterministic) {
        /* Deterministic mode still needs the exact average output rate.
         * Use an aligned Bresenham cadence: NTSC alternates 720/736
         * samples to average 735 samples per 30 Hz game frame. */
        info->frameSamples = (s16)portAudioNextNominalFrameSize();
    } else {
        const u32 queued_bytes = osAiGetLength();
        const s32 queued_samples = (s32)(queued_bytes >> 2);  /* stereo s16 -> sample frames */
        /* THE 30 Hz / 60 Hz UNIT BUG (audio latency).  g_FrameSize is 736 =
         * outputRate * 2 / 60, sized for retail's 30 Hz audio task.  But the port
         * pumps this controller once per RENDERED frame at 60 Hz, so:
         *   - true device consumption is outputRate / 60 = 367.5 samples/pump; and
         *   - one realtime pump's worth of production is g_FrameSize / 2 = 368.
         * Two constants here were left in 30 Hz units and both cost latency:
         *
         *   (a) The controller base was full_samples = g_FrameSize = 736, i.e. it
         *       proposed 2x realtime every pump and relied on the (target-queued)/2
         *       feedback term to claw it back.  A proportional controller with an
         *       off-realtime base has a steady-state offset: it balances not at the
         *       target but at target + 2*(base - consumption) = target + ~737
         *       samples = target + 33 ms.  So the queue sat a full extra frame above
         *       the configured target forever (modelled + measured: 83 ms actual vs
         *       a 50 ms target).  Base it on the measured realtime pump consumption
         *       (368 at 60 Hz) and the fixed point IS the target.
         *
         *   (b) The drain floor was align16(g_FrameSize/2) = 368, which is 0.5
         *       samples ABOVE consumption (367.5) -- so once the queue was full the
         *       controller had literally no way to shrink it and railed at the cap.
         *       On web that meant PERF-035's level-load prefill went straight to the
         *       12-frame (~400 ms) cap and stuck there: ~400 ms fire-to-hear delay.
         *       Drop the floor one 16-sample quantum below realtime (352 < 367.5) so
         *       the controller has ~15.5 samples/pump of drain authority.
         *
         * Net: measured realtime base + floor below 60 Hz realtime = the queue
         * converges to the configured Audio.QueueTargetFrames target and holds
         * there at 60/40/30 Hz. Deterministic mode takes the branch above and is
         * untouched (tape gate byte-identical). */
        const s32 realtime_60hz = (s32)portAudioAlign16(g_FrameSize / 2);
        const s32 target_samples = (s32)((f32)g_FrameSize * g_portAudioQueueTargetFrames);
        s32 min_samples = realtime_60hz - 0x10;                  /* (b) floor below realtime */
        const s32 max_samples = (s32)portAudioAlign16(g_MaxFrameSize);
        const s32 full_samples = portAudioPumpRateAdaptiveEnabled()
            ? (s32)portAudioMeasurePumpBase((u32)max_samples)
            : realtime_60hz; /* FID-0141 negative control: fixed 60 Hz base */

        if (min_samples < 0x10) min_samples = 0x10;
        info->frameSamples = (s16)portAudioQueueChooseSamples(
            full_samples, queued_samples, target_samples, min_samples, max_samples);
    }

    /* Always keep the synth input aligned. */
    info->frameSamples = (s16)portAudioAlign16((u32)info->frameSamples);
}

/* Synth stage: the N64 music synth (or the PERF-060 mute-silence frame) plus the
 * libaudio low-pass, then the cmd-list double-buffer flip. Runs on the main
 * thread in the synchronous path, or on the PERF-052 worker (via
 * portAudioSynthWorkerRunJob). Exactly one of those calls it per frame, so
 * g_CurrentAcmdList / g_CommandLength stay single-owner. */
static void portAudioSynthStage(s16 *data, s32 frameSamples, int skip) {
    if (skip) {
        /* PERF-060: same-size silence frame in place of the music synth. The SFX
         * mixer (in the tail) still runs, so all FID-0089 voice lifecycle is
         * preserved. */
        memset(data, 0, (size_t)frameSamples * 4);
        g_CommandLength = 0;
    } else {
        alAudioFrame(g_PortAudioMgr.cmdList[g_CurrentAcmdList],
                     &g_CommandLength, data, frameSamples);
        portAudioApplyLibaudioLowPass(data, frameSamples);
    }
    g_CurrentAcmdList ^= 1;
}

#ifndef __EMSCRIPTEN__
/* PERF-052 worker job body — runs on the synth worker thread. Serial by
 * construction: the main thread joins the previous job before kicking the next,
 * so it owns the synth cmd-list state and the target PCM buffer for its
 * duration. The mute-skip decision is made on the main thread and passed in, so
 * the worker never reads live mute state.
 *
 * The WHOLE job runs under the reified interrupt-mask lock. This is the
 * faithful reification of the N64's exclusion model, not an extra layer: on
 * hardware, a game-thread `osSetIntMask(OS_IM_NONE)` section stopped the
 * interrupt-driven audio thread from being SCHEDULED AT ALL, so the entire
 * synth pass (alAudioFrame -> sndPlayerVoiceHandler / CSP handlers) was atomic
 * with respect to every masked game-thread section — the handlers themselves
 * never take the mask (sndPlayerVoiceHandler brackets nothing). Holding the
 * lock across the job reproduces exactly that: the game thread's short masked
 * sections (event posts, state finalize) and the worker's handler pass are
 * mutually exclusive, while unmasked main-thread work (sim, render, the
 * FID-0089 SFX tail — which takes only s_audioMutex) still overlaps the synth.
 * Lock ordering is one-way: the worker takes synthLock -> s_audioMutex (via
 * the handler's PC-mixer calls); no main-thread path holds s_audioMutex while
 * bracketing (verified), so no inversion. Uncontended in the common case —
 * main blocks only when it posts audio events while a job is in flight. */
void portAudioSynthWorkerRunJob(s16 *buf, s32 frameSamples, int skip) {
    OSIntMask mask = osSetIntMask(OS_IM_NONE);
    portAudioSynthStage(buf, frameSamples, skip);
    osSetIntMask(mask);
}
#endif

void portAudioFrame(void) {
    AudioInfo *info;

    if (!g_portAudioReady) return;

#ifndef __EMSCRIPTEN__
    if (portAudioSynthWorkerRunning()) {
        /* PERF-052 threaded pipeline (+1 frame audio latency). The synth for the
         * previous frame ran on the worker; drain it (join) and finish its
         * main-thread tail — the FID-0089 SFX-mix / master-volume / queue-write
         * cadence is byte-for-byte the synchronous path, just one frame delayed.
         * Then size THIS frame and kick the worker for it. The join barrier makes
         * the worker idle before amClearDmaBuffers / the DMA callback it will run
         * next, so the DMA state has a single owner at every instant. */
        if (s_synthPendingInfo != NULL) {
            portAudioSynthWorkerJoin();
            portAudioFrameTail(s_synthPendingInfo);
        }
        amClearDmaBuffers();
        info = g_PortAudioMgr.audioInfo[g_AudioFrameCount % NUMBER_OUTPUT_BUFFERS];
        portAudioSizeFrame(info);
        s_synthPendingInfo = info;
        /* First enable frame: s_synthPendingInfo was NULL above, so we only kick
         * here — a one-frame prime that produces no output this frame (that gap
         * is the inherent +1 latency). */
        portAudioSynthWorkerKick(info->data, info->frameSamples,
                                 portAudioSynthSkipActive());
        return;
    }
#endif

    /* Synchronous (default) path — cadence unchanged from before PERF-052. */
    amClearDmaBuffers();
    info = g_PortAudioMgr.audioInfo[g_AudioFrameCount % NUMBER_OUTPUT_BUFFERS];
    portAudioSizeFrame(info);
    portAudioSynthStage(info->data, info->frameSamples, portAudioSynthSkipActive());
    portAudioFrameTail(info);
}

/* ---- main-thread frame tail: music dump + SFX mix + master volume + queue + telemetry ---- */
static void portAudioFrameTail(AudioInfo *info) {
    u32 target_bytes;

    /* PERF-010: telemetry target mirrors the controller's actual target
     * (Audio.QueueTargetFrames), not a hardcoded 1.5, so the trace's target=/
     * soft= columns stay accurate when the knob is tuned. Byte-identical at the
     * 1.5 default. Diagnostic only — not part of the synthesized output. */
    target_bytes = (u32)((f32)g_FrameSize * g_portAudioQueueTargetFrames) * 4;

    {
        static int s_musicDumpEnabled = -1;
        if (s_musicDumpEnabled < 0) {
            s_musicDumpEnabled = (getenv("GE007_MUSIC_AUDIO_DUMP") != NULL) ? 1 : 0;
        }
        if (s_musicDumpEnabled) {
            extern void portMusicAudioDump(const void *buf, unsigned int size);
            portMusicAudioDump(info->data, info->frameSamples * 4);
        }
    }

    portAudioMixSfxIntoBuffer(info->data, info->frameSamples);

    /* W6.E3.T1 §4.3: master-volume final stage (identity at unity). */
    portAudioApplyMasterVolume(info->data, info->frameSamples);

    /* Queue audio immediately (no 1-frame delay like N64 double-buffer) */
    osAiSetNextBuffer(info->data, info->frameSamples * 4);

    /* Debug: audio health telemetry (gated behind GE007_VERBOSE) */
    {
        static int s_frameCount = 0;
        static int s_verbose = -1;
        static int s_trace_init = 0;
        static FILE *s_trace_fp = NULL;
        static u32 s_minQueue = 0xFFFFFFFF, s_maxQueue = 0;
        static u32 s_underruns = 0, s_overTarget = 0;
        static u32 s_queuePrimed = 0;
        static PortMixerStats s_prevMixerStats = {0};
        static int s_prevMixerStatsValid = 0;
        if (s_verbose < 0) s_verbose = (getenv("GE007_VERBOSE") != NULL) ? 1 : 0;
        if (!s_trace_init) {
            const char *trace_path = getenv("GE007_AUDIO_TRACE");
            s_trace_init = 1;
            if (trace_path && *trace_path) {
                s_trace_fp = fopen(trace_path, "w");
            }
        }
        s_frameCount++;

        if (s_verbose || s_trace_fp) {
            PortAiStats ai_stats = {0};
            PortSfxMixStats sfx_stats = {0};
            PortMixerStats mixer_stats = {0};
            PortMixerStats mixer_delta = {0};
            PortSndPlayerStats sndp_stats = {0};
            u32 qb = 0;
            u32 post_queue = 0;
            u32 output_peak = 0;
            u32 output_rail_hits = 0;
            u32 controller_target_bytes = target_bytes;
            u32 soft_target_bytes = controller_target_bytes;
            u32 queue_limit = g_FrameSize * 4 * 4;
            u32 drop_count = portAiGetDroppedBufferCount();
            u32 device_buffer_bytes = portAudioGetDeviceBufferBytes();

            portAiGetStats(&ai_stats);
            portAudioGetSfxMixStats(&sfx_stats);
            mixerGetStats(&mixer_stats);
            sndGetPlayerStats(&sndp_stats);
            portAudioMeasureOutput(info->data, info->frameSamples,
                                   &output_peak, &output_rail_hits);

            if (s_prevMixerStatsValid) {
                mixer_delta.adpcmClampHits =
                    mixer_stats.adpcmClampHits - s_prevMixerStats.adpcmClampHits;
                mixer_delta.resampleClampHits =
                    mixer_stats.resampleClampHits - s_prevMixerStats.resampleClampHits;
                mixer_delta.envMixerClampHits =
                    mixer_stats.envMixerClampHits - s_prevMixerStats.envMixerClampHits;
                mixer_delta.mixClampHits =
                    mixer_stats.mixClampHits - s_prevMixerStats.mixClampHits;
                mixer_delta.poleFilterClampHits =
                    mixer_stats.poleFilterClampHits - s_prevMixerStats.poleFilterClampHits;
            } else {
                mixer_delta.adpcmClampHits = mixer_stats.adpcmClampHits;
                mixer_delta.resampleClampHits = mixer_stats.resampleClampHits;
                mixer_delta.envMixerClampHits = mixer_stats.envMixerClampHits;
                mixer_delta.mixClampHits = mixer_stats.mixClampHits;
                mixer_delta.poleFilterClampHits = mixer_stats.poleFilterClampHits;
            }
            s_prevMixerStats = mixer_stats;
            s_prevMixerStatsValid = 1;

            if (!g_deterministic) {
                qb = ai_stats.queue_before_bytes;
                post_queue = ai_stats.queue_after_bytes;
                queue_limit = ai_stats.queue_limit_bytes;
            }

            if (device_buffer_bytes != 0) {
                u32 host_jitter_headroom = device_buffer_bytes * 2;
                if (soft_target_bytes < controller_target_bytes + host_jitter_headroom) {
                    soft_target_bytes = controller_target_bytes + host_jitter_headroom;
                }
            }

            if (qb < s_minQueue) s_minQueue = qb;
            if (qb > s_maxQueue) s_maxQueue = qb;
            if (!s_queuePrimed && s_frameCount > 30) {
                s_queuePrimed = 1;
            }
            if (s_queuePrimed && qb == 0) s_underruns++;
            if (qb > soft_target_bytes) s_overTarget++;

            if (s_trace_fp) {
                fprintf(s_trace_fp,
                        "{\"frame\":%d,\"samples\":%d,\"pump_base_samples\":%u,\"queue_before\":%u,\"queue_after\":%u,"
                        "\"target_bytes\":%u,\"soft_target_bytes\":%u,\"limit_bytes\":%u,\"primed\":%u,"
                        "\"fx_type\":%u,\"fx_custom\":%u,"
                        "\"output_peak\":%u,\"output_rail_hits\":%u,"
                        "\"adpcm_dec_calls\":%u,\"adpcm_clamp_hits\":%u,"
                        "\"adpcm_clamp_delta\":%u,"
                        "\"resample_calls\":%u,\"resample_clamp_hits\":%u,"
                        "\"resample_clamp_delta\":%u,"
                        "\"env_mixer_calls\":%u,\"env_mixer_sample_frames\":%u,"
                        "\"env_mixer_clamp_hits\":%u,\"env_sample_xor\":%u,"
                        "\"env_mixer_clamp_delta\":%u,"
                        "\"mix_calls\":%u,\"mix_clamp_hits\":%u,"
                        "\"mix_clamp_delta\":%u,"
                        "\"pole_filter_calls\":%u,\"pole_filter_sample_frames\":%u,"
                        "\"pole_filter_clamp_hits\":%u,"
                        "\"pole_filter_clamp_delta\":%u,"
                        "\"pole_sample_xor\":%u,\"pole_filter_peak\":%u,"
                        "\"save_buffer_calls\":%u,\"save_buffer_bytes\":%u,"
                        "\"save_buffer_dmemout_calls\":%u,"
                        "\"requested_bytes\":%u,\"accepted_bytes\":%u,\"dropped_buffers\":%u,"
                        "\"dropped_bytes\":%u,\"underruns\":%u,\"over_soft_target\":%u,"
                        "\"sfx_mix_calls\":%u,\"sfx_voice_starts\":%u,\"sfx_voice_stops\":%u,"
                        "\"sfx_active_voices\":%u,\"sfx_active_voice_frames\":%u,"
                        "\"sfx_sample_frames\":%u,\"sfx_peak_delta\":%u,\"sfx_peak_delta_max\":%u,"
                        "\"sndp_real_path\":%u,\"sndp_stub_path\":%u,"
                        "\"sndp_player_inits\":%u,\"sndp_submit_events\":%u,"
                        "\"sndp_play_events\":%u,\"sndp_voice_starts\":%u,"
                        "\"sndp_voice_stops\":%u,\"sndp_active_voices\":%u,"
                        "\"sndp_volume_updates\":%u,\"sndp_pan_updates\":%u,"
                        "\"sndp_pitch_updates\":%u,\"sndp_fx_updates\":%u,"
                        "\"sndp_release_events\":%u,\"sndp_decay_events\":%u}\n",
                        s_frameCount, info->frameSamples, g_AudioPumpBaseSamples, qb, post_queue,
                        controller_target_bytes, soft_target_bytes, queue_limit, s_queuePrimed,
                        (unsigned int)g_portAudioFxType,
                        (unsigned int)g_portAudioFxCustom,
                        output_peak,
                        output_rail_hits,
                        mixer_stats.adpcmDecCalls,
                        mixer_stats.adpcmClampHits,
                        mixer_delta.adpcmClampHits,
                        mixer_stats.resampleCalls,
                        mixer_stats.resampleClampHits,
                        mixer_delta.resampleClampHits,
                        mixer_stats.envMixerCalls,
                        mixer_stats.envMixerSampleFrames,
                        mixer_stats.envMixerClampHits,
                        mixer_stats.envSampleXor,
                        mixer_delta.envMixerClampHits,
                        mixer_stats.mixCalls,
                        mixer_stats.mixClampHits,
                        mixer_delta.mixClampHits,
                        mixer_stats.poleFilterCalls,
                        mixer_stats.poleFilterSampleFrames,
                        mixer_stats.poleFilterClampHits,
                        mixer_delta.poleFilterClampHits,
                        mixer_stats.poleSampleXor,
                        mixer_stats.poleFilterPeak,
                        mixer_stats.saveBufferCalls,
                        mixer_stats.saveBufferBytes,
                        mixer_stats.saveBufferDmemoutCalls,
                        ai_stats.requested_bytes, ai_stats.accepted_bytes,
                        drop_count, ai_stats.dropped_bytes, s_underruns, s_overTarget,
                        sfx_stats.mixCalls, sfx_stats.voiceStarts, sfx_stats.voiceStops,
                        sfx_stats.activeVoicesLast, sfx_stats.activeVoiceFrames,
                        sfx_stats.sampleFramesMixed, sfx_stats.peakDeltaLast,
                        sfx_stats.peakDeltaMax,
                        sndp_stats.realPath,
                        sndp_stats.stubPath,
                        sndp_stats.playerInits,
                        sndp_stats.submitEvents,
                        sndp_stats.playEvents,
                        sndp_stats.voiceStarts,
                        sndp_stats.voiceStops,
                        sndp_stats.activeVoices,
                        sndp_stats.volumeUpdates,
                        sndp_stats.panUpdates,
                        sndp_stats.pitchUpdates,
                        sndp_stats.fxUpdates,
                        sndp_stats.releaseEvents,
                        sndp_stats.decayEvents);
                fflush(s_trace_fp);
            }

            if (s_frameCount % 120 == 1) {
                printf("[AUDIO] f=%d samp=%d base=%u q=%uB [%u-%u] target=%uB soft=%uB limit=%uB under=%u over=%u drop=%u post=%uB req=%uB enq=%uB dropB=%uB peak=%u rails=%u sfxStart=%u sfxActive=%u sfxPeak=%u\n",
                       s_frameCount, info->frameSamples, g_AudioPumpBaseSamples, qb,
                       s_minQueue, s_maxQueue, controller_target_bytes, soft_target_bytes, queue_limit,
                       s_underruns, s_overTarget, drop_count, post_queue,
                       ai_stats.requested_bytes, ai_stats.accepted_bytes,
                       ai_stats.dropped_bytes, output_peak, output_rail_hits,
                       sfx_stats.voiceStarts, sfx_stats.activeVoicesLast,
                       sfx_stats.peakDeltaLast);
                s_minQueue = 0xFFFFFFFF;
                s_maxQueue = 0;
            }
        }
    }

    /* The cmd-list double-buffer flip now lives in portAudioSynthStage (with the
     * synth that writes the list), so it happens on whichever thread synthesized
     * this frame. */
    g_lastInfo = info;
}

#ifdef PORT_AUDIO_CONTROLLER_TEST_HOOKS
/* ROM-free wiring hooks: tests drive the REAL portAudioSizeFrame call through
 * stubbed osGetCount/osAiGetLength. Kept out of production symbol tables. */
void portAudioTestConfigureQueueController(u32 frame_size, u32 max_frame_size) {
    g_FrameSize = frame_size;
    g_MaxFrameSize = max_frame_size;
    portAudioResetPumpRate();
}

s32 portAudioTestSizeFrameSamples(void) {
    AudioInfo info = {0};
    portAudioSizeFrame(&info);
    return info.frameSamples;
}

u32 portAudioTestGetPumpBaseSamples(void) {
    return g_AudioPumpBaseSamples;
}
#endif

/* Export the nominal per-game-frame sample count so the AI stub can derive
 * a queue-limit safety net without duplicating PAL/NTSC math. */
u32 portAudioGetFrameSize(void) {
    return g_FrameSize;
}

#endif /* NATIVE_PORT */

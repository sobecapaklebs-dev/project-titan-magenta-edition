/**
 * audio_pc.c — SDL2 audio backend for the GoldenEye 007 PC port.
 *
 * Replaces the N64 RSP audio synthesizer with a simple SDL2 mixer.
 * Parses N64 bank files (big-endian ALBankFile format), decodes VADPCM
 * samples to PCM, and plays them through SDL2 audio.
 *
 * Integration points:
 * - alBnkfNew() calls portAudioParseBankFile() to parse bank data
 * - sndPlaySfx() calls portAudioPlaySound() to play decoded samples
 * - main_pc.c calls portAudioInit() during startup
 */

#ifdef PROJECT_TITAN_TGR
#include "sdl_titan_compat.h"
#else
#include <SDL.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <ultra64.h>
#include "config_pc.h"
#include "settings.h"
#include "audio_pc.h"
#include "web_audio_worklet.h"

/* ===== Configuration ===== */
#ifdef PROJECT_TITAN_TGR
#define PORT_AUDIO_RATE       32000
#else
#define PORT_AUDIO_RATE       22050
#endif
#define PORT_AUDIO_CHANNELS   2
/* WEB-012: native tunes 512 for a real-time CoreAudio/WASAPI thread. The browser
 * has no such thread — SDL 2.32's emscripten driver is a deprecated MAIN-THREAD
 * ScriptProcessorNode at the context rate (48 kHz → 512 frames ≈ 10.7 ms), a
 * deadline any sim+encode frame routinely blows on wasm (heavy scenes, pipeline
 * compiles, GC) → crackle. Default the buffer deeper on web (2048 ≈ +32 ms
 * latency) where the extra headroom matters far more than the latency. */
#ifdef __EMSCRIPTEN__
#define PORT_AUDIO_SAMPLES    2048
#else
#define PORT_AUDIO_SAMPLES    512
#endif
#define PORT_MAX_VOICES       24
#define PORT_MAX_SOUNDS       512

/* N64 ADPCM constants */
#define AL_ADPCM_WAVE  0
#define AL_RAW16_WAVE  1
#define VADPCM_FRAME_BYTES    9
#define VADPCM_FRAME_SAMPLES  16

/* ===== Big-endian readers ===== */
static inline u16 rd_be16(const u8 *p) {
    return (u16)((p[0] << 8) | p[1]);
}
static inline u32 rd_be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
static inline s16 rd_bes16(const u8 *p) { return (s16)rd_be16(p); }
static inline s32 rd_bes32(const u8 *p) { return (s32)rd_be32(p); }
static inline s32 adpcm_sext4(s32 v) { return ((v & 0xf) ^ 8) - 8; }

/* Overflow-safe "does [offset, offset+count) fit inside a buffer of `size`
 * bytes" check (template: audio_compat.c:41-44's bank_ctl_has()). A naive
 * `offset + count > size` wraps when offset/count are attacker/corrupt-file
 * controlled u32s near UINT32_MAX, silently passing an out-of-bounds access;
 * this idiom can't overflow because it only ever subtracts a smaller value
 * from a larger one once `offset <= size` is already established. Local copy
 * (rather than sharing audio_compat.c's static) because this is a different
 * translation unit. */
static int bank_ctl_has(u32 size, u32 offset, u32 count) {
    return offset <= size && count <= size - offset;
}

/* ===== Decoded sample storage ===== */
typedef struct {
    s16 *pcm;
    u32  numSamples;
    u32  sampleRate;
    s32  hasLoop;
    u32  loopStart;
    u32  loopEnd;
#ifdef PROJECT_TITAN_TGR
    const u8 *bookData;
    u32 romOffset;
    u32 encodedBytes;
    u32 cacheBytes;
    u32 cacheStamp;
    s16 bookOrder;
    s16 bookPredictors;
    u8 waveType;
#endif
} PortDecodedSample;

/* ===== Mixer voice ===== */
typedef struct {
    PortDecodedSample *sample;
    f32 position;    /* fractional sample position */
    f32 pitch;       /* playback speed (1.0 = normal) */
    f32 gainCurrent;
    f32 gainTarget;
    f32 gainStep;
    s32 gainSamplesRemaining;
    f32 panL;
    f32 panR;
    f32 envCurrent;
    f32 envTarget;
    f32 envStep;
    s32 envSamplesRemaining;
    f32 envDecayTarget;
    s32 envDecaySamples;
    u8  envPhase;
    u8  envEnabled;
    u8  stopOnGainEnd;
    u8  fxMix;
    s32 delaySamples;
    s32 active;
} PortVoice;

typedef struct {
    u32 activeVoices;
    u32 sampleFramesMixed;
    u32 peakDelta;
} PortSfxFrameMixStats;

/* ===== Global state ===== */
static SDL_AudioDeviceID s_audioDevice;
static SDL_mutex        *s_audioMutex;
static PortVoice         s_voices[PORT_MAX_VOICES];
static PortDecodedSample s_sfxSamples[PORT_MAX_SOUNDS];
#ifdef PROJECT_TITAN_TGR
#define PORT_TITAN_SFX_CACHE_BYTES (1536u * 1024u)
static u32 s_titanSfxCacheBytes;
static u32 s_titanSfxCacheStamp;
#endif
static PortSfxMixStats   s_sfxMixStats;
static s32               s_sfxSampleCount;
static u32               s_sfxBankSampleRate;
static s32               s_audioInitialized;
static s32               s_audioDisabledMsgShown;
static f32               s_masterVolume = 1.0f;   /* Audio.MasterVolume (W6.E3.T1 Q1 opt-a: 0.7->1.0 = unity/byte-identical) */
static f32               s_musicBusVolume = 1.0f;  /* Audio.MusicVolume  (music bus, unity passthrough) */
static f32               s_sfxBusVolume   = 1.0f;  /* Audio.SfxVolume    (SFX bus, unity passthrough)   */
static s32               s_sfxMixLegacy = -1;      /* GE007_SFX_MIX_LEGACY=1: old nearest-neighbor/end-wrap SFX mix (audit §2.3/2.4 A/B) */
static s32               s_traceWeaponAudio = -1;  /* WEB-048: GE007_TRACE_WEAPON_AUDIO cached once (mirror s_sfxMixLegacy) */
/* WEB-045: soft-mute ramp state (Q15, 32768 = unity). Live mute attenuates the
 * queued PCM in the AI queue path (portAudioApplyMuteRamp, called from
 * osAiSetNextBuffer) instead of pausing the SDL device — the device keeps
 * running so nothing stale survives to replay on unmute, and the on/off click
 * becomes a short slew. s_muteGainQ15 follows s_muteTargetQ15 by one
 * PORT_MUTE_RAMP_STEP per sample-frame (~5 ms full travel). Both are touched
 * only on the main thread (queue mode: the callback mixer is dead, WEB-047), so
 * no lock is needed. At gain==unity && target==unity the apply is a no-op, i.e.
 * byte-identical to the un-muted native path. */
#define PORT_MUTE_RAMP_SAMPLES ((PORT_AUDIO_RATE * 5) / 1000)  /* ~5 ms */
#define PORT_MUTE_RAMP_STEP    (32768 / PORT_MUTE_RAMP_SAMPLES)
static s32               s_muteTargetQ15 = 32768;  /* 0 = muted, 32768 = unity */
static s32               s_muteGainQ15   = 32768;
static s32               s_lastPlayedVoice = -1;
static u32               s_audioDeviceBufferBytes;
static s32               s_audioDeviceSamples = PORT_AUDIO_SAMPLES;
static s32               s_sfxTraceInit;
static FILE             *s_sfxTraceFp;
static s32               s_sfxTraceEnabled;
static s32               s_sfxAuthoredMix = -1;

extern u8  *g_romData;
extern u32  g_romSize;
extern s32  g_GlobalTimer;

/* Forward declarations for music system (defined later in this file) */
static void portAudioParseInstrumentBank(u8 *ctlData, u32 ctlSize, u32 tblRomOffset);

enum {
    PORT_ENV_PHASE_HOLD = 0,
    PORT_ENV_PHASE_ATTACK,
    PORT_ENV_PHASE_DECAY
};

static int portAudioEnvEnabled(void)
{
    const char *value = getenv("GE007_AUDIO");

    if (!value) {
        value = getenv("GE007_ENABLE_AUDIO");
    }

    /* Audio is ON by default. Only disable when explicitly set to "0". */
    if (value && (value[0] == '0' && value[1] == '\0')) {
        return 0;
    }

    return 1;
}

static f32 portAudioClamp01(f32 value)
{
    if (value < 0.0f) {
        return 0.0f;
    }

    if (value > 1.0f) {
        return 1.0f;
    }

    return value;
}

static s32 portAudioMicrosecondsToSamples(ALMicroTime usec)
{
    int64_t samples;

    if (usec <= 0) {
        return 0;
    }

    samples = ((int64_t)usec * PORT_AUDIO_RATE + 999999) / 1000000;
    if (samples <= 0) {
        samples = 1;
    }

    return (s32)samples;
}

static s32 portAudioAuthoredMixEnabled(void)
{
    if (s_sfxAuthoredMix < 0) {
        const char *value = getenv("GE007_SFX_AUTHORED_MIX");
        s_sfxAuthoredMix = (value != NULL && value[0] == '0' && value[1] == '\0') ? 0 : 1;
    }

    return s_sfxAuthoredMix;
}

void portAudioTraceSfxJson(const char *fmt, ...)
{
    va_list ap;

    if (!s_sfxTraceInit) {
        const char *path = getenv("GE007_SFX_TRACE_JSONL");
        s_sfxTraceInit = 1;
        if (path != NULL && path[0] != '\0') {
            s_sfxTraceFp = fopen(path, "a");
            if (s_sfxTraceFp != NULL) {
                setvbuf(s_sfxTraceFp, NULL, _IOLBF, 0);
                s_sfxTraceEnabled = 1;
            }
        }
    }

    if (!s_sfxTraceEnabled || s_sfxTraceFp == NULL || fmt == NULL) {
        return;
    }

    va_start(ap, fmt);
    vfprintf(s_sfxTraceFp, fmt, ap);
    va_end(ap);
    fputc('\n', s_sfxTraceFp);
}

/* ===== VADPCM Decoder =====
 *
 * N64 VADPCM (Vector ADPCM) compresses 16 PCM samples into 9 bytes:
 *   Byte 0: header — high nibble = scale shift, low nibble = predictor index
 *   Bytes 1-8: 16 signed 4-bit residuals (2 per byte, high nibble first)
 *
 * Decoding uses a predictor book (ALADPCMBook) with coefficients that
 * predict each sample from previous samples. The decoder processes
 * samples in two groups of 8 for prediction history purposes.
 */
static void vadpcm_decode(const u8 *adpcm_data, s32 adpcm_len,
                          const s16 *book, s32 order, s32 npredictors,
                          s16 *out, s32 *out_sample_count)
{
    s32 nframes = adpcm_len / VADPCM_FRAME_BYTES;
    s16 state[16];
    memset(state, 0, sizeof(state));

    if (order < 2 || npredictors <= 0) {
        *out_sample_count = 0;
        return;
    }

    for (s32 f = 0; f < nframes; f++) {
        const u8 *frame = &adpcm_data[f * VADPCM_FRAME_BYTES];
        s32 shift = frame[0] >> 4;
        s32 pred_idx = frame[0] & 0x0f;
        s32 residualScale = 1 << shift;
        s16 *frameOut = &out[f * VADPCM_FRAME_SAMPLES];

        if (pred_idx >= npredictors) {
            pred_idx = 0;
        }

        const s16 *coef = &book[pred_idx * order * 8];
        s16 prevFrame[16];
        memcpy(prevFrame, state, sizeof(prevFrame));

        for (s32 group = 0; group < 2; group++) {
            s16 ins[8];
            s16 prev1;
            s16 prev2;

            if (group == 0) {
                prev2 = prevFrame[14];
                prev1 = prevFrame[15];
            } else {
                prev2 = frameOut[6];
                prev1 = frameOut[7];
            }

            for (s32 j = 0; j < 4; j++) {
                u8 packed = frame[1 + group * 4 + j];
                ins[j * 2]     = (s16)(adpcm_sext4(packed >> 4) * residualScale);
                ins[j * 2 + 1] = (s16)(adpcm_sext4(packed) * residualScale);
            }

            for (s32 j = 0; j < 8; j++) {
                s32 acc = coef[j] * prev2 + coef[8 + j] * prev1
                        + (s32)ins[j] * 2048;

                for (s32 k = 0; k < j; k++) {
                    acc += coef[8 + ((j - k) - 1)] * ins[k];
                }

                acc >>= 11;
                if (acc > 32767) acc = 32767;
                if (acc < -32768) acc = -32768;
                frameOut[group * 8 + j] = (s16)acc;
            }
        }

        memcpy(state, frameOut, sizeof(state));
    }

    *out_sample_count = nframes * VADPCM_FRAME_SAMPLES;
}

static void portAudioNormalizeDecodedSample(PortDecodedSample *sample)
{
    s32 peak = 0;

    if (!sample || !sample->pcm || sample->numSamples == 0) {
        return;
    }

    for (u32 i = 0; i < sample->numSamples; i++) {
        s32 v = sample->pcm[i];
        if (v < 0) {
            v = -v;
        }
        if (v > peak) {
            peak = v;
        }
    }

    if (peak > 0 && peak < 512) {
        const f32 targetPeak = 12000.0f;
        f32 scale = targetPeak / (f32)peak;

        if (scale > 512.0f) {
            scale = 512.0f;
        }

        for (u32 i = 0; i < sample->numSamples; i++) {
            f32 value = (f32)sample->pcm[i] * scale;
            s32 scaled = (s32)(value >= 0.0f ? value + 0.5f : value - 0.5f);
            if (scaled > 32767) scaled = 32767;
            if (scaled < -32768) scaled = -32768;
            sample->pcm[i] = (s16)scaled;
        }
    }
}

/* ===== N64 Bank File Parser =====
 *
 * Reads the big-endian .ctl binary data and extracts all sound samples.
 * For each ADPCM sound, decodes to PCM and stores in s_sfxSamples[].
 *
 * N64 .ctl binary layout (all values big-endian):
 *
 *   ALBankFile:
 *     +0x00: s16 revision
 *     +0x02: s16 bankCount
 *     +0x04: s32 bankOffsets[bankCount]  (offsets from start of .ctl)
 *
 *   ALBank (at bankOffset):
 *     +0x00: s16 instCount
 *     +0x02: u8 flags, u8 pad
 *     +0x04: s32 sampleRate
 *     +0x08: s32 percussionOffset (0 = none)
 *     +0x0C: s32 instOffsets[instCount]
 *
 *   ALInstrument (at instOffset):
 *     +0x00: u8 volume, pan, priority, flags
 *     +0x04: u8 tremType, tremRate, tremDepth, tremDelay
 *     +0x08: u8 vibType, vibRate, vibDepth, vibDelay
 *     +0x0C: s16 bendRange
 *     +0x0E: s16 soundCount
 *     +0x10: s32 soundOffsets[soundCount]
 *
 *   ALSound (at soundOffset):
 *     +0x00: s32 envelopeOffset
 *     +0x04: s32 keyMapOffset
 *     +0x08: s32 wavetableOffset
 *     +0x0C: u8 samplePan, sampleVolume, flags, pad
 *
 *   ALWaveTable (at wavetableOffset):
 *     +0x00: s32 base (offset into .tbl)
 *     +0x04: s32 len (ADPCM data length in bytes)
 *     +0x08: u8 type (0=ADPCM, 1=RAW16), u8 flags, u16 pad
 *     For ADPCM:
 *       +0x0C: s32 loopOffset (0=none)
 *       +0x10: s32 bookOffset
 *
 *   ALADPCMBook (at bookOffset):
 *     +0x00: s32 order
 *     +0x04: s32 npredictors
 *     +0x08: s16 book[order * npredictors * 8]
 *
 *   ALADPCMloop (at loopOffset):
 *     +0x00: s32 start, s32 end, s32 count
 *     +0x0C: s16 state[16]
 */
static s32 s_sfxBankParsed = 0;

void portAudioParseBankFile(u8 *ctlData, u32 ctlSize, u32 tblRomOffset)
{
    if (!ctlData || ctlSize < 8) return;
    if (g_romSize == 0) {
        printf("[AUDIO] No ROM data, cannot parse bank\n");
        return;
    }

    /* First call parses the SFX bank, second call parses the instrument bank. */
    if (s_sfxBankParsed) {
        portAudioParseInstrumentBank(ctlData, ctlSize, tblRomOffset);
        return;
    }

    /* ALBankFile header */
    s16 bankCount = rd_bes16(ctlData + 2);
    if (bankCount <= 0 || bankCount > 16) {
        printf("[AUDIO] Invalid bankCount=%d, skipping\n", bankCount);
        return;
    }

    /* Get first bank offset */
    u32 bankOff = rd_be32(ctlData + 4);
    /* ALBank header is 0x10 bytes (instCount@0, sampleRate@4, instruments@0xC);
     * a start-only `bankOff >= ctlSize` check lets a bank near the end of a
     * truncated/corrupt ctlData buffer read past it once we dereference
     * `bank + 0x0C` below. */
    if (!bank_ctl_has(ctlSize, bankOff, 0x10)) return;

    const u8 *bank = ctlData + bankOff;

    /* ALBank header */
    s16 instCount = rd_bes16(bank + 0);
    u32 sampleRate = rd_be32(bank + 4);
    s_sfxBankSampleRate = sampleRate;

    printf("[AUDIO] SFX bank: %d instruments, sampleRate=%u\n",
           instCount, sampleRate);

    if (instCount <= 0) return;

    /* Get first instrument offset */
    u32 instOff = rd_be32(bank + 0x0C);
    /* ALInstrument header used here is 0x10 bytes (soundCount@0xE); the
     * soundOffsets[] array beyond it is bounds-checked per-element below. */
    if (!bank_ctl_has(ctlSize, instOff, 0x10)) return;

    const u8 *inst = ctlData + instOff;

    /* ALInstrument header */
    s16 soundCount = rd_bes16(inst + 0x0E);
    printf("[AUDIO] First instrument: %d sounds\n", soundCount);

    if (soundCount <= 0 || soundCount > PORT_MAX_SOUNDS) {
        soundCount = (soundCount > PORT_MAX_SOUNDS) ? PORT_MAX_SOUNDS : 0;
    }

    s_sfxSampleCount = soundCount;

    /* Parse each sound */
    s32 decoded_count = 0;
    for (s32 si = 0; si < soundCount; si++) {
        /* Bounds-check the soundOffsets[si] slot itself (not just the value
         * it holds) before reading it -- a large soundCount with instOff near
         * ctlSize would otherwise read past ctlData. */
        if (!bank_ctl_has(ctlSize, instOff, 0x10 + (u32)(si + 1) * 4)) {
            printf("[AUDIO]   Sound %d: soundOffsets[] entry out of bounds\n", si);
            break;
        }
        u32 soundOff = rd_be32(inst + 0x10 + si * 4);
        /* ALSound fields used here span [0x08, 0x0E) (wtOff, pan, vol). */
        if (!bank_ctl_has(ctlSize, soundOff, 0x0E)) {
            printf("[AUDIO]   Sound %d: invalid offset 0x%x\n", si, soundOff);
            continue;
        }

        const u8 *snd = ctlData + soundOff;

        /* ALSound fields */
        u32 wtOff = rd_be32(snd + 0x08);
        u8  sndPan = snd[0x0C];
        u8  sndVol = snd[0x0D];
        (void)sndPan; (void)sndVol;

        /* ALWaveTable fields used here span [0x00, 0x14) (base, len, type,
         * loopOffset, bookOffset). */
        if (!bank_ctl_has(ctlSize, wtOff, 0x14)) continue;

        const u8 *wt = ctlData + wtOff;

        /* ALWaveTable fields */
        u32 wtBase = rd_be32(wt + 0x00);  /* offset into .tbl */
        s32 wtLen  = rd_bes32(wt + 0x04); /* ADPCM data length */
        u8  wtType = wt[0x08];

        if (wtLen <= 0) continue;

        /* Compute absolute ROM offset of sample data. Overflow-safe idiom:
         * a naive `sampleRomAddr + (u32)wtLen > g_romSize` wraps when
         * sampleRomAddr/wtLen (file-derived, corrupt-file-controlled) are
         * near UINT32_MAX, silently passing an out-of-ROM-bounds address. */
        u32 sampleRomAddr = tblRomOffset + wtBase;
        if (!bank_ctl_has(g_romSize, sampleRomAddr, (u32)wtLen)) {
            printf("[AUDIO]   Sound %d: sample data out of ROM bounds\n", si);
            continue;
        }

        PortDecodedSample *ds = &s_sfxSamples[si];
        ds->sampleRate = sampleRate;
        ds->hasLoop = 0;

        if (wtType == AL_ADPCM_WAVE) {
            /* Get ADPCM book: order/npredictors header is 8 bytes. */
            u32 bookOff = rd_be32(wt + 0x10);
            if (!bank_ctl_has(ctlSize, bookOff, 8)) continue;

            const u8 *bookData = ctlData + bookOff;
            s32 order = rd_bes32(bookData + 0x00);
            s32 npredictors = rd_bes32(bookData + 0x04);

            if (order <= 0 || order > 16 || npredictors <= 0 || npredictors > 16) {
                continue;
            }

            /* Read book coefficients (big-endian s16 array). order/npredictors
             * are clamped to <=16 above, so bookSize is bounded (<=2048) and
             * this multiply/shift can't overflow -- but the array itself must
             * still fit inside ctlData. */
            s32 bookSize = order * npredictors * 8;
            if (!bank_ctl_has(ctlSize, bookOff, 8 + (u32)bookSize * 2)) continue;
#ifdef PROJECT_TITAN_TGR
            ds->bookData = bookData;
            ds->bookOrder = (s16)order;
            ds->bookPredictors = (s16)npredictors;
#else
            s16 *bookCoefs = (s16 *)malloc(bookSize * sizeof(s16));
            if (!bookCoefs) continue;

            for (s32 i = 0; i < bookSize; i++) {
                bookCoefs[i] = rd_bes16(bookData + 8 + i * 2);
            }
#endif

            /* Check for loop: ALADPCMloop start/end/count header is 0xC bytes. */
            u32 loopOff = rd_be32(wt + 0x0C);
            if (loopOff != 0 && bank_ctl_has(ctlSize, loopOff, 0x0C)) {
                const u8 *loopData = ctlData + loopOff;
                ds->loopStart = rd_be32(loopData + 0x00);
                ds->loopEnd   = rd_be32(loopData + 0x04);
                u32 loopCount = rd_be32(loopData + 0x08);
                ds->hasLoop = (loopCount > 0) ? 1 : 0;
            }

            /* Desktop predecodes the whole bank. Titan records source metadata
             * and decodes on first use into a bounded LRU cache. */
            s32 maxSamples = (wtLen / VADPCM_FRAME_BYTES) * VADPCM_FRAME_SAMPLES;
#ifdef PROJECT_TITAN_TGR
            ds->romOffset = sampleRomAddr;
            ds->encodedBytes = (u32)wtLen;
            ds->waveType = AL_ADPCM_WAVE;
            ds->numSamples = (u32)maxSamples;
            decoded_count++;
#else
            ds->pcm = (s16 *)malloc(maxSamples * sizeof(s16));
            if (!ds->pcm) {
                free(bookCoefs);
                continue;
            }

            u8 *sampleData = (u8 *)malloc((u32)wtLen);
            if (!sampleData ||
                !platformRomRead(sampleRomAddr, sampleData, (u32)wtLen)) {
                free(sampleData);
                free(ds->pcm);
                ds->pcm = NULL;
                free(bookCoefs);
                continue;
            }

            /* Decode VADPCM → PCM */
            s32 outCount = 0;
            vadpcm_decode(sampleData, wtLen, bookCoefs, order, npredictors,
                          ds->pcm, &outCount);
            ds->numSamples = (u32)outCount;

            free(bookCoefs);
            free(sampleData);
            decoded_count++;
#endif

        } else if (wtType == AL_RAW16_WAVE) {
            /* RAW16: big-endian s16 samples */
            s32 numSamples = wtLen / 2;
#ifdef PROJECT_TITAN_TGR
            ds->romOffset = sampleRomAddr;
            ds->encodedBytes = (u32)wtLen;
            ds->waveType = AL_RAW16_WAVE;
            ds->numSamples = (u32)numSamples;
#else
            ds->pcm = (s16 *)malloc(numSamples * sizeof(s16));
            if (!ds->pcm) continue;

            u8 *sampleData = (u8 *)malloc((u32)wtLen);
            if (!sampleData ||
                !platformRomRead(sampleRomAddr, sampleData, (u32)wtLen)) {
                free(sampleData);
                free(ds->pcm);
                ds->pcm = NULL;
                continue;
            }

            for (s32 i = 0; i < numSamples; i++) {
                ds->pcm[i] = rd_bes16(sampleData + i * 2);
            }
            ds->numSamples = (u32)numSamples;
            if (!portAudioAuthoredMixEnabled()) {
                portAudioNormalizeDecodedSample(ds);
            }
#endif

            /* Check for loop */
            u32 loopOff = rd_be32(wt + 0x0C);
            if (loopOff != 0 && bank_ctl_has(ctlSize, loopOff, 0x0C)) {
                const u8 *loopData = ctlData + loopOff;
                ds->loopStart = rd_be32(loopData + 0x00);
                ds->loopEnd   = rd_be32(loopData + 0x04);
                u32 loopCount = rd_be32(loopData + 0x08);
                ds->hasLoop = (loopCount > 0) ? 1 : 0;
            }

            decoded_count++;
#ifndef PROJECT_TITAN_TGR
            free(sampleData);
#endif
        }
    }

    printf("[AUDIO] Decoded %d / %d SFX samples (rate=%u)\n",
           decoded_count, soundCount, sampleRate);

    s_sfxBankParsed = 1;
}

#ifdef PROJECT_TITAN_TGR
static int portTitanSfxSampleActive(const PortDecodedSample *sample)
{
    s32 index;
    for (index = 0; index < PORT_MAX_VOICES; index++) {
        if (s_voices[index].active && s_voices[index].sample == sample) return 1;
    }
    return 0;
}

static int portTitanEnsureSfxDecoded(s32 soundIndex)
{
    PortDecodedSample *sample;
    u32 pcmBytes;
    u8 *encoded = NULL;
    s16 *pcm = NULL;

    if (soundIndex < 0 || soundIndex >= s_sfxSampleCount) return 0;
    sample = &s_sfxSamples[soundIndex];
    sample->cacheStamp = ++s_titanSfxCacheStamp;
    if (sample->pcm != NULL) return 1;
    if (sample->encodedBytes == 0u || sample->numSamples == 0u ||
        sample->numSamples > 0x7fffffffu / sizeof(s16)) return 0;
    pcmBytes = sample->numSamples * (u32)sizeof(s16);
    if (pcmBytes > PORT_TITAN_SFX_CACHE_BYTES) return 0;

    while (s_titanSfxCacheBytes + pcmBytes > PORT_TITAN_SFX_CACHE_BYTES) {
        PortDecodedSample *victim = NULL;
        s32 index;
        for (index = 0; index < s_sfxSampleCount; index++) {
            PortDecodedSample *candidate = &s_sfxSamples[index];
            if (candidate->pcm == NULL || candidate == sample ||
                portTitanSfxSampleActive(candidate)) continue;
            if (victim == NULL || candidate->cacheStamp < victim->cacheStamp)
                victim = candidate;
        }
        if (victim == NULL) return 0;
        free(victim->pcm);
        victim->pcm = NULL;
        if (victim->cacheBytes <= s_titanSfxCacheBytes)
            s_titanSfxCacheBytes -= victim->cacheBytes;
        else
            s_titanSfxCacheBytes = 0u;
        victim->cacheBytes = 0u;
    }

    pcm = (s16 *)malloc(pcmBytes);
    encoded = (u8 *)malloc(sample->encodedBytes);
    if (pcm == NULL || encoded == NULL ||
        !platformRomRead(sample->romOffset, encoded, sample->encodedBytes))
        goto fail;

    if (sample->waveType == AL_ADPCM_WAVE) {
        s16 book[16 * 16 * 8];
        s32 bookCount = sample->bookOrder * sample->bookPredictors * 8;
        s32 outCount = 0;
        s32 index;
        if (sample->bookData == NULL || bookCount <= 0 ||
            bookCount > (s32)(sizeof(book) / sizeof(book[0]))) goto fail;
        for (index = 0; index < bookCount; index++)
            book[index] = rd_bes16(sample->bookData + 8 + index * 2);
        vadpcm_decode(encoded, (s32)sample->encodedBytes, book,
                      sample->bookOrder, sample->bookPredictors,
                      pcm, &outCount);
        if (outCount <= 0 || (u32)outCount > sample->numSamples) goto fail;
        sample->numSamples = (u32)outCount;
        pcmBytes = sample->numSamples * (u32)sizeof(s16);
    } else if (sample->waveType == AL_RAW16_WAVE) {
        u32 index;
        for (index = 0u; index < sample->numSamples; index++)
            pcm[index] = rd_bes16(encoded + index * 2u);
    } else {
        goto fail;
    }

    free(encoded);
    sample->pcm = pcm;
    sample->cacheBytes = pcmBytes;
    s_titanSfxCacheBytes += pcmBytes;
    if (sample->waveType == AL_RAW16_WAVE && !portAudioAuthoredMixEnabled())
        portAudioNormalizeDecodedSample(sample);
    return 1;

fail:
    free(encoded);
    free(pcm);
    return 0;
}
#endif

/* ===== SDL2 mixing helpers =====
 *
 * NOTE (WEB-047): audio runs in QUEUE mode (want.callback == NULL) — the engine
 * generates each frame's PCM synchronously (portAudioFrame → osAiSetNextBuffer →
 * SDL_QueueAudio), and there is NO SDL audio-callback thread. The old
 * audioCallback()/musicMixSamples() software mixer that fed a pull callback was
 * unreachable and has been removed (audioCallback) / #if 0'd (musicMixSamples)
 * so it can't mislead Asyncify-reentrancy reasoning. The live SFX mixer
 * (portAudioMixActiveVoices) is still used by the queue path.
 */
static void portAudioSetVoicePanLocked(PortVoice *voice, f32 pan)
{
    f32 panF;

    if (!voice) {
        return;
    }

    panF = pan / 127.0f;
    if (panF < 0.0f) panF = 0.0f;
    if (panF > 1.0f) panF = 1.0f;

    voice->panL = 1.0f - panF;
    voice->panR = panF;
}

static void portAudioStartGainRampLocked(PortVoice *voice, f32 volume, s32 rampSamples)
{
    if (!voice) {
        return;
    }

    volume = portAudioClamp01(volume);

    if (rampSamples > 0) {
        voice->gainTarget = volume;
        voice->gainStep = (volume - voice->gainCurrent) / (f32)rampSamples;
        voice->gainSamplesRemaining = rampSamples;
    } else {
        voice->gainCurrent = volume;
        voice->gainTarget = volume;
        voice->gainStep = 0.0f;
        voice->gainSamplesRemaining = 0;
    }
}

static void portAudioSetVoiceMixLocked(PortVoice *voice, f32 volume, f32 pan)
{
    if (!voice) {
        return;
    }

    portAudioSetVoicePanLocked(voice, pan);
    portAudioStartGainRampLocked(voice, volume, 0);
}

static void portAudioStartDecayLocked(PortVoice *voice)
{
    if (!voice) {
        return;
    }

    voice->envPhase = PORT_ENV_PHASE_DECAY;

    if (voice->envDecaySamples > 0) {
        voice->envTarget = voice->envDecayTarget;
        voice->envStep = (voice->envDecayTarget - voice->envCurrent) / (f32)voice->envDecaySamples;
        voice->envSamplesRemaining = voice->envDecaySamples;
    } else {
        voice->envCurrent = voice->envDecayTarget;
        voice->envTarget = voice->envDecayTarget;
        voice->envStep = 0.0f;
        voice->envSamplesRemaining = 0;
        voice->envPhase = PORT_ENV_PHASE_HOLD;
    }
}

static void portAudioSetVoiceEnvelopeLocked(PortVoice *voice, const PortSfxPlayParams *params)
{
    if (!voice) {
        return;
    }

    voice->envEnabled = 0;
    voice->envPhase = PORT_ENV_PHASE_HOLD;
    voice->envCurrent = 1.0f;
    voice->envTarget = 1.0f;
    voice->envStep = 0.0f;
    voice->envSamplesRemaining = 0;
    voice->envDecayTarget = 1.0f;
    voice->envDecaySamples = 0;

    if (!params || !params->useEnvelope) {
        return;
    }

    voice->envEnabled = 1;
    voice->envDecayTarget = portAudioClamp01((f32)params->decayVolume / 127.0f);
    voice->envDecaySamples = portAudioMicrosecondsToSamples(params->decayTime);

    {
        f32 attackTarget = portAudioClamp01((f32)params->attackVolume / 127.0f);
        s32 attackSamples = portAudioMicrosecondsToSamples(params->attackTime);

        if (attackSamples > 0) {
            voice->envCurrent = 0.0f;
            voice->envTarget = attackTarget;
            voice->envStep = attackTarget / (f32)attackSamples;
            voice->envSamplesRemaining = attackSamples;
            voice->envPhase = PORT_ENV_PHASE_ATTACK;
        } else {
            voice->envCurrent = attackTarget;
            voice->envTarget = attackTarget;
            voice->envStep = 0.0f;
            voice->envSamplesRemaining = 0;
            portAudioStartDecayLocked(voice);
        }
    }
}

static void portAudioAdvanceRampsLocked(PortVoice *voice)
{
    if (!voice) {
        return;
    }

    if (voice->gainSamplesRemaining > 0) {
        voice->gainCurrent += voice->gainStep;
        voice->gainSamplesRemaining--;
        if (voice->gainSamplesRemaining <= 0) {
            voice->gainCurrent = voice->gainTarget;
            voice->gainStep = 0.0f;
            voice->gainSamplesRemaining = 0;
            if (voice->stopOnGainEnd && voice->gainTarget <= 0.0f) {
                voice->active = 0;
                voice->stopOnGainEnd = 0;
                s_sfxMixStats.voiceStops++;
                return;
            }
        }
    }

    if (voice->envEnabled && voice->envSamplesRemaining > 0) {
        voice->envCurrent += voice->envStep;
        voice->envSamplesRemaining--;
        if (voice->envSamplesRemaining <= 0) {
            voice->envCurrent = voice->envTarget;
            voice->envStep = 0.0f;
            voice->envSamplesRemaining = 0;

            if (voice->envPhase == PORT_ENV_PHASE_ATTACK) {
                portAudioStartDecayLocked(voice);
            } else {
                voice->envPhase = PORT_ENV_PHASE_HOLD;
            }
        }
    }
}

static void portAudioComputeSpatial(f32 worldX, f32 worldY, f32 worldZ,
                                    f32 *attenOut, f32 *panOut)
{
    extern float g_pcCamX, g_pcCamY, g_pcCamZ, g_pcCamYaw;
    f32 dx = worldX - g_pcCamX;
    f32 dy = worldY - g_pcCamY;
    f32 dz = worldZ - g_pcCamZ;
    f32 dist = sqrtf(dx * dx + dy * dy + dz * dz);
    f32 atten = 1.0f;
    f32 panF = 64.0f;

    if (dist > 500.0f) {
        atten = 1.0f - (dist - 500.0f) / 14500.0f;
        if (atten < 0.0f) atten = 0.0f;
    }

    if (dist > 1.0f) {
        f32 sinYaw = sinf(g_pcCamYaw);
        f32 cosYaw = cosf(g_pcCamYaw);
        f32 rightDot = dx * cosYaw + dz * (-sinYaw);

        panF = 64.0f + (rightDot / dist) * 63.0f;
        if (panF < 0.0f) panF = 0.0f;
        if (panF > 127.0f) panF = 127.0f;
    }

    if (attenOut) {
        *attenOut = atten;
    }
    if (panOut) {
        *panOut = panF;
    }
}

void portAudioComputeSpatialMix(f32 worldX, f32 worldY, f32 worldZ,
                                f32 *attenOut, f32 *panOut)
{
    portAudioComputeSpatial(worldX, worldY, worldZ, attenOut, panOut);
}

static void portAudioMixActiveVoices(s16 *out, s32 numSamples, PortSfxFrameMixStats *stats)
{
    if (s_sfxMixLegacy < 0) {
        const char *e = getenv("GE007_SFX_MIX_LEGACY");
        s_sfxMixLegacy = (e != NULL && e[0] == '1') ? 1 : 0;
    }
    for (s32 v = 0; v < PORT_MAX_VOICES; v++) {
        PortVoice *voice = &s_voices[v];
        if (!voice->active || !voice->sample || !voice->sample->pcm) {
            continue;
        }

        PortDecodedSample *smp = voice->sample;
        s32 startSample = 0;

        if (stats != NULL) {
            stats->activeVoices++;
        }

        if (voice->delaySamples > 0) {
            if (voice->delaySamples >= numSamples) {
                voice->delaySamples -= numSamples;
                continue;
            }

            startSample = voice->delaySamples;
            voice->delaySamples = 0;
        }

        for (s32 i = startSample; i < numSamples; i++) {
            f32 gain;
            f32 sample;
            s32 beforeLeft;
            s32 beforeRight;
            s32 left;
            s32 right;

            /* §2.3/2.4: authored-loop-window wrap + linear interpolation.
             * GE007_SFX_MIX_LEGACY=1 restores the old nearest-neighbor /
             * end-of-buffer wrap for A/B. The volume multiply stays
             * * s_masterVolume, matching W6.E3.T1's landed SFX-bus semantics
             * (this fix changes resampling/looping, not gain). For a pitch==1.0
             * one-shot the new path yields frac==0 => sample==pcm[pos], i.e.
             * byte-identical to legacy; only looping/pitched SFX change. */
            if (s_sfxMixLegacy) {
                u32 pos = (u32)voice->position;
                if (pos >= smp->numSamples) {
                    if (smp->hasLoop && smp->loopEnd > smp->loopStart) {
                        voice->position = (f32)smp->loopStart;
                        pos = smp->loopStart;
                    } else {
                        voice->active = 0;
                        voice->stopOnGainEnd = 0;
                        s_sfxMixStats.voiceStops++;
                        break;
                    }
                }
                gain = voice->gainCurrent;
                if (voice->envEnabled) {
                    gain *= voice->envCurrent;
                }
                sample = (f32)smp->pcm[pos] * s_masterVolume * gain;
            } else {
                /* Authored loop window [loopStart, wrapEnd). The synth treats
                 * loopEnd as the first sample NOT played before jumping to
                 * loopStart (audio_compat.c:4276 loops when sample > loop.end and
                 * plays up to loop.end => EXCLUSIVE). Clamp to the decoded buffer
                 * and reject degenerate windows. */
                u32 wrapEnd = smp->numSamples;
                u32 loopStart = 0;
                s32 looping = 0;
                if (smp->hasLoop && smp->loopEnd > smp->loopStart &&
                    smp->loopStart < smp->numSamples) {
                    looping = 1;
                    loopStart = smp->loopStart;
                    wrapEnd = (smp->loopEnd < smp->numSamples) ? smp->loopEnd
                                                               : smp->numSamples;
                }

                /* Wrap at the authored point, PRESERVING the fractional phase. */
                if (voice->position >= (f32)wrapEnd) {
                    if (looping) {
                        f32 span = (f32)(wrapEnd - loopStart);
                        do { voice->position -= span; } while (voice->position >= (f32)wrapEnd);
                        if (voice->position < (f32)loopStart) {
                            voice->position = (f32)loopStart;   /* pitch > span safety */
                        }
                    } else {
                        voice->active = 0;
                        voice->stopOnGainEnd = 0;
                        s_sfxMixStats.voiceStops++;
                        break;
                    }
                }

                u32 pos = (u32)voice->position;
                f32 frac = voice->position - (f32)pos;

                gain = voice->gainCurrent;
                if (voice->envEnabled) {
                    gain *= voice->envCurrent;
                }

                /* Linear interpolation with correct edge handling: the neighbor of
                 * the last in-loop sample is loopStart; past the end of a one-shot
                 * it is the last sample (hold — never read past numSamples). */
                {
                    f32 s0 = (f32)smp->pcm[pos];
                    u32 nextPos = pos + 1;
                    f32 s1;
                    if (nextPos >= wrapEnd) {
                        s1 = looping ? (f32)smp->pcm[loopStart]
                                     : (f32)smp->pcm[wrapEnd - 1];
                    } else {
                        s1 = (f32)smp->pcm[nextPos];
                    }
                    sample = (s0 + (s1 - s0) * frac) * s_masterVolume * gain;
                }
            }
            beforeLeft = out[i * 2];
            beforeRight = out[i * 2 + 1];
            left  = beforeLeft + (s32)(sample * voice->panL);
            right = beforeRight + (s32)(sample * voice->panR);

            if (left > 32767) left = 32767;
            if (left < -32768) left = -32768;
            if (right > 32767) right = 32767;
            if (right < -32768) right = -32768;

            out[i * 2]     = (s16)left;
            out[i * 2 + 1] = (s16)right;

            if (stats != NULL) {
                s32 deltaLeft = left - beforeLeft;
                s32 deltaRight = right - beforeRight;

                if (deltaLeft < 0) {
                    deltaLeft = -deltaLeft;
                }
                if (deltaRight < 0) {
                    deltaRight = -deltaRight;
                }
                if ((u32)deltaLeft > stats->peakDelta) {
                    stats->peakDelta = (u32)deltaLeft;
                }
                if ((u32)deltaRight > stats->peakDelta) {
                    stats->peakDelta = (u32)deltaRight;
                }
                if (deltaLeft != 0 || deltaRight != 0) {
                    stats->sampleFramesMixed++;
                }
            }

            voice->position += voice->pitch;
            portAudioAdvanceRampsLocked(voice);
            if (!voice->active) {
                break;
            }
        }
    }
}

/* WEB-047: audioCallback() removed — it was the SDL pull-callback entry point,
 * dead since the port switched to queue mode (want.callback == NULL). Its live
 * half (portAudioMixActiveVoices) survives via the queue-side SFX mixer; its
 * music half (musicMixSamples) is the legacy CSP software mixer, now #if 0'd. */

/* Audio volume buses (W6.E3.T1, §4.3). Convert a 0..1 gain to Q15 (32768 =
 * unity). Consumers identity-bypass at >= 32768 so the default mix (all buses
 * 1.0) stays byte-identical. LIVE: the settings registry updates the backing
 * float in place, so each read here reflects the current value. */
static s32 portAudioVolumeToQ15(f32 v)
{
    s32 q;
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 32768;
    q = (s32)(v * 32768.0f + 0.5f);
    if (q < 0) q = 0;
    if (q > 32768) q = 32768;
    return q;
}

s32 portAudioGetMasterVolumeQ15(void)   { return portAudioVolumeToQ15(s_masterVolume); }
s32 portAudioGetMusicBusVolumeQ15(void) { return portAudioVolumeToQ15(s_musicBusVolume); }
s32 portAudioGetSfxBusVolumeQ15(void)   { return portAudioVolumeToQ15(s_sfxBusVolume); }

/* WEB-045: live soft-mute. portAudioSetMuted only moves the ramp target; the
 * ramp is applied per sample-frame by portAudioApplyMuteRamp, called from the AI
 * queue path (osAiSetNextBuffer) just before SDL_QueueAudio — the last point
 * before the device, so every queued buffer is governed. This replaces the old
 * SDL_PauseAudioDevice mute, which (a) clicked on the hard cut and (b) left up
 * to one full queue (~167 ms) of pre-mute audio buffered that replayed as a
 * stale burst on unmute. Keeping the device running drains that queue as
 * (ramped) silence, so nothing stale survives. */
void portAudioSetMuted(int muted)
{
    s_muteTargetQ15 = muted ? 0 : 32768;
}

/* PERF-060: live mute-target query (1 = output is headed to silence). Lets
 * audi_port.c's synth-skip re-check the CURRENT mute state each frame, so a
 * runtime unmute (F-key / GE007_AUTO_MUTE_TOGGLE) restores full synthesis. */
int portAudioIsMuted(void)
{
    return s_muteTargetQ15 == 0;
}

void portAudioApplyMuteRamp(s16 *samples, s32 sampleFrames)
{
    s32 gain   = s_muteGainQ15;
    s32 target = s_muteTargetQ15;
    s32 i;

    /* Settled at unity: byte-identical no-op (the common un-muted case, and the
     * only state a deterministic run — which never toggles mute — ever sees). */
    if (gain >= 32768 && target >= 32768) {
        return;
    }
    if (samples == NULL || sampleFrames <= 0) {
        return;
    }

    for (i = 0; i < sampleFrames; i++) {
        if (gain < target) {
            gain += PORT_MUTE_RAMP_STEP;
            if (gain > target) gain = target;
        } else if (gain > target) {
            gain -= PORT_MUTE_RAMP_STEP;
            if (gain < target) gain = target;
        }
        if (gain < 32768) {
            /* Pure attenuation (gain <= unity): result magnitude <= input, so no
             * s16 overflow — the cast is safe without a clamp. */
            samples[i * 2]     = (s16)(((s32)samples[i * 2]     * gain) >> 15);
            samples[i * 2 + 1] = (s16)(((s32)samples[i * 2 + 1] * gain) >> 15);
        }
    }
    s_muteGainQ15 = gain;
}

/* WEB-048: cache GE007_TRACE_WEAPON_AUDIO once (mirror s_sfxMixLegacy) instead
 * of a getenv() on every weapon-SFX play/mix in the per-frame path. */
static int portTraceWeaponAudio(void)
{
    if (s_traceWeaponAudio < 0) {
        s_traceWeaponAudio = (getenv("GE007_TRACE_WEAPON_AUDIO") != NULL) ? 1 : 0;
    }
    return s_traceWeaponAudio;
}

/* Register audio settings with the config system.
 * Called from main_pc.c before configInit(). */
/* ===== PERF-052: opt-in native audio-synthesis worker thread ========= *
 *
 * Default OFF. When Audio.SynthThread=1 on a NON-deterministic NATIVE run, the
 * heavy N64 music synth (alAudioFrame) is moved onto a dedicated worker thread,
 * one job in flight, joined once per frame by portAudioFrame (audi_port.c). The
 * worker overlaps the synth with the main thread's sim+render, so a GPU or
 * main-thread stall no longer starves the audio queue — at the cost of exactly
 * one frame of added audio latency (the pipeline drains the previous frame's
 * synth while kicking this frame's).
 *
 * Threading model: the port runs sim + render + the audio pump all on the main
 * thread; this worker is the only other thread that touches libaudio. The
 * reified osSetIntMask lock (stubs.c, gated by g_audioSynthLockActive) provides
 * the mutual exclusion the N64 interrupt mask originally did.
 *
 * The SDL_sem/SDL_mutex path is intentionally avoided here in favour of raw
 * pthreads: (a) it is native-only anyway (compiled out under __EMSCRIPTEN__),
 * (b) the mutex/cond handoff is what TSAN instruments natively, giving a clean
 * happens-before for the PCM-buffer hand-back with no false positives, and
 * (c) winpthread already links on the MinGW native build. The job body itself
 * (alAudioFrame + the libaudio low-pass) lives in audi_port.c
 * (portAudioSynthWorkerRunJob) so it can reach that file's static synth state;
 * this file owns only the thread + handshake. */
static s32 s_audioSynthThread = 0;   /* Audio.SynthThread: 0/1, default 0 */

#if !defined(__EMSCRIPTEN__) && !defined(PROJECT_TITAN_TGR)
#include <pthread.h>

extern int  g_deterministic;
extern int  g_audioSynthLockActive;                 /* stubs.c: reified-lock gate  */
extern void portSynthLockInit(void);                /* stubs.c: build recursive lock */
extern void portAudioSynthWorkerRunJob(s16 *buf, s32 frameSamples, int skip); /* audi_port.c */

enum { SYNTH_JOB_IDLE = 0, SYNTH_JOB_PENDING = 1, SYNTH_JOB_DONE = 2 };

static pthread_t       s_synthThread;
static pthread_mutex_t s_synthJobMtx;
static pthread_cond_t  s_synthJobCv;
static int             s_synthJobState   = SYNTH_JOB_IDLE;
static int             s_synthQuit       = 0;
static s16            *s_synthJobBuf     = NULL;
static s32             s_synthJobSamples = 0;
static int             s_synthJobSkip    = 0;
static int             s_synthWorkerOn   = 0;   /* latched: worker created & live */

/* PERF-052 main-thread-global isolation. The worker's synth subtree reaches two
 * MAIN-THREAD globals the reified osSetIntMask lock deliberately does not cover:
 *
 *  (a) the gfx_ptr registry — alAudioFrame's VESTIGIAL Acmd build goes through
 *      osVirtualToPhysical, whose gfx_ptr_store side-effect exists only for the
 *      renderer. The worker opts out via the gfx_ptr_store_suppress thread-local
 *      (gfx_ptr.h): set once below at thread start, so no worker store can race
 *      the render path's probes and the main thread's path is bit-identical.
 *
 *  (b) g_GlobalTimer — read only as the frame-stamp argument of audio trace
 *      lines (snd.c/audio_pc.c). The main thread snapshots it into
 *      g_sndSynthFrameStamp inside the kick's critical section (so the job
 *      mutex orders it against the timer's writer in lvl.c), and every trace
 *      site reads its stamp through the sndTraceFrameStamp()/
 *      portAudioTraceFrameStamp() expression: g_sndOnSynthWorker (thread-local,
 *      1 only on the worker) selects the snapshot there and the live
 *      g_GlobalTimer everywhere else. Stamp granularity is identical: the game
 *      timer cannot advance while the job it stamps is in flight (main joins
 *      before the next tick).
 *
 * SHAPE MATTERS (tape-gate lesson): these are deliberately EXPORTED VARIABLES
 * read inline, NOT accessor functions. An earlier draft routed the stamp
 * through a cross-TU call, and inserting a function call into the trace
 * argument lists changed the stack-frame codegen of the snd.c event-posting
 * functions — whose on-stack ALSndpEvent unions are memcpy'd WHOLE into the
 * event queue, unassigned bytes included. The committed tape baselines encode
 * those incidental bytes, so the call version deterministically flipped the
 * Dam sim hashes (audio->sim via the FID-0089 voice write-backs) while this
 * branch-over-plain-reads version leaves them byte-identical (verified 3x). */
__thread int g_sndOnSynthWorker = 0;  /* 1 only on the synth worker thread      */
s32 g_sndSynthFrameStamp = 0;         /* main writes at kick, under s_synthJobMtx;
                                       * read only on the worker (mutex-ordered) */

static void *portAudioSynthThreadMain(void *arg) {
    (void)arg;
    g_sndOnSynthWorker = 1;       /* (b) above: trace stamps read the snapshot  */
    gfx_ptr_store_suppress = 1;   /* (a) above: no registry writes from this thread */
    for (;;) {
        s16 *buf;
        s32  samples;
        int  skip;

        pthread_mutex_lock(&s_synthJobMtx);
        while (s_synthJobState != SYNTH_JOB_PENDING && !s_synthQuit) {
            pthread_cond_wait(&s_synthJobCv, &s_synthJobMtx);
        }
        if (s_synthQuit) {
            pthread_mutex_unlock(&s_synthJobMtx);
            break;
        }
        buf     = s_synthJobBuf;
        samples = s_synthJobSamples;
        skip    = s_synthJobSkip;
        pthread_mutex_unlock(&s_synthJobMtx);

        /* The heavy synth, off the main thread. Every libaudio critical section
         * it enters brackets through the reified osSetIntMask lock, so it is
         * mutually exclusive with the main thread's event posts / CSP controls. */
        portAudioSynthWorkerRunJob(buf, samples, skip);

        pthread_mutex_lock(&s_synthJobMtx);
        s_synthJobState = SYNTH_JOB_DONE;
        pthread_cond_signal(&s_synthJobCv);
        pthread_mutex_unlock(&s_synthJobMtx);
    }
    return NULL;
}

int portAudioSynthWorkerRunning(void) {
    return s_synthWorkerOn;
}

/* main thread: hand the just-sized buffer to the worker (non-blocking). */
void portAudioSynthWorkerKick(s16 *buf, s32 frameSamples, int skip) {
    pthread_mutex_lock(&s_synthJobMtx);
    s_synthJobBuf     = buf;
    s_synthJobSamples = frameSamples;
    s_synthJobSkip    = skip;
    /* Frame-stamp snapshot for the worker's trace lines — written under the job
     * mutex so the worker's reads are ordered against this write (and the game
     * timer's writer stays main-thread-only). */
    g_sndSynthFrameStamp = g_GlobalTimer;
    s_synthJobState   = SYNTH_JOB_PENDING;
    pthread_cond_signal(&s_synthJobCv);
    pthread_mutex_unlock(&s_synthJobMtx);
}

/* main thread: block until the previously-kicked job is fully synthesized. The
 * mutex/cond release-acquire publishes the worker's writes to the shared PCM
 * buffer back to the main thread, so the frame tail can safely read/mix it. */
void portAudioSynthWorkerJoin(void) {
    pthread_mutex_lock(&s_synthJobMtx);
    while (s_synthJobState != SYNTH_JOB_DONE) {
        pthread_cond_wait(&s_synthJobCv, &s_synthJobMtx);
    }
    s_synthJobState = SYNTH_JOB_IDLE;
    pthread_mutex_unlock(&s_synthJobMtx);
}

/* Called once at the end of a successful portAudioInit. Latches the (already
 * env/CLI/config-resolved) Audio.SynthThread and, when enabled on a
 * non-deterministic native run, constructs the recursive lock + worker BEFORE
 * arming g_audioSynthLockActive, so the flag is 1 only while the worker lives. */
void portAudioSynthWorkerStart(void) {
    if (s_synthWorkerOn) return;
    if (g_deterministic) return;      /* tape / sim-hash lanes never thread */
    if (!s_audioSynthThread) return;  /* opt-in, default off                */

    /* Force portAudioTraceSfxJson's lazy init (s_sfxTraceInit/Fp/Enabled) to
     * happen HERE, on the main thread, before the worker exists — after this
     * both threads only read those statics (and pthread_create publishes the
     * writes), so the first-trace-call lazy init can never be a cross-thread
     * write-write race. NULL fmt = init-only, no output. */
    portAudioTraceSfxJson(NULL);

    portSynthLockInit();
    pthread_mutex_init(&s_synthJobMtx, NULL);
    pthread_cond_init(&s_synthJobCv, NULL);
    s_synthJobState = SYNTH_JOB_IDLE;
    s_synthQuit = 0;

    if (pthread_create(&s_synthThread, NULL, portAudioSynthThreadMain, NULL) != 0) {
        printf("[AUDIO] PERF-052: synth worker create failed; staying single-threaded\n");
        pthread_mutex_destroy(&s_synthJobMtx);
        pthread_cond_destroy(&s_synthJobCv);
        return;
    }
    g_audioSynthLockActive = 1;   /* arm the reified lock AFTER the worker exists */
    s_synthWorkerOn = 1;
    printf("[AUDIO] PERF-052: native audio-synth worker ENABLED "
           "(+1 frame audio latency; absorbs GPU/main-thread stalls without gaps)\n");
}

/* Called at the top of portAudioShutdown. The worker finishes any in-flight job
 * (RunJob is uninterruptible), then observes the quit flag and exits; we join it
 * and only then clear the lock gate, so no thread can be mid-bracket when the
 * flag drops. */
void portAudioSynthWorkerStop(void) {
    if (!s_synthWorkerOn) return;

    pthread_mutex_lock(&s_synthJobMtx);
    s_synthQuit = 1;
    pthread_cond_signal(&s_synthJobCv);
    pthread_mutex_unlock(&s_synthJobMtx);
    pthread_join(s_synthThread, NULL);

    g_audioSynthLockActive = 0;   /* worker gone: brackets revert to no-op */
    s_synthWorkerOn = 0;
    pthread_mutex_destroy(&s_synthJobMtx);
    pthread_cond_destroy(&s_synthJobCv);
}
#else /* Web/Titan: synthesis stays cooperative on the foreground thread. */
int  portAudioSynthWorkerRunning(void) { return 0; }
void portAudioSynthWorkerKick(s16 *buf, s32 frameSamples, int skip) { (void)buf; (void)frameSamples; (void)skip; }
void portAudioSynthWorkerJoin(void) { }
void portAudioSynthWorkerStart(void) { }
void portAudioSynthWorkerStop(void) { }
/* No worker on web — the flag stays 0 forever, so every trace-stamp expression
 * (snd.c sndTraceFrameStamp / portAudioTraceFrameStamp below) reads the live
 * g_GlobalTimer, exactly as before PERF-052. */
#ifdef PROJECT_TITAN_TGR
int g_sndOnSynthWorker = 0;
#else
__thread int g_sndOnSynthWorker = 0;
#endif
s32 g_sndSynthFrameStamp = 0;
#endif

/* PERF-052: frame stamp for this file's own audio-trace lines. Identity
 * (g_GlobalTimer) everywhere except on the synth worker, where it reads the
 * kick-time snapshot instead of touching the main thread's live timer. Kept as
 * a branch over plain reads — see the SHAPE MATTERS note above. */
#define portAudioTraceFrameStamp() \
    (g_sndOnSynthWorker ? g_sndSynthFrameStamp : g_GlobalTimer)

void portAudioRegisterConfig(void)
{
    /* W6.E3.T1 Q1 (docs §9.1) option (a): Audio.MasterVolume default 0.7 -> 1.0.
     * The master scaler now actually applies to the final synth+SFX mix
     * (portAudioApplyMasterVolume, §4.3); unity default keeps output
     * byte-identical while making the knob functional. */
    settingsRegisterFloat("Audio.MasterVolume", &s_masterVolume, 1.0f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_MASTER_VOLUME",
                          "--config-override Audio.MasterVolume=VALUE",
                          "Master volume",
                          "Overall native audio output volume (scales the final music+SFX mix). 1.0 = unity.");
    settingsRegisterFloat("Audio.MusicVolume", &s_musicBusVolume, 1.0f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_MUSIC_VOLUME",
                          "--config-override Audio.MusicVolume=VALUE",
                          "Music volume",
                          "Music bus volume (scales the sequenced-music mix). 1.0 = unity.");
    settingsRegisterFloat("Audio.SfxVolume", &s_sfxBusVolume, 1.0f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_SFX_VOLUME",
                          "--config-override Audio.SfxVolume=VALUE",
                          "SFX volume",
                          "Sound-effects bus volume (scales snd-player SFX voices). 1.0 = unity.");
    settingsRegisterInt("Audio.DeviceSamples", &s_audioDeviceSamples, PORT_AUDIO_SAMPLES, 128, 2048,
                        SETTING_SCOPE_RESTART, "GE007_AUDIO_DEVICE_SAMPLES",
                        "--config-override Audio.DeviceSamples=VALUE",
                        "Audio device samples",
                        "SDL audio device buffer size in samples.");
    /* H1 output low-pass (docs/design/remaster-aaa/06-audio-remaster.md §4.2). Backing
     * storage lives in audi_port.c (the frame-pump consumer); registered here with
     * the rest of the Audio.* surface. Both LIVE (re-read each audio frame).
     * Default OFF + byte-identical at defaults (W6.E1.T2 ACC-ID gate). */
    settingsRegisterInt("Audio.OutputFilter", &g_portAudioOutputFilter, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_OUTPUT_FILTER",
                        "--config-override Audio.OutputFilter=VALUE",
                        "Output low-pass filter",
                        "One-pole DAC-coloration low-pass on the 22050 Hz synth bus (models N64 analog output). Default off.");
    settingsRegisterInt("Audio.OutputFilterAlpha", &g_portAudioOutputFilterAlpha,
                        PORT_AUDIO_OUTPUT_FILTER_ALPHA_DEFAULT, 1024, 32767,
                        SETTING_SCOPE_LIVE, "GE007_OUTPUT_FILTER_ALPHA",
                        "--config-override Audio.OutputFilterAlpha=VALUE",
                        "Output filter alpha",
                        "Q15 one-pole coefficient for Audio.OutputFilter (higher = brighter/less filtering).");
    /* PERF-010: audio-queue occupancy target for the non-deterministic frame-size
     * controller (audi_port.c). Backing storage lives in audi_port.c (the
     * consumer); registered here with the rest of the Audio.* surface. LIVE
     * (re-read each audio frame). Default 1.5 = the historical fixed target and
     * is byte-identical: (s32)(n*1.5f) == n + n/2 for all integer n.
     * Max is platform-aware: the equilibrium queue settles near the target, so
     * it must stay clear of the AI drop cap (stubs.c AI_QUEUE_LIMIT_FRAMES) —
     * 12 frames on web (room for the full range) but only 5 native, where a
     * target near 4 would trip routine drops. Keep >=2 frames of native headroom. */
#ifdef __EMSCRIPTEN__
    const f32 queueTargetMax = 4.0f;
#else
    const f32 queueTargetMax = 3.0f;
#endif
    settingsRegisterFloat("Audio.QueueTargetFrames", &g_portAudioQueueTargetFrames,
                          1.5f, 0.5f, queueTargetMax,
                          SETTING_SCOPE_LIVE, "GE007_AUDIO_QUEUE_TARGET",
                          "--config-override Audio.QueueTargetFrames=VALUE",
                          "Audio queue target",
                          "Target audio-queue occupancy in frames; higher = more stall absorption but more latency; 1.5 = default.");

    /* RX.1 settings curation: dev/diagnostic audio knobs hidden behind the
     * launcher's "Advanced (expert)" disclosure (env/CLI overrides unaffected).
     * DeviceSamples = latency-vs-glitch buffer tuning; OutputFilterAlpha = the
     * internal Q15 coefficient for the OutputFilter toggle (which stays
     * player-facing); QueueTargetFrames = the same latency-vs-absorption class
     * of low-level tuning as DeviceSamples. Volumes stay player-facing. */
    /* PERF-052: opt-in native audio-synthesis worker thread. RESTART scope —
     * latched once at portAudioInit (a mid-run change needs a restart). Default
     * 0 keeps the historical single-threaded synth (byte-identical). Native-only:
     * the worker is compiled out under __EMSCRIPTEN__, and it never engages under
     * --deterministic, so every tape/hash baseline is untouched by construction. */
    settingsRegisterInt("Audio.SynthThread", &s_audioSynthThread, 0, 0, 1,
                        SETTING_SCOPE_RESTART, "GE007_AUDIO_SYNTH_THREAD",
                        "--config-override Audio.SynthThread=VALUE",
                        "Audio synthesis worker thread",
                        "Native only. Run the N64 music synthesis on a background thread; "
                        "adds one frame of audio latency but absorbs GPU/main-thread stalls "
                        "without audio gaps. Default off; takes effect on restart.");

    settingsMarkAdvanced("Audio.DeviceSamples");
    settingsMarkAdvanced("Audio.OutputFilterAlpha");
    settingsMarkAdvanced("Audio.QueueTargetFrames");
    settingsMarkAdvanced("Audio.SynthThread");
}

int portAudioShouldStartMuted(void)
{
    const char *mute = getenv("GE007_MUTE");
    const char *unmute = getenv("GE007_UNMUTE");

    if (mute != NULL && mute[0] == '1' && mute[1] == '\0') {
        return 1;
    }

    if (unmute != NULL && unmute[0] == '0' && unmute[1] == '\0') {
        return 1;
    }

    return 0;
}

/* ===== Public API ===== */

void portAudioInit(void)
{
    if (s_audioInitialized) return;
    if (!portAudioEnvEnabled()) {
        if (!s_audioDisabledMsgShown) {
            printf("[AUDIO] Disabled via GE007_AUDIO=0. Unset GE007_AUDIO to re-enable.\n");
            s_audioDisabledMsgShown = 1;
        }
        return;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        printf("[AUDIO] SDL_InitSubSystem(AUDIO) failed: %s\n", SDL_GetError());
        return;
    }
    s_audioMutex = SDL_CreateMutex();
    if (!s_audioMutex) {
        printf("[AUDIO] Failed to create mutex\n");
        return;
    }

    int web_worklet_active = 0;
#ifdef __EMSCRIPTEN__
    /* WEB-069: prefer the off-main-thread AudioWorklet output backend. It moves
     * the audio drain onto the audio render thread (immune to main-thread
     * stalls), so the deep ScriptProcessorNode buffer below is no longer needed
     * and device-side latency drops from ~43 ms to one 128-sample render quantum.
     * If the browser has no AudioWorklet, webAudioOutputInit returns 0 and we
     * fall through to the legacy SDL ScriptProcessorNode path unchanged. */
    if (webAudioOutputInit(PORT_AUDIO_RATE, PORT_AUDIO_CHANNELS)) {
        web_worklet_active = 1;
        s_audioDevice = 0;             /* worklet owns output; no SDL device */
        s_audioDeviceBufferBytes = 0;  /* worklet reports its own occupancy */
        printf("[AUDIO] Web AudioWorklet output backend active (%d Hz, %d ch)\n",
               PORT_AUDIO_RATE, PORT_AUDIO_CHANNELS);
    } else {
        printf("[AUDIO] AudioWorklet unavailable; using SDL ScriptProcessorNode\n");
    }
#endif

    /* Snap the boot mute ramp for BOTH output backends: the soft-mute gain is
     * applied in the shared mix path, so the web AudioWorklet route must honor
     * GE007_MUTE/GE007_UNMUTE exactly like the SDL device route (test etiquette
     * depends on GE007_MUTE muting every backend). */
    {
        int start_muted = portAudioShouldStartMuted();
        s_muteTargetQ15 = start_muted ? 0 : 32768;
        s_muteGainQ15   = s_muteTargetQ15;
        if (start_muted) {
            printf("[AUDIO] Starting MUTED (clear GE007_MUTE or set GE007_UNMUTE=1 to enable on boot)\n");
        }
    }

    /* Single unified SDL audio device (queue mode, no callback).
     * All audio — both the libultra synthesis path (osAiSetNextBuffer) and
     * any future SFX paths — goes through this one device.
     * portAiInit() in stubs.c reuses this device via portAudioGetDevice().
     * Skipped entirely when the web AudioWorklet backend is active. */
    if (!web_worklet_active) {
        SDL_AudioSpec want, have;
        memset(&want, 0, sizeof(want));
        want.freq     = PORT_AUDIO_RATE;
        want.format   = AUDIO_S16SYS;
        want.channels = PORT_AUDIO_CHANNELS;
#ifdef __EMSCRIPTEN__
        /* WEB-012: an out-of-range or non-pow2 buffer makes the browser's
         * createScriptProcessor throw IndexSizeError, which surfaces as a
         * permanent "Boot failed" every visit (a persisted bad Audio.DeviceSamples
         * bricks the site until storage is cleared — unrecoverable by the user).
         * Snap the value to the nearest power of two in [256,2048] BEFORE
         * SDL_OpenAudioDevice so no such value ever reaches the SPN. Native paths
         * are untouched; only the web build has this brick failure mode. */
        {
            s32 req = s_audioDeviceSamples;
            s32 clamped = 256;
            while (clamped < 2048 && clamped * 2 <= req) clamped *= 2;
            if (clamped != s_audioDeviceSamples) {
                printf("[AUDIO] WEB-012: clamped Audio.DeviceSamples %d -> %d (pow2 in [256,2048])\n",
                       s_audioDeviceSamples, clamped);
                s_audioDeviceSamples = clamped;
            }
        }
#endif
        want.samples  = (u16)s_audioDeviceSamples;
        want.callback = NULL; /* queue mode */

        s_audioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (s_audioDevice == 0) {
            printf("[AUDIO] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
            return;
        }

        /* WEB-045: always START (unpause) the unified device; the initial mute
         * is expressed through the soft-mute ramp (snapped above, both
         * backends) instead of leaving the device paused. This keeps the
         * runtime M toggle symmetric — it only moves the ramp target and never
         * has to un-pause a device — and avoids the paused-queue →
         * stale-replay-on-first-unmute case. */
        SDL_PauseAudioDevice(s_audioDevice, 0);
#ifdef __EMSCRIPTEN__
        /* WEB-046: on web the SDL emscripten backend does NOT open a device at
         * this spec — it runs a main-thread ScriptProcessorNode at the browser
         * AudioContext's own native rate (typically 44.1/48 kHz) and SDL
         * resamples our stream into it. The figures below are the requested SDL
         * spec, not the hardware device. */
        printf("[AUDIO] Unified device: %d Hz, %d ch, %d buf (requested %d)\n"
               "[AUDIO] (web: browser AudioContext runs at its own native rate; "
               "SDL resamples this %d Hz stream via a main-thread "
               "ScriptProcessorNode — figures above are the requested spec)\n",
               have.freq, have.channels, have.samples, s_audioDeviceSamples,
               have.freq);
#else
        printf("[AUDIO] Unified device: %d Hz, %d ch, %d buf (requested %d)\n",
               have.freq, have.channels, have.samples, s_audioDeviceSamples);
#endif
        s_audioDeviceBufferBytes = (u32)(have.samples * have.channels * (s32)sizeof(s16));
    }

    memset(&s_sfxMixStats, 0, sizeof(s_sfxMixStats));
    s_audioInitialized = 1;
    { extern void portAiInit(void); portAiInit(); }

    /* PERF-052: latch Audio.SynthThread and bring up the synth worker (no-op
     * unless enabled + native + non-deterministic). */
    portAudioSynthWorkerStart();
}

SDL_AudioDeviceID portAudioGetDevice(void) {
    return s_audioDevice;
}

u32 portAudioGetDeviceBufferBytes(void) {
    return s_audioDeviceBufferBytes;
}

void portAudioShutdown(void)
{
    if (!s_audioInitialized) return;

    /* PERF-052: join the synth worker BEFORE tearing down the device/mixer it
     * feeds, so no in-flight synth writes into freed state. Clears the reified
     * lock gate once the worker is gone. No-op when the worker was never started. */
    portAudioSynthWorkerStop();

    if (s_audioDevice) SDL_CloseAudioDevice(s_audioDevice);
    if (s_audioMutex) SDL_DestroyMutex(s_audioMutex);

    /* Free decoded samples */
    for (s32 i = 0; i < PORT_MAX_SOUNDS; i++) {
        if (s_sfxSamples[i].pcm) {
            free(s_sfxSamples[i].pcm);
            s_sfxSamples[i].pcm = NULL;
        }
#ifdef PROJECT_TITAN_TGR
        s_sfxSamples[i].cacheBytes = 0u;
#endif
    }
#ifdef PROJECT_TITAN_TGR
    s_titanSfxCacheBytes = 0u;
    s_titanSfxCacheStamp = 0u;
#endif

    s_audioInitialized = 0;
}

/**
 * Play a pre-decoded SFX by bank-local sample index.
 * Returns the voice index (0..PORT_MAX_VOICES-1) or -1 on failure.
 */
static s32 portAudioPlaySoundDetailedInternal(s16 soundIndex,
                                              const PortSfxPlayParams *params)
{
    PortSfxPlayParams localParams;

    if (!s_audioInitialized) return -1;
    if (soundIndex < 0 || soundIndex >= s_sfxSampleCount) return -1;

    PortDecodedSample *smp = &s_sfxSamples[soundIndex];
#ifdef PROJECT_TITAN_TGR
    if (!portTitanEnsureSfxDecoded(soundIndex)) return -1;
#endif
    if (!smp->pcm || smp->numSamples == 0) return -1;

    memset(&localParams, 0, sizeof(localParams));
    localParams.volume = 1.0f;
    localParams.pan = AL_PAN_CENTER;
    localParams.pitch = 1.0f;
    if (params != NULL) {
        localParams = *params;
    }

    if (portTraceWeaponAudio()) {
        s32 peak = 0;
        u32 inspect = smp->numSamples < 4096 ? smp->numSamples : 4096;
        for (u32 i = 0; i < inspect; i++) {
            s32 v = smp->pcm[i];
            if (v < 0) {
                v = -v;
            }
            if (v > peak) {
                peak = v;
            }
        }
        printf("[SFX_SAMPLE] sound=%d samples=%u rate=%u peak=%d volume=%.6f pan=%.1f pitch=%.3f delay=%d fxMix=%u\n",
               soundIndex, smp->numSamples, smp->sampleRate, peak,
               (double)localParams.volume, (double)localParams.pan,
               (double)localParams.pitch, localParams.delaySamples,
               (unsigned int)localParams.fxMix);
        if (localParams.useEnvelope) {
            printf("[SFX_SAMPLE_AUTH] sound=%d volume=%.6f attackVol=%u decayVol=%u attackTime=%d decayTime=%d\n",
                   soundIndex,
                   (double)localParams.volume,
                   (unsigned int)localParams.attackVolume,
                   (unsigned int)localParams.decayVolume,
                   (int)localParams.attackTime,
                   (int)localParams.decayTime);
        }
    }

    SDL_LockMutex(s_audioMutex);

    /* Find a free voice */
    s32 voiceIdx = -1;
    for (s32 i = 0; i < PORT_MAX_VOICES; i++) {
        if (!s_voices[i].active) {
            voiceIdx = i;
            break;
        }
    }

    /* If no free voice, steal the oldest (voice 0 wraps) */
    if (voiceIdx < 0) {
        extern void sndStubInvalidateVoiceIndex(s32 voiceIdx);

        voiceIdx = 0;
        f32 maxPos = -1.0f;
        for (s32 i = 0; i < PORT_MAX_VOICES; i++) {
            if (s_voices[i].position > maxPos) {
                maxPos = s_voices[i].position;
                voiceIdx = i;
            }
        }

        sndStubInvalidateVoiceIndex(voiceIdx);
    }

    PortVoice *v = &s_voices[voiceIdx];
    v->sample   = smp;
    v->position = 0.0f;
    v->pitch    = localParams.pitch;
    v->delaySamples = localParams.delaySamples > 0 ? localParams.delaySamples : 0;
    v->fxMix = localParams.fxMix;
    v->active   = 1;
    v->stopOnGainEnd = 0;

    portAudioSetVoiceMixLocked(v, localParams.volume, localParams.pan);
    portAudioSetVoiceEnvelopeLocked(v, &localParams);
    s_sfxMixStats.voiceStarts++;

    SDL_UnlockMutex(s_audioMutex);

    s_lastPlayedVoice = voiceIdx;
    portAudioTraceSfxJson(
        "{\"event\":\"voice_start\",\"frame\":%d,\"voice\":%d,\"bank\":%d,\"volume\":%.6f,\"pan\":%.3f,\"pitch\":%.6f,"
        "\"delay_samples\":%d,\"fx_mix\":%u,\"use_envelope\":%d,\"attack_time\":%d,\"decay_time\":%d,"
        "\"attack_vol\":%u,\"decay_vol\":%u}",
        portAudioTraceFrameStamp(),
        voiceIdx,
        soundIndex,
        (double)localParams.volume,
        (double)localParams.pan,
        (double)localParams.pitch,
        localParams.delaySamples,
        (unsigned int)localParams.fxMix,
        (int)localParams.useEnvelope,
        (int)localParams.attackTime,
        (int)localParams.decayTime,
        (unsigned int)localParams.attackVolume,
        (unsigned int)localParams.decayVolume);
    return voiceIdx;
}

s32 portAudioPlaySfxDetailed(s16 soundIndex, const PortSfxPlayParams *params)
{
    return portAudioPlaySoundDetailedInternal(soundIndex, params);
}

s32 portAudioPlaySound(s16 soundIndex, f32 volume, f32 pan, f32 pitch)
{
    PortSfxPlayParams params;

    memset(&params, 0, sizeof(params));
    params.volume = volume;
    params.pan = pan;
    params.pitch = pitch;
    return portAudioPlaySoundDetailedInternal(soundIndex, &params);
}

s32 portAudioPlaySoundDelayed(s16 soundIndex, f32 volume, f32 pan, f32 pitch,
                              s32 delaySamples)
{
    PortSfxPlayParams params;

    memset(&params, 0, sizeof(params));
    params.volume = volume;
    params.pan = pan;
    params.pitch = pitch;
    params.delaySamples = delaySamples;
    return portAudioPlaySoundDetailedInternal(soundIndex, &params);
}

/* Temporary snd.c adapter:
 * keep submission API stable while we migrate all SFX scheduling to the
 * synth/CSP/AI-queue backend in libaudio.
 * TODO(audio-migration): delete this and have snd.c submit via ALSndPlayer. */
s32 portAudioSubmitSfx(s16 soundIndex, f32 volume, f32 pan, f32 pitch)
{
    return portAudioPlaySound(soundIndex, volume, pan, pitch);
}

void portAudioStopAll(void)
{
    if (!s_audioInitialized) return;

    SDL_LockMutex(s_audioMutex);
    for (s32 i = 0; i < PORT_MAX_VOICES; i++) {
        if (s_voices[i].active) {
            s_sfxMixStats.voiceStops++;
        }
        s_voices[i].active = 0;
        s_voices[i].stopOnGainEnd = 0;
    }
    SDL_UnlockMutex(s_audioMutex);
}

/**
 * Check if the audio system has been initialized and has samples loaded.
 */
s32 portAudioIsReady(void)
{
    return s_audioInitialized && s_sfxSampleCount > 0;
}

/**
 * Compute stereo pan and volume attenuation from a 3D world position
 * relative to the camera (listener). Returns pan in [0..127] range
 * and scales volume by distance attenuation.
 */
s32 portAudioPlaySoundAtPosition(s16 soundIndex, f32 volume, f32 pitch,
                                 f32 worldX, f32 worldY, f32 worldZ)
{
    f32 atten = 1.0f;
    f32 panF = 64.0f;

    portAudioComputeSpatial(worldX, worldY, worldZ, &atten, &panF);
    return portAudioPlaySound(soundIndex, volume * atten, panF, pitch);
}

s32 portAudioSubmitSfxAtPosition(s16 soundIndex, f32 volume, f32 pitch,
                                 f32 worldX, f32 worldY, f32 worldZ)
{
    return portAudioPlaySoundAtPosition(soundIndex, volume, pitch, worldX, worldY, worldZ);
}

/**
 * Retroactively apply spatial audio (distance attenuation + stereo pan)
 * to the most recently played SFX voice. Called from chrobjSndCreatePostEvent
 * after sndPlaySfx has already started the sound center-panned.
 *
 * This replaces the N64 approach where sndCreatePostEvent(state, 8, vol)
 * would post a volume change to an already-playing voice.
 */
void portAudioApplySpatialToLastVoice(f32 worldX, f32 worldY, f32 worldZ)
{
    s32 voiceIdx = s_lastPlayedVoice;
    if (voiceIdx < 0 || voiceIdx >= PORT_MAX_VOICES) return;
    if (!s_audioInitialized) return;

    SDL_LockMutex(s_audioMutex);

    PortVoice *v = &s_voices[voiceIdx];
    if (!v->active) {
        SDL_UnlockMutex(s_audioMutex);
        return;
    }

    f32 atten = 1.0f;
    f32 panF = 64.0f;
    f32 baseVol = v->gainTarget;

    portAudioComputeSpatial(worldX, worldY, worldZ, &atten, &panF);
    portAudioSetVoiceMixLocked(v, baseVol * atten, panF);

    SDL_UnlockMutex(s_audioMutex);
}

void portAudioMixSfxIntoBuffer(s16 *out, s32 numSamples)
{
    PortSfxFrameMixStats frameStats;

    if (!s_audioInitialized || !out || numSamples <= 0) {
        return;
    }

    memset(&frameStats, 0, sizeof(frameStats));

    SDL_LockMutex(s_audioMutex);
    portAudioMixActiveVoices(out, numSamples, &frameStats);

    s_sfxMixStats.mixCalls++;
    s_sfxMixStats.activeVoicesLast = frameStats.activeVoices;
    s_sfxMixStats.activeVoiceFrames += frameStats.activeVoices;
    s_sfxMixStats.sampleFramesMixed += frameStats.sampleFramesMixed;
    s_sfxMixStats.peakDeltaLast = frameStats.peakDelta;
    if (frameStats.peakDelta > s_sfxMixStats.peakDeltaMax) {
        s_sfxMixStats.peakDeltaMax = frameStats.peakDelta;
    }

    if (portTraceWeaponAudio()) {
        s32 maxVal = 0;
        for (s32 i = 0; i < numSamples * 2; i++) {
            s32 v = out[i];
            if (v < 0) {
                v = -v;
            }
            if (v > maxVal) {
                maxVal = v;
            }
        }
        if (maxVal > 0) {
            printf("[SFX_MIX] samples=%d peak=%d\n", numSamples, maxVal);
        }
    }

    SDL_UnlockMutex(s_audioMutex);
}

void portAudioStopVoice(s32 voiceIdx)
{
    if (!s_audioInitialized || voiceIdx < 0 || voiceIdx >= PORT_MAX_VOICES) {
        return;
    }

    SDL_LockMutex(s_audioMutex);
    if (s_voices[voiceIdx].active) {
        s_sfxMixStats.voiceStops++;
        s_voices[voiceIdx].active = 0;
        s_voices[voiceIdx].stopOnGainEnd = 0;
    }
    SDL_UnlockMutex(s_audioMutex);
    portAudioTraceSfxJson(
        "{\"event\":\"voice_stop\",\"frame\":%d,\"voice\":%d}",
        portAudioTraceFrameStamp(),
        voiceIdx);
}

void portAudioReleaseVoice(s32 voiceIdx, ALMicroTime releaseUsec)
{
    s32 releaseSamples;
    s32 wasActive = 0;

    if (!s_audioInitialized || voiceIdx < 0 || voiceIdx >= PORT_MAX_VOICES) {
        return;
    }

    releaseSamples = portAudioMicrosecondsToSamples(releaseUsec);

    SDL_LockMutex(s_audioMutex);
    if (s_voices[voiceIdx].active) {
        wasActive = 1;
        if (releaseSamples > 0) {
            portAudioStartGainRampLocked(&s_voices[voiceIdx], 0.0f, releaseSamples);
            s_voices[voiceIdx].stopOnGainEnd = 1;
        } else {
            s_sfxMixStats.voiceStops++;
            s_voices[voiceIdx].active = 0;
            s_voices[voiceIdx].stopOnGainEnd = 0;
        }
    }
    SDL_UnlockMutex(s_audioMutex);

    portAudioTraceSfxJson(
        "{\"event\":\"voice_release\",\"frame\":%d,\"voice\":%d,\"release_usec\":%d,\"release_samples\":%d,\"active\":%d}",
        portAudioTraceFrameStamp(),
        voiceIdx,
        (int)releaseUsec,
        releaseSamples,
        wasActive);
}

s32 portAudioVoiceIsActive(s32 voiceIdx)
{
    s32 active = 0;

    if (!s_audioInitialized || voiceIdx < 0 || voiceIdx >= PORT_MAX_VOICES) {
        return 0;
    }

    SDL_LockMutex(s_audioMutex);
    active = s_voices[voiceIdx].active;
    SDL_UnlockMutex(s_audioMutex);

    return active;
}

void portAudioUpdateVoiceMix(s32 voiceIdx, f32 volume, f32 pan)
{
    portAudioUpdateVoiceMixRamp(voiceIdx, volume, pan, 0);
}

void portAudioUpdateVoiceMixRamp(s32 voiceIdx, f32 volume, f32 pan,
                                 ALMicroTime rampUsec)
{
    s32 rampSamples;

    if (!s_audioInitialized || voiceIdx < 0 || voiceIdx >= PORT_MAX_VOICES) {
        return;
    }

    rampSamples = portAudioMicrosecondsToSamples(rampUsec);

    SDL_LockMutex(s_audioMutex);
    if (s_voices[voiceIdx].active) {
        portAudioSetVoicePanLocked(&s_voices[voiceIdx], pan);
        portAudioStartGainRampLocked(&s_voices[voiceIdx], volume, rampSamples);
    }
    SDL_UnlockMutex(s_audioMutex);
    portAudioTraceSfxJson(
        "{\"event\":\"voice_mix\",\"frame\":%d,\"voice\":%d,\"volume\":%.6f,\"pan\":%.3f,\"ramp_usec\":%d,\"ramp_samples\":%d}",
        portAudioTraceFrameStamp(),
        voiceIdx,
        (double)volume,
        (double)pan,
        (int)rampUsec,
        rampSamples);
}

void portAudioUpdateVoicePitch(s32 voiceIdx, f32 pitch)
{
    if (!s_audioInitialized || voiceIdx < 0 || voiceIdx >= PORT_MAX_VOICES) {
        return;
    }

    if (pitch < 0.01f) pitch = 0.01f;

    SDL_LockMutex(s_audioMutex);
    if (s_voices[voiceIdx].active) {
        s_voices[voiceIdx].pitch = pitch;
    }
    SDL_UnlockMutex(s_audioMutex);
    portAudioTraceSfxJson(
        "{\"event\":\"voice_pitch\",\"frame\":%d,\"voice\":%d,\"pitch\":%.6f}",
        portAudioTraceFrameStamp(),
        voiceIdx,
        (double)pitch);
}

void portAudioUpdateVoiceFxMix(s32 voiceIdx, u8 fxMix)
{
    if (!s_audioInitialized || voiceIdx < 0 || voiceIdx >= PORT_MAX_VOICES) {
        return;
    }

    SDL_LockMutex(s_audioMutex);
    if (s_voices[voiceIdx].active) {
        s_voices[voiceIdx].fxMix = fxMix;
    }
    SDL_UnlockMutex(s_audioMutex);
    portAudioTraceSfxJson(
        "{\"event\":\"voice_fxmix\",\"frame\":%d,\"voice\":%d,\"fx_mix\":%u}",
        portAudioTraceFrameStamp(),
        voiceIdx,
        (unsigned int)fxMix);
}

void portAudioUpdateVoiceSpatial(s32 voiceIdx, f32 volume,
                                 f32 worldX, f32 worldY, f32 worldZ)
{
    portAudioUpdateVoiceSpatialRamp(voiceIdx, volume, worldX, worldY, worldZ, 0);
}

void portAudioUpdateVoiceSpatialRamp(s32 voiceIdx, f32 volume,
                                     f32 worldX, f32 worldY, f32 worldZ,
                                     ALMicroTime rampUsec)
{
    f32 atten = 1.0f;
    f32 panF = 64.0f;
    s32 rampSamples;

    if (!s_audioInitialized || voiceIdx < 0 || voiceIdx >= PORT_MAX_VOICES) {
        return;
    }

    portAudioComputeSpatial(worldX, worldY, worldZ, &atten, &panF);
    rampSamples = portAudioMicrosecondsToSamples(rampUsec);

    SDL_LockMutex(s_audioMutex);
    if (s_voices[voiceIdx].active) {
        portAudioSetVoicePanLocked(&s_voices[voiceIdx], panF);
        portAudioStartGainRampLocked(&s_voices[voiceIdx], volume * atten, rampSamples);
    }
    SDL_UnlockMutex(s_audioMutex);
    portAudioTraceSfxJson(
        "{\"event\":\"voice_spatial\",\"frame\":%d,\"voice\":%d,\"base_volume\":%.6f,\"atten\":%.6f,\"final_volume\":%.6f,"
        "\"pan\":%.3f,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"ramp_usec\":%d,\"ramp_samples\":%d}",
        portAudioTraceFrameStamp(),
        voiceIdx,
        (double)volume,
        (double)atten,
        (double)(volume * atten),
        (double)panF,
        (double)worldX,
        (double)worldY,
        (double)worldZ,
        (int)rampUsec,
        rampSamples);
}

void portAudioUpdateVoicePanPositionRamp(s32 voiceIdx, f32 volume,
                                         f32 worldX, f32 worldY, f32 worldZ,
                                         ALMicroTime rampUsec)
{
    f32 atten = 1.0f;
    f32 panF = 64.0f;
    s32 rampSamples;

    if (!s_audioInitialized || voiceIdx < 0 || voiceIdx >= PORT_MAX_VOICES) {
        return;
    }

    portAudioComputeSpatial(worldX, worldY, worldZ, &atten, &panF);
    rampSamples = portAudioMicrosecondsToSamples(rampUsec);

    SDL_LockMutex(s_audioMutex);
    if (s_voices[voiceIdx].active) {
        portAudioSetVoicePanLocked(&s_voices[voiceIdx], panF);
        portAudioStartGainRampLocked(&s_voices[voiceIdx], volume, rampSamples);
    }
    SDL_UnlockMutex(s_audioMutex);
    portAudioTraceSfxJson(
        "{\"event\":\"voice_pan_position\",\"frame\":%d,\"voice\":%d,\"volume\":%.6f,\"ignored_atten\":%.6f,"
        "\"pan\":%.3f,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"ramp_usec\":%d,\"ramp_samples\":%d}",
        portAudioTraceFrameStamp(),
        voiceIdx,
        (double)volume,
        (double)atten,
        (double)panF,
        (double)worldX,
        (double)worldY,
        (double)worldZ,
        (int)rampUsec,
        rampSamples);
}

void portAudioGetSfxMixStats(PortSfxMixStats *stats)
{
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));

    if (s_audioInitialized && s_audioMutex != NULL) {
        SDL_LockMutex(s_audioMutex);
        *stats = s_sfxMixStats;
        SDL_UnlockMutex(s_audioMutex);
    } else {
        *stats = s_sfxMixStats;
    }
}

/* =============================================================
 * MUSIC SYSTEM
 *
 * Implements a CSP (Compressed Sequence Player) sequencer for
 * GoldenEye N64 music. Parses the instrument bank for note-to-sample
 * mapping, pre-parses CSP sequence data into a flat event array,
 * and plays notes through the SDL2 mixer with pitch shifting.
 * ============================================================= */

#define PORT_MAX_INSTRUMENTS     80
#define PORT_MAX_INST_SOUNDS     32
#define PORT_MAX_MUSIC_VOICES    24
#define PORT_MAX_SEQ_EVENTS      32768

/* Instrument sound with pitch mapping info */
typedef struct {
    PortDecodedSample sample;
    u8 keyBase;
    u8 keyMin;
    u8 keyMax;
    u8 velocityMin;
    u8 velocityMax;
    s8 detune;
    u8 samplePan;
    u8 sampleVolume;
} PortInstSound;

/* Single instrument (MIDI program) */
typedef struct {
    PortInstSound sounds[PORT_MAX_INST_SOUNDS];
    s32 soundCount;
} PortInstrument;

/* Pre-parsed sequencer event */
enum {
    PORT_EVT_NOTE_ON = 0,
    PORT_EVT_NOTE_OFF,
    PORT_EVT_TEMPO,
    PORT_EVT_PROGRAM,
    PORT_EVT_CC,       /* Control Change: byte1=controller, byte2=value */
    PORT_EVT_SEQ_END
};

typedef struct {
    u32 absoluteTick;
    u8  type;
    u8  channel;
    u8  byte1;     /* note / program / controller */
    u8  byte2;     /* velocity / value */
    u32 extra;     /* duration ticks (note_on) or tempo µs/QN (tempo) */
} PortSeqEvent;

/* Active music voice */
typedef struct {
    PortDecodedSample *sample;
    f32 position;
    f32 pitch;
    f32 volumeL;
    f32 volumeR;
    u32 endTick;    /* absolute tick at which note stops */
    u8  channel;
    u8  note;
    s32 active;
} PortMusicVoice;

/* ===== Music global state ===== */
static PortInstrument s_instruments[PORT_MAX_INSTRUMENTS];
static s32            s_instrumentCount;
static s32            s_instBankParsed;
static u32            s_instBankSampleRate;

static PortSeqEvent  *s_seqEvents;
static s32            s_seqEventCount;
static s32            s_seqEventIdx;
static PortMusicVoice s_musicVoices[PORT_MAX_MUSIC_VOICES];
static u8             s_channelProgram[16];
static u8             s_channelVolume[16];  /* MIDI CC7 — 0..127 */
static u8             s_channelPan[16];     /* MIDI CC10 — 0..127 */
static u32            s_seqDivision;
static u32            s_seqTempo;           /* µs per quarter note */
static f64            s_seqTicksPerSample;
static f64            s_seqCurrentTick;
static s32            s_musicPlaying;
static s32            s_seqShouldLoop;      /* 1 if the sequence had infinite loop markers */
static f32            s_musicVolume = 0.5f;

/* ===== Instrument Bank Parser ===== */

static void portAudioParseInstrumentBank(u8 *ctlData, u32 ctlSize, u32 tblRomOffset)
{
#ifdef PROJECT_TITAN_TGR
    /* Queue-mode gameplay music is rendered by the original libaudio synth,
     * whose DMA callbacks already stream instrument data from the ROM.  The
     * desktop fallback decoder is otherwise a duplicate multi-MiB PCM bank. */
    (void)ctlData;
    (void)ctlSize;
    (void)tblRomOffset;
    s_instBankParsed = 0;
#else
    if (!ctlData || ctlSize < 8 || g_romSize == 0) return;

    s16 bankCount = rd_bes16(ctlData + 2);
    if (bankCount <= 0 || bankCount > 16) return;

    u32 bankOff = rd_be32(ctlData + 4);
    /* ALBank header used unconditionally here is 8 bytes (instCount@0,
     * sampleRate@4); the instrument offset array beyond it is bounds-checked
     * per-element below. */
    if (!bank_ctl_has(ctlSize, bankOff, 8)) return;

    const u8 *bank = ctlData + bankOff;
    s16 instCount = rd_bes16(bank + 0);
    u32 sampleRate = rd_be32(bank + 4);
    s_instBankSampleRate = sampleRate;

    printf("[MUSIC] Instrument bank: %d instruments, sampleRate=%u\n",
           instCount, sampleRate);

    if (instCount <= 0 || instCount > PORT_MAX_INSTRUMENTS)
        instCount = (instCount > PORT_MAX_INSTRUMENTS) ? PORT_MAX_INSTRUMENTS : 0;

    s_instrumentCount = instCount;

    /* Parse each instrument */
    for (s32 ii = 0; ii < instCount; ii++) {
        /* Bounds-check the instOffsets[ii] slot itself before reading it. */
        if (!bank_ctl_has(ctlSize, bankOff, 0x0C + (u32)(ii + 1) * 4)) break;
        u32 instOff = rd_be32(bank + 0x0C + ii * 4);
        /* ALInstrument header used here is 0x10 bytes (soundCount@0xE). */
        if (!bank_ctl_has(ctlSize, instOff, 0x10)) continue;

        const u8 *inst = ctlData + instOff;
        s16 soundCount = rd_bes16(inst + 0x0E);
        if (soundCount <= 0) continue;
        if (soundCount > PORT_MAX_INST_SOUNDS) soundCount = PORT_MAX_INST_SOUNDS;

        PortInstrument *pi = &s_instruments[ii];
        pi->soundCount = 0;

        for (s32 si = 0; si < soundCount; si++) {
            /* Bounds-check the soundOffsets[si] slot itself before reading it. */
            if (!bank_ctl_has(ctlSize, instOff, 0x10 + (u32)(si + 1) * 4)) break;
            u32 soundOff = rd_be32(inst + 0x10 + si * 4);
            /* ALSound fields used here span [0x04, 0x0E) (kmOff, wtOff, pan, vol). */
            if (!bank_ctl_has(ctlSize, soundOff, 0x0E)) continue;

            const u8 *snd = ctlData + soundOff;
            u32 kmOff = rd_be32(snd + 0x04); /* keyMap offset */
            u32 wtOff = rd_be32(snd + 0x08); /* wavetable offset */
            u8  sndPan = snd[0x0C];
            u8  sndVol = snd[0x0D];

            /* ALWaveTable fields used here span [0x00, 0x14) (base, len, type,
             * loopOffset, bookOffset). */
            if (!bank_ctl_has(ctlSize, wtOff, 0x14)) continue;

            /* Read keymap (ALKeyMap fields km[0..5]) */
            u8 velMin = 0, velMax = 127, kMin = 0, kMax = 127, kBase = 60;
            s8 detune = 0;
            if (kmOff != 0 && bank_ctl_has(ctlSize, kmOff, 6)) {
                const u8 *km = ctlData + kmOff;
                velMin = km[0];
                velMax = km[1];
                kMin   = km[2];
                kMax   = km[3];
                kBase  = km[4];
                detune = (s8)km[5];
            }

            /* Read wavetable */
            const u8 *wt = ctlData + wtOff;
            u32 wtBase = rd_be32(wt + 0x00);
            s32 wtLen  = rd_bes32(wt + 0x04);
            u8  wtType = wt[0x08];

            if (wtLen <= 0) continue;

            /* Overflow-safe idiom -- see portAudioParseBankFile() above. */
            u32 sampleRomAddr = tblRomOffset + wtBase;
            if (!bank_ctl_has(g_romSize, sampleRomAddr, (u32)wtLen)) continue;

            PortInstSound *ps = &pi->sounds[pi->soundCount];
            ps->sample.sampleRate = sampleRate;
            ps->sample.hasLoop = 0;
            ps->keyBase = kBase;
            ps->keyMin = kMin;
            ps->keyMax = kMax;
            ps->velocityMin = velMin;
            ps->velocityMax = velMax;
            ps->detune = detune;
            ps->samplePan = sndPan;
            ps->sampleVolume = sndVol;

            if (wtType == AL_ADPCM_WAVE) {
                u32 bookOff = rd_be32(wt + 0x10);
                if (!bank_ctl_has(ctlSize, bookOff, 8)) continue;

                const u8 *bookData = ctlData + bookOff;
                s32 order = rd_bes32(bookData + 0x00);
                s32 npredictors = rd_bes32(bookData + 0x04);
                if (order <= 0 || order > 16 || npredictors <= 0 || npredictors > 16)
                    continue;

                /* order/npredictors clamped <=16 above, so bookSize (<=2048)
                 * can't overflow, but the array must still fit in ctlData. */
                s32 bookSize = order * npredictors * 8;
                if (!bank_ctl_has(ctlSize, bookOff, 8 + (u32)bookSize * 2)) continue;
                s16 *bookCoefs = (s16 *)malloc(bookSize * sizeof(s16));
                if (!bookCoefs) continue;

                for (s32 i = 0; i < bookSize; i++)
                    bookCoefs[i] = rd_bes16(bookData + 8 + i * 2);

                /* Check for loop */
                u32 loopOff = rd_be32(wt + 0x0C);
                if (loopOff != 0 && bank_ctl_has(ctlSize, loopOff, 0x0C)) {
                    const u8 *ld = ctlData + loopOff;
                    ps->sample.loopStart = rd_be32(ld + 0x00);
                    ps->sample.loopEnd   = rd_be32(ld + 0x04);
                    u32 loopCount = rd_be32(ld + 0x08);
                    ps->sample.hasLoop = (loopCount > 0) ? 1 : 0;
                }

                s32 maxSamples = (wtLen / VADPCM_FRAME_BYTES) * VADPCM_FRAME_SAMPLES;
                ps->sample.pcm = (s16 *)malloc(maxSamples * sizeof(s16));
                if (!ps->sample.pcm) { free(bookCoefs); continue; }

                u8 *sampleData = (u8 *)malloc((u32)wtLen);
                if (!sampleData ||
                    !platformRomRead(sampleRomAddr, sampleData, (u32)wtLen)) {
                    free(sampleData);
                    free(ps->sample.pcm);
                    ps->sample.pcm = NULL;
                    free(bookCoefs);
                    continue;
                }

                s32 outCount = 0;
                vadpcm_decode(sampleData, wtLen, bookCoefs, order, npredictors,
                              ps->sample.pcm, &outCount);
                ps->sample.numSamples = (u32)outCount;
                free(bookCoefs);
                free(sampleData);

            } else if (wtType == AL_RAW16_WAVE) {
                s32 numSamples = wtLen / 2;
                ps->sample.pcm = (s16 *)malloc(numSamples * sizeof(s16));
                if (!ps->sample.pcm) continue;

                u8 *sampleData = (u8 *)malloc((u32)wtLen);
                if (!sampleData ||
                    !platformRomRead(sampleRomAddr, sampleData, (u32)wtLen)) {
                    free(sampleData);
                    free(ps->sample.pcm);
                    ps->sample.pcm = NULL;
                    continue;
                }

                for (s32 i = 0; i < numSamples; i++)
                    ps->sample.pcm[i] = rd_bes16(sampleData + i * 2);
                ps->sample.numSamples = (u32)numSamples;
                free(sampleData);

                u32 loopOff = rd_be32(wt + 0x0C);
                if (loopOff != 0 && bank_ctl_has(ctlSize, loopOff, 0x0C)) {
                    const u8 *ld = ctlData + loopOff;
                    ps->sample.loopStart = rd_be32(ld + 0x00);
                    ps->sample.loopEnd   = rd_be32(ld + 0x04);
                    u32 loopCount = rd_be32(ld + 0x08);
                    ps->sample.hasLoop = (loopCount > 0) ? 1 : 0;
                }
            } else {
                continue;
            }

            pi->soundCount++;
        }
    }

    s32 total = 0;
    for (s32 i = 0; i < instCount; i++)
        total += s_instruments[i].soundCount;

    printf("[MUSIC] Parsed %d total instrument sounds across %d instruments\n",
           total, instCount);
    s_instBankParsed = 1;
#endif
}

/* ===== CSP (Compressed Sequence Player) Parser =====
 *
 * Reads the N64 CSP format: ALCMidiHdr header followed by per-track
 * MIDI event streams with variable-length deltas, running status,
 * and backup block compression. Outputs a flat sorted event array.
 */

/* CSP track reader state */
typedef struct {
    u8 *curLoc;
    u8 *curBUPtr;
    u8  curBULen;
    u8  lastStatus;
    u32 evtDeltaTicks;
    s32 valid;
} CspTrackState;

static u8 cspGetByte(CspTrackState *ts)
{
    u8 b;
    if (ts->curBULen > 0) {
        b = *ts->curBUPtr++;
        ts->curBULen--;
        return b;
    }

    b = *ts->curLoc++;
    if (b == 0xFE) {
        u8 next = *ts->curLoc++;
        if (next == 0xFE) {
            return 0xFE; /* escaped literal */
        }
        /* Backup block: hi byte of offset, then lo byte, then length */
        u8 loBackUp = *ts->curLoc++;
        u8 theLen   = *ts->curLoc++;
        u32 backup  = ((u32)next << 8) | loBackUp;
        ts->curBUPtr = ts->curLoc - (backup + 4);
        ts->curBULen = theLen;
        b = *ts->curBUPtr++;
        ts->curBULen--;
    }
    return b;
}

static u32 cspReadVarLen(CspTrackState *ts)
{
    u32 value = 0;
    u8 b;
    do {
        b = cspGetByte(ts);
        value = (value << 7) | (b & 0x7F);
    } while (b & 0x80);
    return value;
}

/**
 * Parse a decompressed CSP sequence into a flat event array.
 * seqData points to the decompressed sequence (ALCMidiHdr + track data).
 * The header fields are big-endian (from N64 ROM).
 */
static s32 cspParseEvents(u8 *seqData, PortSeqEvent *events, s32 maxEvents,
                          u32 *outDivision, s32 *outHasLoop)
{
    /* Parse ALCMidiHdr: 16 track offsets (u32 BE) + division (u32 BE) */
    u32 trackOffset[16];
    for (s32 i = 0; i < 16; i++)
        trackOffset[i] = rd_be32(seqData + i * 4);

    u32 division = rd_be32(seqData + 64);
    *outDivision = division;

    /* Initialize per-track state */
    CspTrackState tracks[16];
    u32 validMask = 0;
    for (s32 i = 0; i < 16; i++) {
        memset(&tracks[i], 0, sizeof(CspTrackState));
        if (trackOffset[i] != 0) {
            tracks[i].curLoc = seqData + trackOffset[i];
            tracks[i].valid = 1;
            tracks[i].evtDeltaTicks = cspReadVarLen(&tracks[i]);
            validMask |= (1u << i);
        }
    }

    s32 eventCount = 0;
    u32 lastDeltaTicks = 0;
    s32 deltaFlag = 1;
    s32 loopBackSeen = 0;
    *outHasLoop = 0;

    /* Safety: limit total events to prevent runaway parsing */
    s32 safetyCounter = 500000;

    while (validMask && eventCount < maxEvents - 1 && safetyCounter-- > 0) {
        /* Find track with earliest next event */
        u32 firstTime = 0xFFFFFFFF;
        s32 firstTrack = -1;

        for (s32 i = 0; i < 16; i++) {
            if (!(validMask & (1u << i))) continue;
            if (deltaFlag)
                tracks[i].evtDeltaTicks -= lastDeltaTicks;
            if (tracks[i].evtDeltaTicks < firstTime) {
                firstTime = tracks[i].evtDeltaTicks;
                firstTrack = i;
            }
        }

        if (firstTrack < 0) break;

        /* Accumulate absolute tick from deltas */
        static u32 s_cspAbsTick;
        if (eventCount == 0) s_cspAbsTick = 0;
        s_cspAbsTick += firstTime;
        u32 absoluteTick = s_cspAbsTick;

        lastDeltaTicks = firstTime;

        /* Read event from firstTrack */
        CspTrackState *ts = &tracks[firstTrack];
        u8 status = cspGetByte(ts);

        if (status == 0xFF) { /* Meta event */
            u8 type = cspGetByte(ts);
            if (type == 0x51) { /* Tempo change */
                u8 b1 = cspGetByte(ts);
                u8 b2 = cspGetByte(ts);
                u8 b3 = cspGetByte(ts);
                u32 tempo = ((u32)b1 << 16) | ((u32)b2 << 8) | b3;
                events[eventCount].absoluteTick = absoluteTick;
                events[eventCount].type = PORT_EVT_TEMPO;
                events[eventCount].channel = 0;
                events[eventCount].byte1 = 0;
                events[eventCount].byte2 = 0;
                events[eventCount].extra = tempo;
                eventCount++;
                ts->lastStatus = 0;
            } else if (type == 0x2F) { /* End of track */
                validMask &= ~(1u << firstTrack);
                ts->valid = 0;
                if (validMask == 0) {
                    events[eventCount].absoluteTick = absoluteTick;
                    events[eventCount].type = PORT_EVT_SEQ_END;
                    eventCount++;
                }
            } else if (type == 0x2E) { /* Loop start */
                cspGetByte(ts);
                cspGetByte(ts);
                ts->lastStatus = 0;
            } else if (type == 0x2D) { /* Loop end */
                u8 *tmpPtr = ts->curLoc;
                u8 loopCt  = *tmpPtr++;
                u8 curLpCt = *tmpPtr;
                if (curLpCt == 0) {
                    /* Loop count exhausted — skip past the loop marker */
                    *tmpPtr = loopCt;
                    ts->curLoc = tmpPtr + 5;
                } else if (curLpCt == 0xFF) {
                    /* Infinite loop — parse one iteration, then stop this track.
                     * The mixer will handle replay by resetting to event 0. */
                    loopBackSeen = 1;
                    validMask &= ~(1u << firstTrack);
                    ts->valid = 0;
                    if (validMask == 0) {
                        events[eventCount].absoluteTick = absoluteTick;
                        events[eventCount].type = PORT_EVT_SEQ_END;
                        eventCount++;
                    }
                } else {
                    *tmpPtr = curLpCt - 1;
                    tmpPtr++;
                    u32 offset = ((u32)tmpPtr[0] << 24) | ((u32)tmpPtr[1] << 16)
                               | ((u32)tmpPtr[2] << 8) | tmpPtr[3];
                    tmpPtr += 4;
                    ts->curLoc = tmpPtr - offset;
                }
                ts->lastStatus = 0;
            }
        } else { /* MIDI event */
            u8 byte1, byte2 = 0;
            u8 effectiveStatus;

            if (status & 0x80) {
                effectiveStatus = status;
                byte1 = cspGetByte(ts);
                ts->lastStatus = status;
            } else {
                /* Running status: status byte was actually byte1 */
                effectiveStatus = ts->lastStatus;
                byte1 = status;
            }

            u8 statusType = effectiveStatus & 0xF0;
            u8 channel    = effectiveStatus & 0x0F;

            /* Read byte2 for events that have it */
            if (statusType != 0xC0 && statusType != 0xD0) {
                byte2 = cspGetByte(ts);
            }

            if (statusType == 0x90 && byte2 > 0) { /* Note On */
                u32 duration = cspReadVarLen(ts);
                events[eventCount].absoluteTick = absoluteTick;
                events[eventCount].type = PORT_EVT_NOTE_ON;
                events[eventCount].channel = channel;
                events[eventCount].byte1 = byte1;  /* note */
                events[eventCount].byte2 = byte2;  /* velocity */
                events[eventCount].extra = duration;
                eventCount++;
            } else if (statusType == 0x90 && byte2 == 0) { /* Note On with vel 0 = Note Off */
                /* CSP uses embedded durations, but explicit note-offs can occur */
                cspReadVarLen(ts); /* consume duration */
                events[eventCount].absoluteTick = absoluteTick;
                events[eventCount].type = PORT_EVT_NOTE_OFF;
                events[eventCount].channel = channel;
                events[eventCount].byte1 = byte1;
                events[eventCount].byte2 = 0;
                events[eventCount].extra = 0;
                eventCount++;
            } else if (statusType == 0x80) { /* Explicit Note Off */
                events[eventCount].absoluteTick = absoluteTick;
                events[eventCount].type = PORT_EVT_NOTE_OFF;
                events[eventCount].channel = channel;
                events[eventCount].byte1 = byte1;
                events[eventCount].byte2 = 0;
                events[eventCount].extra = 0;
                eventCount++;
            } else if (statusType == 0xB0) { /* Control Change */
                events[eventCount].absoluteTick = absoluteTick;
                events[eventCount].type = PORT_EVT_CC;
                events[eventCount].channel = channel;
                events[eventCount].byte1 = byte1;  /* controller number */
                events[eventCount].byte2 = byte2;  /* value */
                events[eventCount].extra = 0;
                eventCount++;
            } else if (statusType == 0xC0) { /* Program Change */
                events[eventCount].absoluteTick = absoluteTick;
                events[eventCount].type = PORT_EVT_PROGRAM;
                events[eventCount].channel = channel;
                events[eventCount].byte1 = byte1;
                events[eventCount].byte2 = 0;
                events[eventCount].extra = 0;
                eventCount++;
            }
            /* Pitch bend (0xE0) and aftertouch (0xD0) are parsed but
             * not emitted — their effect is minor for Dam parity. */
        }

        /* Read delta for next event on this track */
        if (ts->valid)
            ts->evtDeltaTicks += cspReadVarLen(ts);

        deltaFlag = 1;
    }

    *outHasLoop = loopBackSeen;
    return eventCount;
}

/* ===== Music Voice Helpers ===== */

/**
 * Find the instrument sound matching a MIDI note within the given instrument.
 * Returns NULL if no matching sound is found.
 */
static PortInstSound *findInstSound(s32 instIdx, u8 note, u8 velocity)
{
    if (instIdx < 0 || instIdx >= s_instrumentCount) return NULL;

    PortInstrument *inst = &s_instruments[instIdx];
    for (s32 i = 0; i < inst->soundCount; i++) {
        PortInstSound *ps = &inst->sounds[i];
        if (note >= ps->keyMin && note <= ps->keyMax &&
            velocity >= ps->velocityMin && velocity <= ps->velocityMax) {
            return ps;
        }
    }

    /* Fallback: return first sound if any */
    if (inst->soundCount > 0)
        return &inst->sounds[0];

    return NULL;
}

/**
 * Calculate pitch ratio for a MIDI note relative to sample's key base.
 * pitch = 2^((note - keyBase) / 12) * (sampleRate / outputRate)
 */
static f32 calcNotePitch(u8 note, u8 keyBase, s8 detune, u32 sampleRate)
{
    f32 semitones = (f32)((s32)note - (s32)keyBase) + (f32)detune / 100.0f;
    f32 pitchRatio = powf(2.0f, semitones / 12.0f);
    pitchRatio *= (f32)sampleRate / (f32)PORT_AUDIO_RATE;
    return pitchRatio;
}

/**
 * Start a music voice for a note-on event.
 */
static void musicNoteOn(u8 channel, u8 note, u8 velocity, u32 endTick)
{
    s32 instIdx = s_channelProgram[channel];
    PortInstSound *ps = findInstSound(instIdx, note, velocity);

    if (!ps || !ps->sample.pcm) return;

    /* Find a free music voice */
    s32 vi = -1;
    for (s32 i = 0; i < PORT_MAX_MUSIC_VOICES; i++) {
        if (!s_musicVoices[i].active) { vi = i; break; }
    }
    if (vi < 0) {
        /* Steal oldest voice */
        u32 earliest = 0xFFFFFFFF;
        for (s32 i = 0; i < PORT_MAX_MUSIC_VOICES; i++) {
            if (s_musicVoices[i].endTick < earliest) {
                earliest = s_musicVoices[i].endTick;
                vi = i;
            }
        }
    }
    if (vi < 0) return;

    PortMusicVoice *v = &s_musicVoices[vi];
    v->sample   = &ps->sample;
    v->position = 0.0f;
    v->pitch    = calcNotePitch(note, ps->keyBase, ps->detune,
                                ps->sample.sampleRate);
    v->endTick  = endTick;
    v->channel  = channel;
    v->note     = note;
    v->active   = 1;

    /* Volume from velocity × channel volume × music volume × master volume */
    f32 chVol = (f32)s_channelVolume[channel] / 127.0f;
    f32 vol = ((f32)velocity / 127.0f) * chVol * s_musicVolume * s_masterVolume;

    /* Pan: mix channel pan with sample pan */
    f32 chPan = (f32)s_channelPan[channel] / 127.0f;
    f32 smPan = (f32)ps->samplePan / 127.0f;
    f32 pan = (chPan + smPan) * 0.5f; /* average channel and sample pan */
    v->volumeL = vol * (1.0f - pan * 0.5f);
    v->volumeR = vol * (0.5f + pan * 0.5f);
}

/**
 * Stop all music voices matching a note on a channel.
 */
static void musicNoteOff(u8 channel, u8 note)
{
    for (s32 i = 0; i < PORT_MAX_MUSIC_VOICES; i++) {
        if (s_musicVoices[i].active &&
            s_musicVoices[i].channel == channel &&
            s_musicVoices[i].note == note) {
            s_musicVoices[i].active = 0;
        }
    }
}

/**
 * Update sequencer tempo and recalculate ticks-per-sample.
 */
static void musicSetTempo(u32 tempoUsPerQN)
{
    s_seqTempo = tempoUsPerQN;
    /* ticksPerSample = division * 1000000 / (tempo * outputRate) */
    s_seqTicksPerSample = (f64)s_seqDivision * 1000000.0
                        / ((f64)s_seqTempo * (f64)PORT_AUDIO_RATE);
}

/* ===== Legacy CSP software music mixer (DEAD — WEB-047) =====
 *
 * musicMixSamples() was the software CSP sequence mixer, called ONLY from the
 * removed audioCallback() pull path (queue mode has no callback). It is retained
 * under #if 0 for reference (its companion portMusicPlaySequence() below is the
 * documented "fallback diagnostics" parser). Live music is driven by the N64
 * sequence player (src/music.c → the libaudio synth in audi_port.c), whose
 * output reaches the device through osAiSetNextBuffer — NOT this mixer. */
#if 0
static void musicMixSamples(s16 *out, s32 numSamples)
{
    if (!s_musicPlaying || !s_seqEvents) return;

    for (s32 i = 0; i < numSamples; i++) {
        /* Advance sequencer tick */
        s_seqCurrentTick += s_seqTicksPerSample;
        u32 curTick = (u32)s_seqCurrentTick;

        /* Process any events at or before curTick */
        while (s_seqEventIdx < s_seqEventCount) {
            PortSeqEvent *evt = &s_seqEvents[s_seqEventIdx];
            if (evt->absoluteTick > curTick) break;

            switch (evt->type) {
                case PORT_EVT_NOTE_ON:
                    musicNoteOn(evt->channel, evt->byte1, evt->byte2,
                                evt->absoluteTick + evt->extra);
                    break;
                case PORT_EVT_NOTE_OFF:
                    musicNoteOff(evt->channel, evt->byte1);
                    break;
                case PORT_EVT_TEMPO:
                    musicSetTempo(evt->extra);
                    break;
                case PORT_EVT_PROGRAM:
                    s_channelProgram[evt->channel] = evt->byte1;
                    break;
                case PORT_EVT_CC:
                    if (evt->byte1 == 7) /* Volume */
                        s_channelVolume[evt->channel] = evt->byte2;
                    else if (evt->byte1 == 10) /* Pan */
                        s_channelPan[evt->channel] = evt->byte2;
                    break;
                case PORT_EVT_SEQ_END:
                    if (s_seqShouldLoop) {
                        /* Loop: reset to start, keep playing */
                        s_seqEventIdx = 0;
                        s_seqCurrentTick = 0.0;
                        memset(s_musicVoices, 0, sizeof(s_musicVoices));
                        memset(s_channelVolume, 127, sizeof(s_channelVolume));
                        memset(s_channelPan, 64, sizeof(s_channelPan));
                        musicSetTempo(500000); /* reset to default tempo */
                    } else {
                        s_musicPlaying = 0;
                    }
                    return;
                default:
                    break;
            }
            s_seqEventIdx++;
        }

        /* Check for note duration expiry */
        for (s32 v = 0; v < PORT_MAX_MUSIC_VOICES; v++) {
            if (s_musicVoices[v].active && curTick >= s_musicVoices[v].endTick) {
                s_musicVoices[v].active = 0;
            }
        }

        /* Mix active music voices */
        for (s32 v = 0; v < PORT_MAX_MUSIC_VOICES; v++) {
            PortMusicVoice *voice = &s_musicVoices[v];
            if (!voice->active || !voice->sample || !voice->sample->pcm)
                continue;

            u32 pos = (u32)voice->position;
            PortDecodedSample *smp = voice->sample;

            if (pos >= smp->numSamples) {
                if (smp->hasLoop && smp->loopEnd > smp->loopStart) {
                    voice->position = (f32)smp->loopStart;
                    pos = smp->loopStart;
                } else {
                    voice->active = 0;
                    continue;
                }
            }

            f32 sample = (f32)smp->pcm[pos];

            s32 left  = (s32)out[i * 2]     + (s32)(sample * voice->volumeL);
            s32 right = (s32)out[i * 2 + 1] + (s32)(sample * voice->volumeR);

            if (left > 32767) left = 32767;
            if (left < -32768) left = -32768;
            if (right > 32767) right = 32767;
            if (right < -32768) right = -32768;

            out[i * 2]     = (s16)left;
            out[i * 2 + 1] = (s16)right;

            voice->position += voice->pitch;
        }
    }
}
#endif /* 0 — legacy CSP software music mixer (WEB-047) */

/* ===== Public Music API ===== */

/**
 * Legacy direct-music parser retained for fallback diagnostics.
 * Native gameplay music is driven by src/music.c's ALCSPlayer/CSP path.
 * seqData is the decompressed sequence data (big-endian CSP format).
 * seqLen is the length in bytes.
 */
void portMusicPlaySequence(u8 *seqData, u32 seqLen)
{
    if (!s_audioInitialized) return;
    if (!s_instBankParsed) {
        printf("[MUSIC] No instrument bank loaded, cannot play music\n");
        return;
    }

    SDL_LockMutex(s_audioMutex);

    /* Stop any current music */
    s_musicPlaying = 0;
    memset(s_musicVoices, 0, sizeof(s_musicVoices));
    memset(s_channelProgram, 0, sizeof(s_channelProgram));
    memset(s_channelVolume, 127, sizeof(s_channelVolume));  /* default full volume */
    memset(s_channelPan, 64, sizeof(s_channelPan));         /* default center pan */

    /* Allocate event buffer if needed */
    if (!s_seqEvents) {
        s_seqEvents = (PortSeqEvent *)malloc(PORT_MAX_SEQ_EVENTS * sizeof(PortSeqEvent));
        if (!s_seqEvents) {
            SDL_UnlockMutex(s_audioMutex);
            printf("[MUSIC] Failed to allocate event buffer\n");
            return;
        }
    }

    /* Make a working copy of the sequence data since the CSP parser
     * modifies data in place for loop handling (writes loop counters back).
     * The original buffer lives in the game's alHeap and corruption there
     * would crash the game. */
    u8 *seqCopy = (u8 *)malloc(seqLen + 256); /* extra padding for safety */
    if (!seqCopy) {
        SDL_UnlockMutex(s_audioMutex);
        printf("[MUSIC] Failed to allocate sequence copy\n");
        return;
    }
    memcpy(seqCopy, seqData, seqLen);
    memset(seqCopy + seqLen, 0, 256); /* zero padding */

    /* Parse the CSP sequence copy into events */
    s32 hasLoop = 0;
    s_seqEventCount = cspParseEvents(seqCopy, s_seqEvents, PORT_MAX_SEQ_EVENTS,
                                     &s_seqDivision, &hasLoop);
    free(seqCopy);
    s_seqEventIdx = 0;
    s_seqCurrentTick = 0.0;
    s_seqShouldLoop = hasLoop;

    /* Default tempo: 120 BPM = 500000 µs/QN */
    musicSetTempo(500000);

    printf("[MUSIC] Parsed %d events, division=%u, loop=%d\n",
           s_seqEventCount, s_seqDivision, hasLoop);

    s_musicPlaying = 1;

    SDL_UnlockMutex(s_audioMutex);
}

/**
 * Stop music playback.
 */
void portMusicStop(void)
{
    if (!s_audioInitialized) return;

    SDL_LockMutex(s_audioMutex);
    s_musicPlaying = 0;
    memset(s_musicVoices, 0, sizeof(s_musicVoices));
    SDL_UnlockMutex(s_audioMutex);
}

/**
 * Set music volume (0.0 to 1.0).
 */
void portMusicSetVolume(f32 vol)
{
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    s_musicVolume = vol;
}

/**
 * Check if music is currently playing.
 */
s32 portMusicIsPlaying(void)
{
    return s_musicPlaying;
}

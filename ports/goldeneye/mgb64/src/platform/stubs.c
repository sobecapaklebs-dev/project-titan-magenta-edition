/**
 * stubs.c - Native compatibility shims for libultra OS/GU entry points.
 *
 * This file hosts host-side replacements for hardware-facing APIs that the
 * native port still calls directly. Gameplay and audio helpers should live in
 * their real modules; release checks guard against those becoming silent no-op
 * fallbacks here.
 */
#include <ultra64.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#ifdef PROJECT_TITAN_TGR
#include "sdl_titan_compat.h"
#include "../../../titan_ge_runtime.h"
#else
#include <SDL.h>
#endif
#include <bondconstants.h>
#include <boss.h>
#include "../app/input_actions.h"
#include "radial_deadzone.h"
#include "weapon_cycle_queue.h"
#include "audi.h"
#include "savedir.h"
#include "web_audio_worklet.h"
#include "game/bondinv.h"
#include "game/bondview.h"
#include "game/chr.h"
#include "game/chrai.h"
#include "game/chrlv.h"
#include "game/file.h"
#include "game/file2.h"
#include "game/gun.h"
#include "game/lvl.h"
#include "game/objecthandler.h"
#include "game/player.h"
#include "game/watch.h"
#include "game/bondview_r.h"

extern void fileCheckSaveStageDifficultyTime(save_data *folder,
                                             LEVEL_SOLO_SEQUENCE levelid,
                                             DIFFICULTY difficulty,
                                             s32 newtime);
extern void fileWriteSave(save_data *save);
extern void sndStubTick(void);
extern ALBank *g_musicSfxBufferPtr;
extern void sndDeactivate(ALSoundState *state);
extern void sndCreatePostEvent(ALSoundState *state, s16 eventType, s32 arg2);
extern ObjectRecord *objFindByTagId(s32 TagID);
extern INV_ITEM_TYPE collect_or_interact_object(PropRecord *prop, bool showstring);
extern void propExecuteTickOperation(PropRecord *prop, INV_ITEM_TYPE op);
extern void maybe_detonate_object(ObjectRecord *obj, f32 damage, coord3d *pos, ITEM_IDS item, s32 owner);
extern s32 objGetDestroyedLevel(ObjectRecord *obj);
extern PadRecord *dword_CODE_bss_800799F8;
extern CutsceneRecord *gBondViewCutscene;
extern u64 g_randomSeed;

/* ===== Globals ===== */
u64 osClockRate = 46875000; /* N64 CPU counter rate (62.5 MHz * 3/4) */

/* RSP ucode pointers — stubs */
long long int _rspbootTextStart[1];
long long int _rspbootTextEnd[1];
long long int _gsp3DTextStart[1];
long long int _gsp3DDataStart[1];
long long int _gsp3DDataEnd[1];
long long int _aspMainTextStart[1];
long long int _aspMainTextEnd[1];
long long int _aspMainDataStart[1];
long long int _aspMainDataEnd[1];

/* Provide aliased names */
long long int rspbootTextStart[1];
long long int rspbootTextEnd[1];
long long int gsp3DTextStart[1];
long long int gsp3DDataStart[1];
long long int gsp3DDataEnd[1];
long long int aspMainTextStart[1];
long long int aspMainTextEnd[1];
long long int aspMainDataStart[1];
long long int aspMainDataEnd[1];

/* VI mode tables */
OSViMode osViModeNtscLpn1 = {0};
OSViMode osViModeNtscLpn2 = {0};
OSViMode osViModePalLpn1 = {0};
OSViMode osViModePalLpn2 = {0};
OSViMode osViModeMpalLpn1 = {0};
OSViMode osViModeTable[64] = {{0}};

/* ===== Thread stubs ===== */

void osCreateThread(OSThread *thread, OSId id, void (*entry)(void *),
                    void *arg, void *sp, OSPri pri) {
    if (thread) {
        memset(thread, 0, sizeof(OSThread));
        thread->id = id;
        thread->priority = pri;
    }
}

void osStartThread(OSThread *thread) { (void)thread; }
void osStopThread(OSThread *thread) { (void)thread; }
void osDestroyThread(OSThread *thread) { (void)thread; }
void osSetThreadPri(OSThread *thread, OSPri pri) {
    if (thread) thread->priority = pri;
}
OSPri osGetThreadPri(OSThread *thread) {
    return thread ? thread->priority : 0;
}
OSId osGetThreadId(OSThread *thread) {
    return thread ? thread->id : 0;
}

/* ===== Message queue stubs ===== */

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msg, s32 count) {
    if (mq) {
        memset(mq, 0, sizeof(OSMesgQueue));
        mq->msg = msg;
        mq->msgCount = count;
    }
}

s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flags) {
    if (mq && mq->msg && mq->validCount < mq->msgCount) {
        s32 idx = (mq->first + mq->validCount) % mq->msgCount;
        mq->msg[idx] = msg;
        mq->validCount++;
        return 0;
    }
    return -1;
}

s32 osJamMesg(OSMesgQueue *mq, OSMesg msg, s32 flags) {
    /* Insert at FRONT of queue (high-priority), unlike osSendMesg which appends.
     * This matches N64 behavior where osJamMesg is used for urgent messages
     * (retrace signals, DMA completion) that must be processed first. */
    if (mq && mq->msg && mq->validCount < mq->msgCount) {
        mq->first = (mq->first - 1 + mq->msgCount) % mq->msgCount;
        mq->msg[mq->first] = msg;
        mq->validCount++;
        return 0;
    }
    return -1;
}

/* Forward declaration for SDL frame sync (platform_sdl.c) */
extern void platformFrameSync(void);
/* Forward declaration for timer check (defined later in this file) */
void platformCheckTimers(void);

s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flags) {
    if (mq && mq->validCount > 0) {
        if (msg) *msg = mq->msg[mq->first];
        mq->first = (mq->first + 1) % mq->msgCount;
        mq->validCount--;
        return 0;
    }
    if (flags == OS_MESG_NOBLOCK) return -1;
    /* Blocking mode: drive frame timing and send retrace messages
     * until a message arrives in this queue. This replaces the N64's
     * interrupt-driven scheduler with cooperative frame pacing. */
    {
        /* Check if a message already arrived (e.g. from osSetTimer) */
        if (mq && mq->validCount > 0) {
            if (msg) *msg = mq->msg[mq->first];
            mq->first = (mq->first + 1) % mq->msgCount;
            mq->validCount--;
            return 0;
        }
        /* No message yet — drive frame timing until one arrives */
        {
            extern OSMesgQueue gfxFrameMsgQ;
            if (mq != &gfxFrameMsgQ) {
                /* Non-gfx queue blocking — this shouldn't happen often on PC.
                 * Just return immediately with a fake success to unblock. */
                if (msg) *msg = NULL;
                return 0; /* Fake success — unblock non-gfx blocking calls */
            }
        }
        while (mq && mq->validCount == 0) {
            platformCheckTimers(); /* fire any expired timers */
            if (mq->validCount > 0) break;
            platformFrameSync();
        }
        if (mq && mq->validCount > 0) {
            if (msg) *msg = mq->msg[mq->first];
            mq->first = (mq->first + 1) % mq->msgCount;
            mq->validCount--;
            return 0;
        }
        return -1;
    }
}

/* ===== Event / Interrupt stubs ===== */

void osSetEventMesg(OSEvent e, OSMesgQueue *mq, OSMesg msg) {
    (void)e; (void)mq; (void)msg;
}

OSIntMask osGetIntMask(void) { return 0; }

/* ===== PERF-052: reified interrupt-mask mutual exclusion ============= *
 *
 * On the N64 every libaudio critical section is bracketed with
 *     mask = osSetIntMask(OS_IM_NONE);  ...  osSetIntMask(mask);
 * which disables interrupts so the game thread and the interrupt-driven
 * audio thread cannot touch shared libaudio state at the same time. When
 * the port flattened audio onto one thread this became a pure no-op
 * (osSetIntMask returned 0), which was correct because there was only one
 * thread.
 *
 * PERF-052 (opt-in, native-only) restores the original two-thread structure:
 * a dedicated worker runs the synth (alAudioFrame) while the main thread does
 * everything else. That reintroduces exactly the cross-thread contention the
 * brackets were written to guard, so we REIFY the brackets as a real recursive
 * lock — the code's own intended mutual exclusion, made real again.
 *
 * Protocol (only engaged while the synth worker exists, i.e.
 * g_audioSynthLockActive != 0):
 *   - osSetIntMask(OS_IM_NONE)  -> acquire the lock, return PORT_SYNTH_LOCK_TOKEN
 *   - osSetIntMask(TOKEN)       -> release the lock (the caller's paired restore)
 *   - osSetIntMask(anything else) -> not a lock op (e.g. sched.c's OS_IM_VI
 *                                    VI-mode swap): mirror the legacy no-op,
 *                                    return 0, touch no lock state. The caller's
 *                                    matching restore then passes that 0 back
 *                                    here and also no-ops, so its bracket stays
 *                                    balanced.
 * The token is never inspected by any caller — the save/restore idiom only ever
 * feeds it straight back to osSetIntMask — so returning a sentinel is safe. It
 * is deliberately NOT a real OS_IM_* mask (all of which have low byte 0x01).
 *
 * The mutex is RECURSIVE because bracketed helpers nest on one thread (e.g.
 * snd.c sndDeactivateAllSfxByFlag brackets, then calls alEvtqPostEvent which
 * brackets again). A recursive mutex makes the reified brackets compose exactly
 * as the original per-thread interrupt mask did.
 *
 * When g_audioSynthLockActive == 0 (the default: no worker, single thread, and
 * every web build) osSetIntMask is the exact historical no-op — one predicted-
 * not-taken branch, no lock, returns 0 — so the default path stays
 * byte-identical with zero measurable overhead. */
int g_audioSynthLockActive = 0;

#define PORT_SYNTH_LOCK_TOKEN ((OSIntMask)0x5A4C4B00u) /* 'ZLK\0' — not a mask */

#if !defined(__EMSCRIPTEN__) && !defined(PROJECT_TITAN_TGR)
#include <pthread.h>
static pthread_mutex_t s_synthLock;
static int             s_synthLockReady = 0;

/* Called (once, on the main thread) from portAudioSynthWorkerStart before the
 * worker exists and before g_audioSynthLockActive is set — so the recursive
 * mutex is fully constructed before any thread can reach the acquire path. */
void portSynthLockInit(void) {
    if (!s_synthLockReady) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&s_synthLock, &attr);
        pthread_mutexattr_destroy(&attr);
        s_synthLockReady = 1;
    }
}

OSIntMask osSetIntMask(OSIntMask mask) {
    if (g_audioSynthLockActive) {
        if (mask == OS_IM_NONE) {
            pthread_mutex_lock(&s_synthLock);
            return PORT_SYNTH_LOCK_TOKEN;
        }
        if (mask == PORT_SYNTH_LOCK_TOKEN) {
            pthread_mutex_unlock(&s_synthLock);
            return 0;
        }
        /* Non-lock mask (OS_IM_VI etc.): legacy no-op. */
        return 0;
    }
    (void)mask;
    return 0; /* default single-thread path: exact historical behavior */
}
#else
void portSynthLockInit(void) { }
OSIntMask osSetIntMask(OSIntMask mask) { (void)mask; return 0; }
#endif

/* ===== Deterministic mode ===== */
int g_deterministic = 0;
static char g_traceStatePathStorage[256];
const char *g_traceStatePath = NULL;

void pcSetTraceStatePath(const char *path) {
    size_t len = 0;

    if (path == NULL || *path == '\0') {
        g_traceStatePathStorage[0] = '\0';
        g_traceStatePath = NULL;
        return;
    }

    while (path[len] != '\0' && len < sizeof(g_traceStatePathStorage) - 1) {
        g_traceStatePathStorage[len] = path[len];
        len++;
    }

    g_traceStatePathStorage[len] = '\0';
    g_traceStatePath = g_traceStatePathStorage;
}

/* ===== Timer implementation ===== */

/* N64 CPU counter ticks at 46.875 MHz. In deterministic mode, advance by
 * a fixed amount per call (~1 frame at 60fps) so game logic is identical
 * across runs regardless of wall-clock timing. */
static u32 s_syntheticCount = 0;
static int s_stableDeterministicCount = -1;
#define SYNTHETIC_TICKS_PER_FRAME (46875000U / 60U)

int pcStableDeterministicCountEnabled(void) {
    const char *env;

    if (s_stableDeterministicCount < 0) {
        env = getenv("GE007_DETERMINISTIC_STABLE_COUNT");
        s_stableDeterministicCount = (env != NULL && env[0] != '\0') ? 1 : 0;
    }

    return g_deterministic && s_stableDeterministicCount;
}

void pcAdvanceDeterministicCountForFrame(void) {
    if (pcStableDeterministicCountEnabled()) {
        s_syntheticCount += SYNTHETIC_TICKS_PER_FRAME;
    }
}

u32 osGetCount(void) {
    if (g_deterministic) {
        if (pcStableDeterministicCountEnabled()) {
            return s_syntheticCount;
        }
        s_syntheticCount += SYNTHETIC_TICKS_PER_FRAME;
        return s_syntheticCount;
    }
#ifdef PROJECT_TITAN_TGR
    {
        const TitanGameApiV1 *api=TitanGeRuntime_GetApi();
        u64 usec=api?api->uptime_us():0u;
        return (u32)((usec*46875ULL)/1000ULL);
    }
#else
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (u32)(ts.tv_sec * 46875000ULL + ts.tv_nsec * 46875ULL / 1000000ULL);
    }
#endif
}

void osSetTime(u64 time) { (void)time; }
u64 osGetTime(void) { return (u64)osGetCount(); }

/* Timer list: tracks pending timers for deferred message delivery.
 * N64 uses interrupt-driven timers; on PC we check expiry each frame. */
#define PC_MAX_TIMERS 16

typedef struct {
    int        active;
    OSTimer   *handle;     /* caller's timer struct (used by osStopTimer) */
    u64        expiry_us;  /* wall-clock expiry in microseconds */
    u64        interval_us;/* repeat interval (0 = one-shot) */
    OSMesgQueue *mq;
    OSMesg     msg;
} PcTimer;

static PcTimer s_timerList[PC_MAX_TIMERS];

static u64 pcGetTimeUs(void) {
#ifdef PROJECT_TITAN_TGR
    const TitanGameApiV1 *api=TitanGeRuntime_GetApi();
    return api?api->uptime_us():0u;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000ULL + (u64)ts.tv_nsec / 1000ULL;
#endif
}

void osSetTimer(OSTimer *timer, u64 countdown, u64 interval,
                OSMesgQueue *mq, OSMesg msg) {
    /* Convert N64 CPU counter ticks to microseconds.
     * N64 counter runs at osClockRate (46875000 Hz).
     * us = ticks * 1000000 / osClockRate */
    u64 delay_us = (countdown * 1000000ULL) / osClockRate;
    u64 interval_us = (interval * 1000000ULL) / osClockRate;

    /* Very short delays (< 1ms): fire immediately to avoid stalling
     * the single-threaded game loop on sub-frame waits. */
    if (delay_us < 1000) {
        if (mq) {
            osSendMesg(mq, msg, OS_MESG_NOBLOCK);
        }
        return;
    }

    /* Find a free slot */
    u64 now = pcGetTimeUs();
    for (int i = 0; i < PC_MAX_TIMERS; i++) {
        if (!s_timerList[i].active) {
            s_timerList[i].active = 1;
            s_timerList[i].handle = timer;
            s_timerList[i].expiry_us = now + delay_us;
            s_timerList[i].interval_us = interval_us;
            s_timerList[i].mq = mq;
            s_timerList[i].msg = msg;
            return;
        }
    }

    /* No free slot — fire immediately as fallback */
    if (mq) {
        osSendMesg(mq, msg, OS_MESG_NOBLOCK);
    }
}

void osStopTimer(OSTimer *timer) {
    for (int i = 0; i < PC_MAX_TIMERS; i++) {
        if (s_timerList[i].active && s_timerList[i].handle == timer) {
            s_timerList[i].active = 0;
            s_timerList[i].mq = NULL;
            /* Don't return — clear ALL slots for this handle.  The boss
             * init loop can register multiple timers in successive slots
             * when osRecvMesg returns immediately (non-gfx queue). */
        }
    }
}

/* Check all pending timers and fire any that have expired.
 * Called from platformFrameSync() and from osRecvMesg blocking loop. */
void platformCheckTimers(void) {
    u64 now = pcGetTimeUs();
    for (int i = 0; i < PC_MAX_TIMERS; i++) {
        if (s_timerList[i].active && now >= s_timerList[i].expiry_us) {
            /* Safety: validate that the message queue is reasonable before sending.
             * Timers may hold stale pointers to stack-allocated queues. */
            OSMesgQueue *tmq = s_timerList[i].mq;
            if (tmq && tmq->msg && tmq->msgCount > 0 && tmq->msgCount < 256) {
                osSendMesg(tmq, s_timerList[i].msg, OS_MESG_NOBLOCK);
            }
            if (s_timerList[i].interval_us > 0) {
                s_timerList[i].expiry_us = now + s_timerList[i].interval_us;
            } else {
                s_timerList[i].active = 0;
            }
        }
    }
}

/* ===== VI stubs ===== */

static void *s_framebuffer = NULL;

void osViSetMode(OSViMode *mode) { (void)mode; }
void osViSetEvent(OSMesgQueue *mq, OSMesg msg, u32 retraceCount) {
    (void)mq; (void)msg; (void)retraceCount;
}
void osViSwapBuffer(void *framebuffer) { s_framebuffer = framebuffer; }
void osViBlack(u32 active) { (void)active; }
void osViSetSpecialFeatures(u32 features) { (void)features; }
void *osViGetNextFramebuffer(void) { return s_framebuffer; }
void *osViGetCurrentFramebuffer(void) { return s_framebuffer; }
u32 osViGetStatus(void) { return 0; }
u32 osViGetCurrentLine(void) { return 0; }
void osViSetXScale(f32 scale) { (void)scale; }
void osViSetYScale(f32 scale) { (void)scale; }
void osViRepeatLine(u32 active) { (void)active; }
void osViExtendVStart(u32 extend) { (void)extend; }

/* ===== PI (Peripheral / ROM DMA) stubs ===== */

static OSPiHandle s_piHandle = {0};

OSPiHandle *osCartRomInit(void) { return &s_piHandle; }

void osCreatePiManager(OSPri pri, OSMesgQueue *cmdQ, OSMesg *cmdBuf, s32 cmdBufSize) {
    (void)pri; (void)cmdQ; (void)cmdBuf; (void)cmdBufSize;
}

s32 osPiStartDma(OSIoMesg *mb, s32 pri, s32 direction, u32 devAddr,
                 void *dramAddr, u32 size, OSMesgQueue *mq) {
    /* On PC, the main DMA path goes through ramrom.c's PC romCopy.
     * This stub handles any direct osPiStartDma callers (audi.c audio streaming).
     * devAddr arrives truncated from void* on 64-bit — treat as ROM offset. */
    extern u32  g_romSize;
    extern int platformRomRead(u32 offset, void *destination, u32 bytes);
    (void)mb; (void)pri;
    if (direction == OS_READ && size <= g_romSize && devAddr <= g_romSize - size && dramAddr) {
        (void)platformRomRead(devAddr, dramAddr, size);
    }
    /* Send completion message so osRecvMesg callers don't hang */
    if (mq) {
        osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    }
    return 0;
}

s32 osPiRawReadIo(u32 devAddr, u32 *data) {
    (void)devAddr; if (data) *data = 0; return 0;
}
s32 osPiRawWriteIo(u32 devAddr, u32 data) {
    (void)devAddr; (void)data; return 0;
}
u32 osPiGetStatus(void) { return 0; }

s32 osEPiStartDma(OSPiHandle *handle, OSIoMesg *mb, s32 direction) {
    (void)handle; (void)mb; (void)direction; return 0;
}
s32 osEPiReadIo(OSPiHandle *handle, u32 devAddr, u32 *data) {
    (void)handle; (void)devAddr; if (data) *data = 0; return 0;
}

/* ===== AI (Audio Interface) — SDL queue-based ===== */
static SDL_AudioDeviceID s_aiDev;
static int s_aiOpen;
static u32 s_aiDroppedBuffers;
static PortAiStats s_aiStats;

/* WEB-013: the queue cap bounds how deep the occupancy controller may pre-fill.
 * On web every level load / music decompress / WGPU_COMPAT_WAIT spin is a long
 * synchronous stretch that produces no audio while the once-per-frame pump is
 * stalled, so a 5-frame (167 ms) cap guarantees a hard gap on every transition.
 * Raise it on web so the controller can buffer through those stalls; native
 * keeps the tight cap (its real-time audio thread drains continuously). */
#ifdef __EMSCRIPTEN__
#define AI_QUEUE_LIMIT_FRAMES 12u
#else
#define AI_QUEUE_LIMIT_FRAMES 5u
#endif
#define AI_QUEUE_LIMIT_FRAMES_DETERMINISTIC 4u

/* WEB-069: on web the AudioWorklet backend (web_audio_worklet.c) may own output
 * instead of an SDL device. These two helpers select the backend so the queueing
 * logic below stays single-path. On native, and on web when the worklet is not
 * active, they use SDL exactly as before. */
static inline u32 ai_queued_bytes(void) {
#ifdef PROJECT_TITAN_TGR
    /* Titan exposes free frames rather than queued bytes.  Report the real
     * occupancy to the MGB64 queue controller so it neither floods TGR's
     * bounded ring nor mistakes a full queue for an empty one. */
    {
        const u32 capacity_frames = (64u * 1024u) / 4u;
        u32 free_frames = TitanGeRuntime_AudioFreeFrames();
        if (free_frames > capacity_frames) free_frames = capacity_frames;
        return (capacity_frames - free_frames) * 4u;
    }
#else
#ifdef __EMSCRIPTEN__
    if (webAudioOutputActive()) return webAudioOutputQueuedBytes();
#endif
    return SDL_GetQueuedAudioSize(s_aiDev);
#endif
}
static inline int ai_enqueue(void *buf, u32 size) {   /* 0 == success (SDL contract) */
#ifdef PROJECT_TITAN_TGR
    u32 frames;
    u32 accepted;
    if (buf == NULL || size == 0u || (size & 3u) != 0u) return -1;
    frames = size / 4u;
    accepted = TitanGeRuntime_AudioSubmit((const short *)buf, frames);
    return accepted == frames ? 0 : -1;
#else
#ifdef __EMSCRIPTEN__
    if (webAudioOutputActive()) return webAudioOutputPush(buf, size);
#endif
    return SDL_QueueAudio(s_aiDev, buf, size);
#endif
}

void portAiInit(void) {
    if (s_aiOpen) return;
#ifdef PROJECT_TITAN_TGR
    if (TitanGeRuntime_AudioBegin()) {
        s_aiDev = 1u;
        s_aiOpen = 1;
    }
    return;
#endif
#ifdef __EMSCRIPTEN__
    /* WEB-069: the AudioWorklet backend has no SDL device handle; mark the AI
     * path open so osAiSetNextBuffer routes PCM to it via ai_enqueue(). */
    if (webAudioOutputActive()) {
        s_aiOpen = 1;
        printf("[AI] Using web AudioWorklet output backend\n");
        return;
    }
#endif
    /* Single-device architecture: audio_pc.c owns the SDL audio device.
     * We reuse it here for AI queue writes (osAiSetNextBuffer). */
    {
        extern SDL_AudioDeviceID portAudioGetDevice(void);
        SDL_AudioDeviceID dev = portAudioGetDevice();
        if (dev != 0) {
            s_aiDev = dev;
            s_aiOpen = 1;
            printf("[AI] Using unified SDL audio device\n");
            return;
        }
    }
    printf("[AI] No audio device available (audio disabled?)\n");
}
SDL_AudioDeviceID portAiGetDevice(void) { return s_aiDev; }
u32 osAiGetStatus(void) { return 0; }
s32 osAiSetFrequency(u32 freq) { return (s32)freq; }
/* Audio dump gate: set once at startup, never changes.
 * Checked in osAiSetNextBuffer to avoid calling portAudioDump
 * when dumping is not active — prevents corrupted static crashes. */
static int s_audioDumpEnabled = -1;
s32 osAiSetNextBuffer(void *buf, u32 size) {
    u32 nominal_frame_bytes = portAudioGetFrameSize() * 4;
    u32 live_frame_bytes = nominal_frame_bytes;
    u32 queue_limit;

    /* GE007_AUDIO_DUMP taps the SYNTHESIZED signal, so it must run BEFORE the
     * WEB-045 mute ramp mutates the buffer: the dump exists for native-vs-web /
     * baseline RMS analysis, and the old pause-based mute never touched it —
     * soft-mute zeroing the tap blinded tools/regression_test.sh's audio lane
     * on all 20 levels (release-verify catch, 2026-07-17). Device output is
     * still governed by the ramp below. */
    if (s_audioDumpEnabled < 0)
        s_audioDumpEnabled = (getenv("GE007_AUDIO_DUMP") != NULL) ? 1 : 0;
    if (s_audioDumpEnabled && buf && size > 0) {
        extern void portAudioDump(const void *buf, unsigned int size);
        portAudioDump(buf, size);
    }

    /* WEB-045: soft-mute — attenuate the outgoing PCM in place (a ~5 ms ramp to
     * or from silence) rather than pausing the device. Applied here, the last
     * point before the SDL queue, so it governs every queued buffer (but NOT
     * the diagnostic dump tap above). No-op at unity gain: byte-identical when
     * un-muted, and inert under --deterministic (which never toggles mute).
     * Stereo s16 => size/4 sample-frames. */
    if (buf && size >= 4) {
        extern void portAudioApplyMuteRamp(s16 *samples, s32 sampleFrames);
        portAudioApplyMuteRamp((s16 *)buf, (s32)(size / 4));
    }

    /* Queue limit: drop frames only once backlog grows well beyond the
     * controller's 2-frame target. This is a safety net, not the normal path.
     * Use a conservative NTSC fallback until portAudioInit has established the
     * nominal frame size. */
    if (live_frame_bytes == 0) {
        live_frame_bytes = 2944;
    }
    if (size > live_frame_bytes) {
        live_frame_bytes = size;
    }
    queue_limit = live_frame_bytes * AI_QUEUE_LIMIT_FRAMES;

    if (s_aiOpen && buf && size > 0) {
        const u32 queued = ai_queued_bytes();
        u32 effective_limit = queue_limit;

        s_aiStats.queue_before_bytes = queued;
        s_aiStats.queue_after_bytes = queued;
        s_aiStats.requested_bytes = size;
        s_aiStats.accepted_bytes = 0;

        if (g_deterministic) {
            const u32 deterministic_limit = nominal_frame_bytes * AI_QUEUE_LIMIT_FRAMES_DETERMINISTIC;
            const u32 limit = deterministic_limit != 0 ? deterministic_limit : 11776;
            effective_limit = limit;

            /* Preserve the original deterministic queueing contract used by
             * regression baselines. Validation runs care about bit-stability
             * more than live queue resilience. */
            if (queued < limit) {
                /* AUDIT-0068: credit acceptance only if the queue call actually
                 * succeeded; a failed SDL_QueueAudio is a dropped buffer, not
                 * accepted output. (Under the deterministic dummy driver the call
                 * always succeeds, so baselines are unchanged.) */
                if (ai_enqueue(buf, size) == 0) {
                    s_aiStats.accepted_bytes = size;
                    s_aiStats.queue_after_bytes = ai_queued_bytes();
                } else {
                    s_aiDroppedBuffers++;
                    s_aiStats.dropped_bytes += size;
                }
            } else {
                s_aiDroppedBuffers++;
                s_aiStats.dropped_bytes += size;
            }
        } else {
            /* Apply the cap to post-enqueue occupancy, not just the current
             * queue. A strict pre-enqueue check can still overshoot by one
             * full frame and then trip a drop on the following callback under
             * normal host jitter. */
            if (queued <= queue_limit && size <= (queue_limit - queued)) {
                /* AUDIT-0068: only count bytes the backend actually accepted. */
                if (ai_enqueue(buf, size) == 0) {
                    s_aiStats.accepted_bytes = size;
                    s_aiStats.queue_after_bytes = ai_queued_bytes();
                } else {
                    s_aiDroppedBuffers++;
                    s_aiStats.dropped_bytes += size;
                }
            } else {
                s_aiDroppedBuffers++;
                s_aiStats.dropped_bytes += size;
            }
        }

        s_aiStats.queue_limit_bytes = effective_limit;
        s_aiStats.dropped_buffers = s_aiDroppedBuffers;
    }
    /* Dump tap moved ABOVE the mute ramp (see the WEB-045 block) so the
     * diagnostic capture records the synthesized signal even while muted. */
    return 0;
}

/* PERF-035: predicate for portAudioPrefillQueue (audi_port.c). Reports whether
 * the AI queue sits below the same per-frame cap osAiSetNextBuffer enforces,
 * without duplicating AI_QUEUE_LIMIT_FRAMES at the call site. Mirrors the
 * queue_limit computation above (nominal frame bytes * cap). Web-only in
 * practice — the prefill is gated on __EMSCRIPTEN__ + !g_deterministic — but the
 * predicate is harmless on native (returns 0 when no device is open). */
int osAiQueueBelowLimit(void) {
    u32 live_frame_bytes;
    u32 queue_limit;
    if (!s_aiOpen) return 0;
    live_frame_bytes = portAudioGetFrameSize() * 4;
    if (live_frame_bytes == 0) {
        live_frame_bytes = 2944;
    }
    queue_limit = live_frame_bytes * AI_QUEUE_LIMIT_FRAMES;
    return ai_queued_bytes() < queue_limit;
}

/* AUDIT-0068: on SDL_AUDIODEVICEREMOVED for our opened device, invalidate the
 * cached device so osAiSetNextBuffer stops queueing to a dead handle (its guard
 * is `s_aiOpen`). Called from the SDL event loop. `which` is the SDL_AudioDeviceID
 * SDL reports for a removed opened device. Automatic re-open/recovery is a
 * separate (hardware-dependent) follow-up. */
void osAiNotifyDeviceRemoved(unsigned int which) {
    if (s_aiOpen && (SDL_AudioDeviceID)which == s_aiDev) {
        s_aiOpen = 0;
        fprintf(stderr, "[AUDIO] output device %u removed; audio output stopped\n", which);
    }
}
u32 osAiGetLength(void) {
    /* In deterministic mode, return 0 so frame sizing is identical across
     * runs regardless of SDL audio drain timing. */
    if (g_deterministic) return 0;
    if (!s_aiOpen) return 0;
    /* Return actual queue size in bytes.  The caller (portAudioFrame) uses this
     * as a queue occupancy signal for adaptive frame sizing, not as an N64 DMA
     * remainder. On web this is the AudioWorklet ring estimate (ai_queued_bytes). */
    return ai_queued_bytes();
}

u32 portAiGetDroppedBufferCount(void) {
    return s_aiDroppedBuffers;
}

void portAiGetStats(PortAiStats *stats) {
    if (stats) {
        *stats = s_aiStats;
    }
}

/* ===== DP (Display Processor) stubs ===== */

u32 osDpGetStatus(void) { return 0; }
void osDpSetStatus(u32 status) { (void)status; }
void osDpGetCounters(u32 *counters) {
    if (counters) { counters[0] = 0; counters[1] = 0; counters[2] = 0; counters[3] = 0; }
}
s32 osDpSetNextBuffer(void *buf, u64 size) { (void)buf; (void)size; return 0; }

/* ===== SP stubs ===== */

u32 __osSpGetStatus(void) { return 0; }
void __osSpSetStatus(u32 status) { (void)status; }
void osSpTaskLoad(OSTask *task) { (void)task; }
void osSpTaskStartGo(OSTask *task) { (void)task; }
void osSpTaskYield(void) {}
OSYieldResult osSpTaskYielded(OSTask *task) { (void)task; return 0; }

/* ===== Controller stubs ===== */

/* Effective connected-player count on the native port. Player 1 (slot 0) is
 * always present because the keyboard/mouse drives it even with no pad; opened
 * pads beyond the first add players for split-screen. Clamped to MAXCONTROLLERS.
 * This is what makes joyGetControllerCount() report >=2 once a 2nd pad is in,
 * unblocking the front-end multiplayer gate. */
static int pcConnectedPlayerCount(void) {
    int pads = platformGetPadCount();
    int count = pads > 1 ? pads : 1; /* keyboard/mouse guarantees >=1 */
    if (count > MAXCONTROLLERS) count = MAXCONTROLLERS;
    return count;
}

/* Mark the first `count` slots of an OSContStatus array as a present standard
 * controller and the rest as not responding, so the contiguous-slot logic in
 * joyCheckStatus()/joyGetControllerCount() derives the right player count. */
static void pcFillContStatus(OSContStatus *data, int count) {
    int i;
    if (!data) return;
    memset(data, 0, sizeof(OSContStatus) * MAXCONTROLLERS);
    for (i = 0; i < MAXCONTROLLERS; i++) {
        if (i < count) {
            data[i].type   = CONT_TYPE_NORMAL;
            data[i].status = 0;
            data[i].errnum = 0;
        } else {
            data[i].errnum = CONT_NO_RESPONSE_ERROR;
        }
    }
}

s32 osContInit(OSMesgQueue *mq, u8 *bitpattern, OSContStatus *data) {
    int count = pcConnectedPlayerCount();
    if (bitpattern) {
        /* Low `count` bits set = those player slots present. */
        *bitpattern = (u8)((1u << count) - 1u);
    }
    pcFillContStatus(data, count);
    /* Prime the input queue so the first joyPoll() call succeeds.
     * On N64, the SI interrupt from osContInit fires and posts to the queue;
     * on PC we simulate that by posting a message directly. */
    if (mq) osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}
s32 osContReset(OSMesgQueue *mq, OSContStatus *data) {
    (void)mq; (void)data; return 0;
}
s32 osContStartQuery(OSMesgQueue *mq) {
    if (mq) osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}
s32 osContStartReadData(OSMesgQueue *mq) {
    if (mq) osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}
/* Re-derive connection state each query so hot-plug is reflected: joyCheckStatus
 * reads g_ContStatus[i].errnum here and rebuilds g_ConnectedControllers. */
s32 osContGetQuery(OSContStatus *data) {
    pcFillContStatus(data, pcConnectedPlayerCount());
    return 0;
}
/* Mouse delta from platform_sdl.c */
extern void platformGetMouseDelta(int *dx, int *dy);
extern int g_pcDebugFlyCamera;

/* Gamepad from platform_sdl.c */
extern SDL_GameController *g_gameController;

/* PC input state from platform_sdl.c */
extern int g_pcEscapePressed;
extern int g_pcCrouchToggle;
extern int g_pcMouseRegrabFrame;
extern int platformGetMouseWheel(void);

/* Deadzone for gamepad analog sticks (out of 32767) */
#define GAMEPAD_DEADZONE 8000
/* Threshold for right stick → C-button conversion */
#define CSTICK_THRESHOLD 16000

/* PC input state consumed by bondview.c NATIVE_PORT block.
 * g_pcWeaponCycleForward/Back are queued step counts, not booleans: each
 * mouse-wheel notch (or edge-triggered key/pad press) adds one pending
 * step, and bondviewApplyNativeWeaponCycle() drains one step per tick so
 * a fast multi-notch scroll produces one switch per notch instead of
 * collapsing into a single switch (M2.2). */
int g_pcWeaponCycleForward = 0;
int g_pcWeaponCycleBack = 0;

/* pcQueueWeaponCycleSteps() (queue) and pcDrainWeaponCycleStep() (drain, used by
 * bondview.c) now live in the pure src/platform/weapon_cycle_queue.c TU so the
 * runtime paths and the ROM-free unit test share one implementation (FID-0016 /
 * M2.2). See weapon_cycle_queue.h. */
int g_pcCrouchRequest = 0;  /* 1 = toggle requested this frame */
int g_pcScriptedMouseDeltaX = 0;
int g_pcScriptedMouseDeltaY = 0;
int g_pcBridgeRightStickX = 0;
int g_pcBridgeRightStickY = 0;

typedef struct PcScriptedInputWindow {
    int start_frame;
    int end_frame;
} PcScriptedInputWindow;

#define PC_MAX_SCRIPTED_INPUT_WINDOWS 128
#define PC_MAX_SCRIPTED_WARP_EVENTS 1024

typedef struct PcScriptedInputPattern {
    int initialized;
    int count;
    PcScriptedInputWindow windows[PC_MAX_SCRIPTED_INPUT_WINDOWS];
} PcScriptedInputPattern;

typedef struct PcScriptedWarpEvent {
    int frame;
    int pad;
    int has_offset;
    float right_offset;
    float forward_offset;
    float y_offset;
    int applied;
} PcScriptedWarpEvent;

typedef struct PcScriptedWarpChrEvent {
    int frame;
    int chrnum;
    float distance;
    int has_angle;
    float angle_deg;
    int applied;
} PcScriptedWarpChrEvent;

typedef struct PcScriptedGuardSpawnEvent {
    int frame;
    int mode;
    int source_chrnum;
    int target_chrnum;
    int bodynum;
    int headnum;
    int ailist_id;
    int flags;
    int clear_seen_gate;
    int applied;
} PcScriptedGuardSpawnEvent;

typedef struct PcScriptedSetChrAIEvent {
    int frame;
    int chrnum;
    int ailist_id;
    int applied;
} PcScriptedSetChrAIEvent;

/* Deterministic test hook: OR `set_mask` and AND-NOT `clr_mask` into a chr's
 * chrflags at a frame. Two masks (not one) so a single event can both set
 * CHRFLAG_HIDDEN and clear CHRFLAG_00040000 — the combination the H2 AI-freeze
 * gate (chr.c) requires. masks are unsigned so 0 is a legitimate no-op and large
 * bits (0x00040000) never go negative. */
typedef struct PcScriptedSetChrFlagEvent {
    int frame;
    int chrnum;
    unsigned int set_mask;
    unsigned int clr_mask;
    int applied;
} PcScriptedSetChrFlagEvent;

typedef struct PcScriptedChrToPadEvent {
    int frame;
    int chrnum;
    int pad;
    int stop;
    int applied;
} PcScriptedChrToPadEvent;

typedef struct PcScriptedForceChrEvent {
    int frame;
    int chrnum;
    float x;
    float y;
    float z;
    float yaw_deg;
    int pad;
    int stop;
    int applied;
} PcScriptedForceChrEvent;

typedef struct PcScriptedDamageChrEvent {
    int frame;
    int chrnum;
    float amount;
    int applied;
} PcScriptedDamageChrEvent;

typedef struct PcScriptedObjectTagEvent {
    int frame;
    int tag;
    float amount;
    int applied;
} PcScriptedObjectTagEvent;

typedef struct PcScriptedStageFlagEvent {
    int frame;
    u32 flags;
    int applied;
} PcScriptedStageFlagEvent;

typedef struct PcScriptedAttackRollChrEvent {
    int frame;
    int chrnum;
    int side;
    int applied;
} PcScriptedAttackRollChrEvent;

typedef struct PcScriptedAIMoveChrEvent {
    int frame;
    int chrnum;
    int mode;
    int applied;
} PcScriptedAIMoveChrEvent;

typedef struct PcScriptedAIReactChrEvent {
    int frame;
    int chrnum;
    int mode;
    int applied;
} PcScriptedAIReactChrEvent;

typedef struct PcScriptedFaceChrEvent {
    int frame;
    int chrnum;
    float yaw_offset_deg;
    int applied;
} PcScriptedFaceChrEvent;

typedef struct PcScriptedFaceCoordEvent {
    int frame;
    float x;
    float y;
    float z;
    float yaw_offset_deg;
    float pitch_offset_deg;
    int applied;
} PcScriptedFaceCoordEvent;

typedef struct PcScriptedForcePlayerEvent {
    int frame;
    float x;
    float y;
    float z;
    float yaw_deg;
    float pitch_deg;
    int has_camera_height;
    float camera_height;
    int pad;
    int applied;
} PcScriptedForcePlayerEvent;

typedef struct PcScriptedEquipEvent {
    int frame;
    int item;
    int applied;
} PcScriptedEquipEvent;

typedef struct PcScriptedSetHandAmmoEvent {
    int frame;
    int hand;
    int mag;
    int reserve;
    int applied;
} PcScriptedSetHandAmmoEvent;

enum {
    PC_AI_MOVE_RUN_FROM_BOND = 1,
    PC_AI_MOVE_FIND_COVER = 2,
    PC_AI_MOVE_SIDESTEP = 3,
    PC_AI_MOVE_SIDEHOP = 4,
    PC_AI_MOVE_SIDERUN = 5,
    PC_AI_MOVE_FIRE_WALK = 6,
    PC_AI_MOVE_FIRE_RUN = 7,
    PC_AI_MOVE_FIRE_ROLL = 8,
};

enum {
    PC_AI_REACT_POINT = 1,
    PC_AI_REACT_LOOK = 2,
    PC_AI_REACT_SURRENDER = 3,
    PC_AI_REACT_STARTALARM = 4,
    PC_AI_REACT_DROP_SURRENDER = 5,
    PC_AI_REACT_DROP_FRESH_RIGHT = 6,
};

enum {
    PC_GUARD_SPAWN_MODE_CHR = 1,
    PC_GUARD_SPAWN_MODE_CLONE = 2,
};

static PcScriptedInputPattern s_autoFirePattern;
static PcScriptedInputPattern s_autoReloadPattern;
static PcScriptedInputPattern s_autoAimPattern;
static PcScriptedInputPattern s_autoAPattern;
static PcScriptedInputPattern s_autoBPattern;
static PcScriptedInputPattern s_autoStartPattern;
static PcScriptedInputPattern s_autoLPattern;
static PcScriptedInputPattern s_autoRPattern;
static PcScriptedInputPattern s_autoCUpPattern;
static PcScriptedInputPattern s_autoCDownPattern;
static PcScriptedInputPattern s_autoCLeftPattern;
static PcScriptedInputPattern s_autoCRightPattern;
static PcScriptedInputPattern s_autoCrouchPattern;
static PcScriptedInputPattern s_autoDPadUpPattern;
static PcScriptedInputPattern s_autoDPadDownPattern;
static PcScriptedInputPattern s_autoDPadLeftPattern;
static PcScriptedInputPattern s_autoDPadRightPattern;
static PcScriptedInputPattern s_autoMenuUpPattern;
static PcScriptedInputPattern s_autoMenuDownPattern;
static PcScriptedInputPattern s_autoMenuLeftPattern;
static PcScriptedInputPattern s_autoMenuRightPattern;
static PcScriptedInputPattern s_autoFrontendUpPattern;
static PcScriptedInputPattern s_autoFrontendDownPattern;
static PcScriptedInputPattern s_autoFrontendLeftPattern;
static PcScriptedInputPattern s_autoFrontendRightPattern;
static PcScriptedInputPattern s_autoForwardPattern;
static PcScriptedInputPattern s_autoBackPattern;
static PcScriptedInputPattern s_autoLeftPattern;
static PcScriptedInputPattern s_autoRightPattern;
static PcScriptedInputPattern s_autoLookLeftPattern;
static PcScriptedInputPattern s_autoLookRightPattern;
static PcScriptedInputPattern s_autoLookUpPattern;
static PcScriptedInputPattern s_autoLookDownPattern;
static PcScriptedInputPattern s_autoWeaponNextPattern;
static PcScriptedInputPattern s_autoWeaponPrevPattern;
static int s_autoCrouchPrevActive = 0;
static int s_autoWarpInitialized = 0;
static int s_autoWarpCount = 0;
static PcScriptedWarpEvent s_autoWarpEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoWarpChrInitialized = 0;
static int s_autoWarpChrCount = 0;
static PcScriptedWarpChrEvent s_autoWarpChrEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoGuardSpawnInitialized = 0;
static int s_autoGuardSpawnCount = 0;
static PcScriptedGuardSpawnEvent s_autoGuardSpawnEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoSetChrAIInitialized = 0;
static int s_autoSetChrAICount = 0;
static PcScriptedSetChrAIEvent s_autoSetChrAIEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoSetChrFlagInitialized = 0;
static int s_autoSetChrFlagCount = 0;
static PcScriptedSetChrFlagEvent s_autoSetChrFlagEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoChrToPadInitialized = 0;
static int s_autoChrToPadCount = 0;
static PcScriptedChrToPadEvent s_autoChrToPadEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoForceChrInitialized = 0;
static int s_autoForceChrCount = 0;
static PcScriptedForceChrEvent s_autoForceChrEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoDamageChrInitialized = 0;
static int s_autoDamageChrCount = 0;
static PcScriptedDamageChrEvent s_autoDamageChrEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoCollectTagInitialized = 0;
static int s_autoCollectTagCount = 0;
static PcScriptedObjectTagEvent s_autoCollectTagEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoDamageTagInitialized = 0;
static int s_autoDamageTagCount = 0;
static PcScriptedObjectTagEvent s_autoDamageTagEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoSetStageFlagsInitialized = 0;
static int s_autoSetStageFlagsCount = 0;
static PcScriptedStageFlagEvent s_autoSetStageFlagsEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoUnsetStageFlagsInitialized = 0;
static int s_autoUnsetStageFlagsCount = 0;
static PcScriptedStageFlagEvent s_autoUnsetStageFlagsEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoAttackRollChrInitialized = 0;
static int s_autoAttackRollChrCount = 0;
static PcScriptedAttackRollChrEvent s_autoAttackRollChrEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoAIMoveChrInitialized = 0;
static int s_autoAIMoveChrCount = 0;
static PcScriptedAIMoveChrEvent s_autoAIMoveChrEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoAIReactChrInitialized = 0;
static int s_autoAIReactChrCount = 0;
static PcScriptedAIReactChrEvent s_autoAIReactChrEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoFaceChrInitialized = 0;
static int s_autoFaceChrCount = 0;
static PcScriptedFaceChrEvent s_autoFaceChrEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoFaceCoordInitialized = 0;
static int s_autoFaceCoordCount = 0;
static PcScriptedFaceCoordEvent s_autoFaceCoordEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoForcePlayerInitialized = 0;
static int s_autoForcePlayerCount = 0;
static PcScriptedForcePlayerEvent s_autoForcePlayerEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoTankFaceCoordInitialized = 0;
static int s_autoTankFaceCoordCount = 0;
static PcScriptedFaceCoordEvent s_autoTankFaceCoordEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoArmorInitialized = 0;
static int s_autoArmorFrame = -1;
static float s_autoArmorAmount = 0.0f;
static int s_autoArmorDone = 0;
/* GE007_AUTO_MPMENU: force the multiplayer watch/pause menu open (mpmenuon) on
 * every split-screen pane from the given frame onward. Deterministic stand-in
 * for a per-pane Start press, which the scripted-input router cannot deliver to
 * pads 1-3 (it only fills controller 0). Regression lane FID-0064. */
static int s_autoMpMenuInitialized = 0;
static int s_autoMpMenuFrame = -1;
static int s_autoDamageBondInitialized = 0;
static int s_autoDamageBondFrame = -1;
static float s_autoDamageBondAmount = 0.125f;
static float s_autoDamageBondAngle = 0.0f;
static int s_autoDamageBondAffectsArmor = 1;
static int s_autoDamageBondDone = 0;
static int s_autoWeaponAmmoInitialized = 0;
static int s_autoWeaponAmmoFrame = -1;
static int s_autoWeaponAmmoItem = -1;
static int s_autoWeaponAmmoAmount = -1;
static int s_autoWeaponAmmoDone = 0;
static int s_autoSetHandAmmoInitialized = 0;
static int s_autoSetHandAmmoFrame = -1;
static int s_autoSetHandAmmoHand = -1;
static int s_autoSetHandAmmoMag = -1;
static int s_autoSetHandAmmoReserve = -1;
static int s_autoSetHandAmmoDone = 0;
static int s_autoSetHandAmmoTrace = 0;
static int s_autoSetHandAmmoCount = 0;
static PcScriptedSetHandAmmoEvent s_autoSetHandAmmoEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoEquipInitialized = 0;
static int s_autoEquipCount = 0;
static PcScriptedEquipEvent s_autoEquipEvents[PC_MAX_SCRIPTED_WARP_EVENTS];
static int s_autoEquipFrame = -1;
static int s_autoEquipItem = -1;
static int s_autoEquipDone = 0;
static int s_autoEquipDualInitialized = 0;
static int s_autoEquipDualFrame = -1;
static int s_autoEquipDualRight = -1;
static int s_autoEquipDualLeft = -1;
static int s_autoEquipDualDone = 0;
static int s_autoMissionEndInitialized = 0;
static int s_autoMissionEndApplied = 0;
static int s_autoMissionEndFrame = -1;
static int s_autoMissionEndResult = 0;
static int s_autoMissionEndRestoreDebugFlag = 0;
static int s_autoMissionEndSavedDebugFlag = 0;
static int s_autoCameraModeInitialized = 0;
static int s_autoCameraModeFrame = -1;
static int s_autoCameraMode = -1;
static int s_autoCameraPosEndPad = -1;
static int s_autoCameraModeDone = 0;
static int s_autoCameraModeWarned = 0;
static int s_autoExitOnTitleInitialized = 0;
static int s_autoExitOnTitle = 0;
static int s_autoExitOnTitleDelay = 2;
static int s_autoExitOnTitleSeenFrame = -1;
static int s_autoExitOnTitleApplied = 0;
static int s_autoExitFrameInitialized = 0;
static int s_autoExitFrame = -1;
static int s_autoExitFrameApplied = 0;
static int s_autoUnlockSoloInitialized = 0;
static int s_autoUnlockSoloDone = 0;
static char s_autoUnlockSoloSpec[256];
static int s_autoUnlockSoloFolder = FOLDER1;

enum {
    PC_MISSION_END_INVALID = -1,
    PC_MISSION_END_SUCCESS = 0,
    PC_MISSION_END_FAIL = 1,
    PC_MISSION_END_ABORT = 2,
    PC_MISSION_END_KIA = 3,
};
static int s_autoAddItemInitialized = 0;
static int s_autoAddItemFrame = -1;
static int s_autoAddItem = -1;
static int s_autoAddItemDone = 0;
static int s_autoAddDualInitialized = 0;
static int s_autoAddDualFrame = -1;
static int s_autoAddDualRight = -1;
static int s_autoAddDualLeft = -1;
static int s_autoAddDualDone = 0;

enum { PC_MAX_SCRIPTED_SFX_EVENTS = 32 };
typedef struct PcScriptedSfxEvent_s {
    int frame;
    int sound;
    int applied;
} PcScriptedSfxEvent;

enum { PC_MAX_SCRIPTED_RNG_SEED_EVENTS = 64 };
typedef struct PcScriptedRngSeedEvent_s {
    int frame;
    u64 seed;
    int applied;
} PcScriptedRngSeedEvent;

enum { PC_MAX_SCRIPTED_AUTOAIM_EVENTS = 32 };
typedef struct PcScriptedAutoAimEvent_s {
    int frame;
    int enabled;
    int applied;
} PcScriptedAutoAimEvent;

static int s_autoPlaySfxInitialized = 0;
static int s_autoPlaySfxCount = 0;
static PcScriptedSfxEvent s_autoPlaySfxEvents[PC_MAX_SCRIPTED_SFX_EVENTS];
static int s_autoRngSeedInitialized = 0;
static int s_autoRngSeedCount = 0;
static PcScriptedRngSeedEvent s_autoRngSeedEvents[PC_MAX_SCRIPTED_RNG_SEED_EVENTS];
static int s_autoAutoAimInitialized = 0;
static int s_autoAutoAimCount = 0;
static PcScriptedAutoAimEvent s_autoAutoAimEvents[PC_MAX_SCRIPTED_AUTOAIM_EVENTS];
static ALSoundState *s_autoPlaySfxLastState = NULL;
static int s_autoStopSfxInitialized = 0;
static int s_autoStopSfxFrame = -1;
static int s_autoStopSfxDone = 0;
static int s_autoFxSfxInitialized = 0;
static int s_autoFxSfxFrame = -1;
static int s_autoFxSfxMix = 0;
static int s_autoFxSfxDone = 0;

static int pcGetScriptedLookStep(void)
{
    static int initialized = 0;
    static int look_step = 12;

    if (!initialized) {
        const char *env = getenv("GE007_AUTO_LOOK_STEP");

        if (env != NULL && *env != '\0') {
            long parsed = strtol(env, NULL, 10);

            if (parsed < 1) {
                parsed = 1;
            } else if (parsed > 64) {
                parsed = 64;
            }

            look_step = (int)parsed;
        }

        initialized = 1;
    }

    return look_step;
}

static void pcAddScriptedRngSeedEvent(int frame, u64 seed)
{
    if (frame < 0 || s_autoRngSeedCount >= PC_MAX_SCRIPTED_RNG_SEED_EVENTS) {
        return;
    }

    s_autoRngSeedEvents[s_autoRngSeedCount].frame = frame;
    s_autoRngSeedEvents[s_autoRngSeedCount].seed = seed;
    s_autoRngSeedEvents[s_autoRngSeedCount].applied = 0;
    s_autoRngSeedCount++;
}

static void pcParseScriptedRngSeedEvents(const char *script)
{
    while (script != NULL && *script != '\0' &&
           s_autoRngSeedCount < PC_MAX_SCRIPTED_RNG_SEED_EVENTS) {
        char *endptr;
        long frame;
        long frame_end;
        unsigned long long seed;

        while (*script == ' ' || *script == '\t' ||
               *script == ',' || *script == ';') {
            script++;
        }

        if (*script == '\0') {
            break;
        }

        frame = strtol(script, &endptr, 10);
        if (endptr == script || *endptr != ':') {
            break;
        }

        script = endptr + 1;
        seed = strtoull(script, &endptr, 0);
        if (endptr == script) {
            break;
        }

        pcAddScriptedRngSeedEvent((int)frame, (u64)seed);
        script = endptr;
    }
}

static void pcMaybeApplyScriptedRngSeed(int input_frame)
{
    int i;

    if (!s_autoRngSeedInitialized) {
        const char *script_env;
        const char *frame_env;
        const char *seed_env;

        s_autoRngSeedInitialized = 1;
        memset(s_autoRngSeedEvents, 0, sizeof(s_autoRngSeedEvents));

        script_env = getenv("GE007_AUTO_RNG_SEED_SCRIPT");
        pcParseScriptedRngSeedEvents(script_env);

        frame_env = getenv("GE007_AUTO_RNG_SEED_FRAME");
        seed_env = getenv("GE007_AUTO_RNG_SEED");
        if (s_autoRngSeedCount == 0 &&
            frame_env != NULL && *frame_env != '\0' &&
            seed_env != NULL && *seed_env != '\0') {
            char *frame_end = NULL;
            char *seed_end = NULL;
            long frame = strtol(frame_env, &frame_end, 10);
            unsigned long long seed = strtoull(seed_env, &seed_end, 0);

            if (frame_end != frame_env && *frame_end == '\0' &&
                seed_end != seed_env && *seed_end == '\0') {
                pcAddScriptedRngSeedEvent((int)frame, (u64)seed);
            }
        }
    }

    for (i = 0; i < s_autoRngSeedCount; i++) {
        if (s_autoRngSeedEvents[i].applied ||
            input_frame != s_autoRngSeedEvents[i].frame) {
            continue;
        }

        g_randomSeed = s_autoRngSeedEvents[i].seed;
        s_autoRngSeedEvents[i].applied = 1;
        fprintf(stderr,
                "[AUTO_RNG_SEED] frame=%d seed=0x%016llX\n",
                input_frame,
                (unsigned long long)g_randomSeed);
    }
}

static void pcAddScriptedAutoAimEvent(int frame, int enabled)
{
    if (frame < 0 || s_autoAutoAimCount >= PC_MAX_SCRIPTED_AUTOAIM_EVENTS) {
        return;
    }

    s_autoAutoAimEvents[s_autoAutoAimCount].frame = frame;
    s_autoAutoAimEvents[s_autoAutoAimCount].enabled = enabled ? 1 : 0;
    s_autoAutoAimEvents[s_autoAutoAimCount].applied = 0;
    s_autoAutoAimCount++;
}

static void pcParseScriptedAutoAimEvents(const char *script)
{
    while (script != NULL && *script != '\0' &&
           s_autoAutoAimCount < PC_MAX_SCRIPTED_AUTOAIM_EVENTS) {
        char *endptr;
        long frame;
        long enabled;

        while (*script == ' ' || *script == '\t' ||
               *script == ',' || *script == ';') {
            script++;
        }

        if (*script == '\0') {
            break;
        }

        frame = strtol(script, &endptr, 10);
        if (endptr == script || *endptr != ':') {
            break;
        }

        script = endptr + 1;
        enabled = strtol(script, &endptr, 0);
        if (endptr == script) {
            break;
        }

        pcAddScriptedAutoAimEvent((int)frame, enabled != 0);
        script = endptr;
    }
}

static void pcMaybeApplyScriptedAutoAim(int input_frame)
{
    int i;

    if (!s_autoAutoAimInitialized) {
        const char *script_env;
        const char *frame_env;
        const char *enabled_env;

        s_autoAutoAimInitialized = 1;
        memset(s_autoAutoAimEvents, 0, sizeof(s_autoAutoAimEvents));

        script_env = getenv("GE007_AUTO_AUTOAIM_SCRIPT");
        pcParseScriptedAutoAimEvents(script_env);

        frame_env = getenv("GE007_AUTO_AUTOAIM_FRAME");
        enabled_env = getenv("GE007_AUTO_AUTOAIM");
        if (s_autoAutoAimCount == 0 &&
            frame_env != NULL && *frame_env != '\0' &&
            enabled_env != NULL && *enabled_env != '\0') {
            char *frame_end = NULL;
            char *enabled_end = NULL;
            long frame = strtol(frame_env, &frame_end, 10);
            long enabled = strtol(enabled_env, &enabled_end, 0);

            if (frame_end != frame_env && *frame_end == '\0' &&
                enabled_end != enabled_env && *enabled_end == '\0') {
                pcAddScriptedAutoAimEvent((int)frame, enabled != 0);
            }
        }
    }

    for (i = 0; i < s_autoAutoAimCount; i++) {
        if (s_autoAutoAimEvents[i].applied ||
            input_frame != s_autoAutoAimEvents[i].frame) {
            continue;
        }

        cur_player_set_autoaim((u32)s_autoAutoAimEvents[i].enabled);
        if (g_CurrentPlayer != NULL) {
            set_BONDdata_autoaim_x(s_autoAutoAimEvents[i].enabled);
            set_BONDdata_autoaim_y(s_autoAutoAimEvents[i].enabled);
            g_CurrentPlayer->autoaim_target_x = NULL;
            g_CurrentPlayer->autoaim_target_y = NULL;
            g_CurrentPlayer->autoaimx = 0.0f;
            g_CurrentPlayer->autoaimy = 0.0f;
        }
        s_autoAutoAimEvents[i].applied = 1;
        fprintf(stderr,
                "[AUTO_AUTOAIM] frame=%d enabled=%d\n",
                input_frame,
                s_autoAutoAimEvents[i].enabled);
    }
}

static float pcWrapYawDegrees(float yaw)
{
    while (yaw < 0.0f) {
        yaw += 360.0f;
    }

    while (yaw >= 360.0f) {
        yaw -= 360.0f;
    }

    return yaw;
}

static float pcSignedAtan2f(float y, float x)
{
    float angle = atan2f(y, x);

    if (angle > M_PI_F) {
        angle -= M_TAU_F;
    }

    return angle;
}

#if defined(VERSION_EU)
#define PC_SCRIPTED_FIELD_6C_FACTOR 0.20039999485f
#define PC_SCRIPTED_FIELD_3B8_FACTOR 0.118799984455f
#else
#define PC_SCRIPTED_FIELD_6C_FACTOR 0.170000016689f
#define PC_SCRIPTED_FIELD_3B8_FACTOR 0.100000023842f
#endif

static void pcInitScriptedWarp(void)
{
    const char *script_env;

    if (s_autoWarpInitialized) {
        return;
    }

    s_autoWarpInitialized = 1;
    memset(s_autoWarpEvents, 0, sizeof(s_autoWarpEvents));

    script_env = getenv("GE007_AUTO_WARP_SCRIPT");

    if (script_env != NULL && *script_env != '\0' &&
        !(script_env[0] == '0' && script_env[1] == '\0')) {
        while (*script_env != '\0' && s_autoWarpCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            char *endptr;
            long frame;
            long pad;
            int has_offset = 0;
            float right_offset = 0.0f;
            float forward_offset = 0.0f;
            float y_offset = 0.0f;

            while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
                script_env++;
            }

            if (*script_env == '\0') {
                break;
            }

            frame = strtol(script_env, &endptr, 10);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            pad = strtol(script_env, &endptr, 10);
            if (endptr == script_env) {
                break;
            }

            script_env = endptr;

            if (*script_env == ':') {
                script_env++;
                right_offset = strtof(script_env, &endptr);
                if (endptr == script_env) {
                    break;
                }
                script_env = endptr;

                if (*script_env != ':') {
                    break;
                }

                script_env++;
                forward_offset = strtof(script_env, &endptr);
                if (endptr == script_env) {
                    break;
                }
                script_env = endptr;

                if (*script_env == ':') {
                    script_env++;
                    y_offset = strtof(script_env, &endptr);
                    if (endptr == script_env) {
                        break;
                    }
                    script_env = endptr;
                }

                has_offset = 1;
            }

            s_autoWarpEvents[s_autoWarpCount].frame = (int)frame;
            s_autoWarpEvents[s_autoWarpCount].pad = (int)pad;
            s_autoWarpEvents[s_autoWarpCount].has_offset = has_offset;
            s_autoWarpEvents[s_autoWarpCount].right_offset = right_offset;
            s_autoWarpEvents[s_autoWarpCount].forward_offset = forward_offset;
            s_autoWarpEvents[s_autoWarpCount].y_offset = y_offset;
            s_autoWarpEvents[s_autoWarpCount].applied = 0;
            s_autoWarpCount++;
        }
    }

    if (s_autoWarpCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_WARP_FRAME");
        const char *pad_env = getenv("GE007_AUTO_WARP_PAD");
        const char *right_env = getenv("GE007_AUTO_WARP_PAD_RIGHT_OFFSET");
        const char *forward_env = getenv("GE007_AUTO_WARP_PAD_FORWARD_OFFSET");
        const char *y_env = getenv("GE007_AUTO_WARP_PAD_Y_OFFSET");
        int frame = -1;
        int pad = -1;
        int has_offset = 0;
        float right_offset = 0.0f;
        float forward_offset = 0.0f;
        float y_offset = 0.0f;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 10);
        }

        if (pad_env != NULL && *pad_env != '\0') {
            pad = (int)strtol(pad_env, NULL, 10);
        }

        if (right_env != NULL && *right_env != '\0') {
            right_offset = strtof(right_env, NULL);
            has_offset = 1;
        }

        if (forward_env != NULL && *forward_env != '\0') {
            forward_offset = strtof(forward_env, NULL);
            has_offset = 1;
        }

        if (y_env != NULL && *y_env != '\0') {
            y_offset = strtof(y_env, NULL);
            has_offset = 1;
        }

        if (frame >= 0 && pad >= 0) {
            s_autoWarpEvents[0].frame = frame;
            s_autoWarpEvents[0].pad = pad;
            s_autoWarpEvents[0].has_offset = has_offset;
            s_autoWarpEvents[0].right_offset = right_offset;
            s_autoWarpEvents[0].forward_offset = forward_offset;
            s_autoWarpEvents[0].y_offset = y_offset;
            s_autoWarpEvents[0].applied = 0;
            s_autoWarpCount = 1;
        }
    }
}

static void pcMaybeApplyScriptedWarp(int input_frame)
{
    int i;

    pcInitScriptedWarp();

    if (!g_deterministic || s_autoWarpCount == 0) {
        return;
    }

    for (i = 0; i < s_autoWarpCount; i++) {
        if (s_autoWarpEvents[i].applied ||
            s_autoWarpEvents[i].frame < 0 ||
            s_autoWarpEvents[i].pad < 0 ||
            input_frame != s_autoWarpEvents[i].frame) {
            continue;
        }

        if (s_autoWarpEvents[i].has_offset
                ? portWarpBondToPadOffsetY(s_autoWarpEvents[i].pad,
                                           s_autoWarpEvents[i].right_offset,
                                           s_autoWarpEvents[i].forward_offset,
                                           s_autoWarpEvents[i].y_offset)
                : portWarpBondToPad(s_autoWarpEvents[i].pad)) {
            s_autoWarpEvents[i].applied = 1;
        }
    }
}

static void pcInitScriptedWarpChr(void)
{
    const char *script_env;

    if (s_autoWarpChrInitialized) {
        return;
    }

    s_autoWarpChrInitialized = 1;
    memset(s_autoWarpChrEvents, 0, sizeof(s_autoWarpChrEvents));

    script_env = getenv("GE007_AUTO_WARP_CHR_SCRIPT");

    if (script_env != NULL && *script_env != '\0' &&
        !(script_env[0] == '0' && script_env[1] == '\0')) {
        while (*script_env != '\0' && s_autoWarpChrCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            char *endptr;
            long frame;
            long chrnum;
            float distance = 96.0f;
            int has_angle = 0;
            float angle_deg = 0.0f;

            while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
                script_env++;
            }

            if (*script_env == '\0') {
                break;
            }

            frame = strtol(script_env, &endptr, 10);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            chrnum = strtol(script_env, &endptr, 10);
            if (endptr == script_env) {
                break;
            }

            script_env = endptr;

            if (*script_env == ':') {
                script_env++;
                distance = strtof(script_env, &endptr);
                if (endptr != script_env) {
                    script_env = endptr;
                }
            }

            if (*script_env == ':') {
                script_env++;
                angle_deg = strtof(script_env, &endptr);
                if (endptr != script_env) {
                    script_env = endptr;
                    has_angle = 1;
                }
            }

            s_autoWarpChrEvents[s_autoWarpChrCount].frame = (int)frame;
            s_autoWarpChrEvents[s_autoWarpChrCount].chrnum = (int)chrnum;
            s_autoWarpChrEvents[s_autoWarpChrCount].distance = distance;
            s_autoWarpChrEvents[s_autoWarpChrCount].has_angle = has_angle;
            s_autoWarpChrEvents[s_autoWarpChrCount].angle_deg = angle_deg;
            s_autoWarpChrEvents[s_autoWarpChrCount].applied = 0;
            s_autoWarpChrCount++;
        }
    }

    if (s_autoWarpChrCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_WARP_CHR_FRAME");
        const char *chr_env = getenv("GE007_AUTO_WARP_CHRNUM");
        const char *distance_env = getenv("GE007_AUTO_WARP_CHR_DISTANCE");
        const char *angle_env = getenv("GE007_AUTO_WARP_CHR_ANGLE");
        int frame = -1;
        int chrnum = -1;
        float distance = 96.0f;
        int has_angle = 0;
        float angle_deg = 0.0f;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 10);
        }

        if (chr_env != NULL && *chr_env != '\0') {
            chrnum = (int)strtol(chr_env, NULL, 10);
        }

        if (distance_env != NULL && *distance_env != '\0') {
            distance = strtof(distance_env, NULL);
        }

        if (angle_env != NULL && *angle_env != '\0') {
            angle_deg = strtof(angle_env, NULL);
            has_angle = 1;
        }

        if (frame >= 0 && chrnum >= 0) {
            s_autoWarpChrEvents[0].frame = frame;
            s_autoWarpChrEvents[0].chrnum = chrnum;
            s_autoWarpChrEvents[0].distance = distance;
            s_autoWarpChrEvents[0].has_angle = has_angle;
            s_autoWarpChrEvents[0].angle_deg = angle_deg;
            s_autoWarpChrEvents[0].applied = 0;
            s_autoWarpChrCount = 1;
        }
    }
}

static void pcMaybeApplyScriptedWarpChr(int input_frame)
{
    int i;

    pcInitScriptedWarpChr();

    if (!g_deterministic || s_autoWarpChrCount == 0) {
        return;
    }

    for (i = 0; i < s_autoWarpChrCount; i++) {
        if (s_autoWarpChrEvents[i].applied ||
            s_autoWarpChrEvents[i].frame < 0 ||
            s_autoWarpChrEvents[i].chrnum < 0 ||
            input_frame != s_autoWarpChrEvents[i].frame) {
            continue;
        }

        if (s_autoWarpChrEvents[i].has_angle
                ? portWarpBondNearChrAtAngle(s_autoWarpChrEvents[i].chrnum,
                                             s_autoWarpChrEvents[i].distance,
                                             s_autoWarpChrEvents[i].angle_deg)
                : portWarpBondNearChr(s_autoWarpChrEvents[i].chrnum,
                                      s_autoWarpChrEvents[i].distance)) {
            s_autoWarpChrEvents[i].applied = 1;
        }
    }
}

static int pcParseGuardSpawnMode(const char *text, char **endptr)
{
    long value;

    if (strncmp(text, "clone", 5) == 0) {
        *endptr = (char *)text + 5;
        return PC_GUARD_SPAWN_MODE_CLONE;
    }

    if (strncmp(text, "chr", 3) == 0) {
        *endptr = (char *)text + 3;
        return PC_GUARD_SPAWN_MODE_CHR;
    }

    value = strtol(text, endptr, 0);
    if (*endptr == text) {
        return 0;
    }

    if (value == PC_GUARD_SPAWN_MODE_CHR || value == PC_GUARD_SPAWN_MODE_CLONE) {
        return (int)value;
    }

    return 0;
}

static int pcParseGuardSpawnOptionalInt(const char **cursor, int *value)
{
    char *endptr;
    long parsed;

    if (**cursor != ':') {
        return 0;
    }

    (*cursor)++;
    parsed = strtol(*cursor, &endptr, 0);
    if (endptr == *cursor) {
        return -1;
    }

    *value = (int)parsed;
    *cursor = endptr;

    return 1;
}

static void pcInitScriptedGuardSpawn(void)
{
    const char *script_env;

    if (s_autoGuardSpawnInitialized) {
        return;
    }

    s_autoGuardSpawnInitialized = 1;
    memset(s_autoGuardSpawnEvents, 0, sizeof(s_autoGuardSpawnEvents));

    script_env = getenv("GE007_AUTO_GUARD_SPAWN_SCRIPT");

    if (script_env != NULL && *script_env != '\0' &&
        !(script_env[0] == '0' && script_env[1] == '\0')) {
        while (*script_env != '\0' && s_autoGuardSpawnCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            const char *cursor;
            char *endptr;
            long frame;
            int mode;
            long source_chrnum;
            long target_chrnum;
            int bodynum = -1;
            int headnum = -1;
            int ailist_id = -1;
            int flags = 0;
            int clear_seen_gate = 0;

            while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
                script_env++;
            }

            if (*script_env == '\0') {
                break;
            }

            frame = strtol(script_env, &endptr, 0);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            cursor = endptr + 1;
            mode = pcParseGuardSpawnMode(cursor, &endptr);
            if (mode == 0 || *endptr != ':') {
                break;
            }

            cursor = endptr + 1;
            source_chrnum = strtol(cursor, &endptr, 0);
            if (endptr == cursor || *endptr != ':') {
                break;
            }

            cursor = endptr + 1;
            target_chrnum = strtol(cursor, &endptr, 0);
            if (endptr == cursor) {
                break;
            }

            cursor = endptr;
            if (pcParseGuardSpawnOptionalInt(&cursor, &bodynum) < 0 ||
                pcParseGuardSpawnOptionalInt(&cursor, &headnum) < 0 ||
                pcParseGuardSpawnOptionalInt(&cursor, &ailist_id) < 0 ||
                pcParseGuardSpawnOptionalInt(&cursor, &flags) < 0 ||
                pcParseGuardSpawnOptionalInt(&cursor, &clear_seen_gate) < 0) {
                break;
            }

            s_autoGuardSpawnEvents[s_autoGuardSpawnCount].frame = (int)frame;
            s_autoGuardSpawnEvents[s_autoGuardSpawnCount].mode = mode;
            s_autoGuardSpawnEvents[s_autoGuardSpawnCount].source_chrnum = (int)source_chrnum;
            s_autoGuardSpawnEvents[s_autoGuardSpawnCount].target_chrnum = (int)target_chrnum;
            s_autoGuardSpawnEvents[s_autoGuardSpawnCount].bodynum = bodynum;
            s_autoGuardSpawnEvents[s_autoGuardSpawnCount].headnum = headnum;
            s_autoGuardSpawnEvents[s_autoGuardSpawnCount].ailist_id = ailist_id;
            s_autoGuardSpawnEvents[s_autoGuardSpawnCount].flags = flags;
            s_autoGuardSpawnEvents[s_autoGuardSpawnCount].clear_seen_gate = clear_seen_gate;
            s_autoGuardSpawnEvents[s_autoGuardSpawnCount].applied = 0;
            s_autoGuardSpawnCount++;

            script_env = cursor;
        }
    }

    if (s_autoGuardSpawnCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_GUARD_SPAWN_FRAME");
        const char *mode_env = getenv("GE007_AUTO_GUARD_SPAWN_MODE");
        const char *source_env = getenv("GE007_AUTO_GUARD_SPAWN_SOURCE_CHRNUM");
        const char *target_env = getenv("GE007_AUTO_GUARD_SPAWN_TARGET_CHRNUM");
        const char *body_env = getenv("GE007_AUTO_GUARD_SPAWN_BODY");
        const char *head_env = getenv("GE007_AUTO_GUARD_SPAWN_HEAD");
        const char *ai_env = getenv("GE007_AUTO_GUARD_SPAWN_AI_LIST");
        const char *flags_env = getenv("GE007_AUTO_GUARD_SPAWN_FLAGS");
        const char *clear_env = getenv("GE007_AUTO_GUARD_SPAWN_CLEAR_SEEN");
        int frame = -1;
        int mode = PC_GUARD_SPAWN_MODE_CHR;
        int source_chrnum = -1;
        int target_chrnum = -1;
        int bodynum = -1;
        int headnum = -1;
        int ailist_id = -1;
        int flags = 0;
        int clear_seen_gate = 0;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 0);
        }

        if (mode_env != NULL && *mode_env != '\0') {
            char *endptr;
            mode = pcParseGuardSpawnMode(mode_env, &endptr);
        }

        if (source_env != NULL && *source_env != '\0') {
            source_chrnum = (int)strtol(source_env, NULL, 0);
        }

        if (target_env != NULL && *target_env != '\0') {
            target_chrnum = (int)strtol(target_env, NULL, 0);
        }

        if (body_env != NULL && *body_env != '\0') {
            bodynum = (int)strtol(body_env, NULL, 0);
        }

        if (head_env != NULL && *head_env != '\0') {
            headnum = (int)strtol(head_env, NULL, 0);
        }

        if (ai_env != NULL && *ai_env != '\0') {
            ailist_id = (int)strtol(ai_env, NULL, 0);
        }

        if (flags_env != NULL && *flags_env != '\0') {
            flags = (int)strtol(flags_env, NULL, 0);
        }

        if (clear_env != NULL && *clear_env != '\0') {
            clear_seen_gate = (int)strtol(clear_env, NULL, 0) != 0;
        }

        if (frame >= 0 && mode != 0 && source_chrnum >= 0 && target_chrnum >= 0) {
            s_autoGuardSpawnEvents[0].frame = frame;
            s_autoGuardSpawnEvents[0].mode = mode;
            s_autoGuardSpawnEvents[0].source_chrnum = source_chrnum;
            s_autoGuardSpawnEvents[0].target_chrnum = target_chrnum;
            s_autoGuardSpawnEvents[0].bodynum = bodynum;
            s_autoGuardSpawnEvents[0].headnum = headnum;
            s_autoGuardSpawnEvents[0].ailist_id = ailist_id;
            s_autoGuardSpawnEvents[0].flags = flags;
            s_autoGuardSpawnEvents[0].clear_seen_gate = clear_seen_gate;
            s_autoGuardSpawnEvents[0].applied = 0;
            s_autoGuardSpawnCount = 1;
        }
    }
}

static void pcMaybeApplyScriptedGuardSpawn(int input_frame)
{
    int i;

    pcInitScriptedGuardSpawn();

    if (!g_deterministic || s_autoGuardSpawnCount == 0) {
        return;
    }

    for (i = 0; i < s_autoGuardSpawnCount; i++) {
        int applied;

        if (s_autoGuardSpawnEvents[i].applied ||
            s_autoGuardSpawnEvents[i].frame < 0 ||
            input_frame != s_autoGuardSpawnEvents[i].frame) {
            continue;
        }

        if (s_autoGuardSpawnEvents[i].mode == PC_GUARD_SPAWN_MODE_CLONE) {
            applied = portDeterministicTryCloneChr(
                s_autoGuardSpawnEvents[i].source_chrnum,
                s_autoGuardSpawnEvents[i].target_chrnum,
                s_autoGuardSpawnEvents[i].ailist_id,
                s_autoGuardSpawnEvents[i].clear_seen_gate);
        } else {
            applied = portDeterministicGuardSpawnNextToChr(
                s_autoGuardSpawnEvents[i].source_chrnum,
                s_autoGuardSpawnEvents[i].target_chrnum,
                s_autoGuardSpawnEvents[i].bodynum,
                s_autoGuardSpawnEvents[i].headnum,
                s_autoGuardSpawnEvents[i].ailist_id,
                s_autoGuardSpawnEvents[i].flags,
                s_autoGuardSpawnEvents[i].clear_seen_gate);
        }

        if (applied) {
            s_autoGuardSpawnEvents[i].applied = 1;
        }
    }
}

static void pcInitScriptedSetChrAI(void)
{
    const char *script_env;

    if (s_autoSetChrAIInitialized) {
        return;
    }

    s_autoSetChrAIInitialized = 1;
    memset(s_autoSetChrAIEvents, 0, sizeof(s_autoSetChrAIEvents));

    script_env = getenv("GE007_AUTO_SET_CHR_AI_SCRIPT");

    if (script_env != NULL && *script_env != '\0' &&
        !(script_env[0] == '0' && script_env[1] == '\0')) {
        while (*script_env != '\0' && s_autoSetChrAICount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            char *endptr;
            long frame;
            long chrnum;
            long ailist_id;

            while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
                script_env++;
            }

            if (*script_env == '\0') {
                break;
            }

            frame = strtol(script_env, &endptr, 0);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            chrnum = strtol(script_env, &endptr, 0);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            ailist_id = strtol(script_env, &endptr, 0);
            if (endptr == script_env) {
                break;
            }

            s_autoSetChrAIEvents[s_autoSetChrAICount].frame = (int)frame;
            s_autoSetChrAIEvents[s_autoSetChrAICount].chrnum = (int)chrnum;
            s_autoSetChrAIEvents[s_autoSetChrAICount].ailist_id = (int)ailist_id;
            s_autoSetChrAIEvents[s_autoSetChrAICount].applied = 0;
            s_autoSetChrAICount++;
            script_env = endptr;
        }
    }

    if (s_autoSetChrAICount == 0) {
        const char *frame_env = getenv("GE007_AUTO_SET_CHR_AI_FRAME");
        const char *chr_env = getenv("GE007_AUTO_SET_CHR_AI_CHRNUM");
        const char *ai_env = getenv("GE007_AUTO_SET_CHR_AI_LIST");
        int frame = -1;
        int chrnum = -1;
        int ailist_id = -1;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 0);
        }

        if (chr_env != NULL && *chr_env != '\0') {
            chrnum = (int)strtol(chr_env, NULL, 0);
        }

        if (ai_env != NULL && *ai_env != '\0') {
            ailist_id = (int)strtol(ai_env, NULL, 0);
        }

        if (frame >= 0 && chrnum >= 0 && ailist_id >= 0) {
            s_autoSetChrAIEvents[0].frame = frame;
            s_autoSetChrAIEvents[0].chrnum = chrnum;
            s_autoSetChrAIEvents[0].ailist_id = ailist_id;
            s_autoSetChrAIEvents[0].applied = 0;
            s_autoSetChrAICount = 1;
        }
    }
}

static void pcMaybeApplyScriptedSetChrAI(int input_frame)
{
    int i;

    pcInitScriptedSetChrAI();

    if (!g_deterministic || s_autoSetChrAICount == 0) {
        return;
    }

    for (i = 0; i < s_autoSetChrAICount; i++) {
        ChrRecord *chr;
        AIRecord *ailist;

        if (s_autoSetChrAIEvents[i].applied ||
            s_autoSetChrAIEvents[i].frame < 0 ||
            s_autoSetChrAIEvents[i].chrnum < 0 ||
            s_autoSetChrAIEvents[i].ailist_id < 0 ||
            input_frame != s_autoSetChrAIEvents[i].frame) {
            continue;
        }

        chr = chrFindByLiteralId(s_autoSetChrAIEvents[i].chrnum);
        ailist = ailistFindById(s_autoSetChrAIEvents[i].ailist_id);

        if (chr == NULL || ailist == NULL) {
            continue;
        }

        chr->ailist = ailist;
        chr->aioffset = 0;
        chr->sleep = 0;
        s_autoSetChrAIEvents[i].applied = 1;
    }
}

static void pcInitScriptedSetChrFlag(void)
{
    const char *script_env;

    if (s_autoSetChrFlagInitialized) {
        return;
    }

    s_autoSetChrFlagInitialized = 1;
    memset(s_autoSetChrFlagEvents, 0, sizeof(s_autoSetChrFlagEvents));

    script_env = getenv("GE007_AUTO_SET_CHRFLAG_SCRIPT");

    if (script_env != NULL && *script_env != '\0' &&
        !(script_env[0] == '0' && script_env[1] == '\0')) {
        while (*script_env != '\0' && s_autoSetChrFlagCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            char *endptr;
            long frame;
            long chrnum;
            unsigned long set_mask;
            unsigned long clr_mask;

            while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
                script_env++;
            }

            if (*script_env == '\0') {
                break;
            }

            frame = strtol(script_env, &endptr, 0);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            chrnum = strtol(script_env, &endptr, 0);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            set_mask = strtoul(script_env, &endptr, 0);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            clr_mask = strtoul(script_env, &endptr, 0);
            if (endptr == script_env) {
                break;
            }

            s_autoSetChrFlagEvents[s_autoSetChrFlagCount].frame = (int)frame;
            s_autoSetChrFlagEvents[s_autoSetChrFlagCount].chrnum = (int)chrnum;
            s_autoSetChrFlagEvents[s_autoSetChrFlagCount].set_mask = (unsigned int)set_mask;
            s_autoSetChrFlagEvents[s_autoSetChrFlagCount].clr_mask = (unsigned int)clr_mask;
            s_autoSetChrFlagEvents[s_autoSetChrFlagCount].applied = 0;
            s_autoSetChrFlagCount++;
            script_env = endptr;
        }
    }

    if (s_autoSetChrFlagCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_SET_CHRFLAG_FRAME");
        const char *chr_env = getenv("GE007_AUTO_SET_CHRFLAG_CHRNUM");
        const char *set_env = getenv("GE007_AUTO_SET_CHRFLAG_SET");
        const char *clr_env = getenv("GE007_AUTO_SET_CHRFLAG_CLR");
        int frame = -1;
        int chrnum = -1;
        unsigned int set_mask = 0;
        unsigned int clr_mask = 0;
        int have_mask = 0;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 0);
        }

        if (chr_env != NULL && *chr_env != '\0') {
            chrnum = (int)strtol(chr_env, NULL, 0);
        }

        if (set_env != NULL && *set_env != '\0') {
            set_mask = (unsigned int)strtoul(set_env, NULL, 0);
            have_mask = 1;
        }

        if (clr_env != NULL && *clr_env != '\0') {
            clr_mask = (unsigned int)strtoul(clr_env, NULL, 0);
            have_mask = 1;
        }

        if (frame >= 0 && chrnum >= 0 && have_mask) {
            s_autoSetChrFlagEvents[0].frame = frame;
            s_autoSetChrFlagEvents[0].chrnum = chrnum;
            s_autoSetChrFlagEvents[0].set_mask = set_mask;
            s_autoSetChrFlagEvents[0].clr_mask = clr_mask;
            s_autoSetChrFlagEvents[0].applied = 0;
            s_autoSetChrFlagCount = 1;
        }
    }
}

static void pcMaybeApplyScriptedSetChrFlag(int input_frame)
{
    int i;

    pcInitScriptedSetChrFlag();

    if (!g_deterministic || s_autoSetChrFlagCount == 0) {
        return;
    }

    for (i = 0; i < s_autoSetChrFlagCount; i++) {
        ChrRecord *chr;
        unsigned int flags;

        if (s_autoSetChrFlagEvents[i].applied ||
            s_autoSetChrFlagEvents[i].frame < 0 ||
            s_autoSetChrFlagEvents[i].chrnum < 0 ||
            input_frame != s_autoSetChrFlagEvents[i].frame) {
            continue;
        }

        chr = chrFindByLiteralId(s_autoSetChrFlagEvents[i].chrnum);

        if (chr == NULL) {
            continue;
        }

        /* Compute in unsigned then cast once back to the CHRFLAG enum so the
         * set/clear is -Werror-clean (no implicit int->enum on the bitops). */
        flags = (unsigned int)chr->chrflags;
        flags |= s_autoSetChrFlagEvents[i].set_mask;
        flags &= ~s_autoSetChrFlagEvents[i].clr_mask;
        chr->chrflags = (CHRFLAG)flags;
        s_autoSetChrFlagEvents[i].applied = 1;
    }
}

static PadRecord *pcResolveScriptedPad(int padnum)
{
    if (padnum < 0 || g_CurrentSetup.pads == NULL) {
        return NULL;
    }

    if (isNotBoundPad(padnum)) {
        return &g_CurrentSetup.pads[padnum];
    }

    if (g_CurrentSetup.boundpads == NULL) {
        return NULL;
    }

    return (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(padnum)];
}

static void pcInitScriptedChrToPad(void)
{
    const char *script_env;

    if (s_autoChrToPadInitialized) {
        return;
    }

    s_autoChrToPadInitialized = 1;
    memset(s_autoChrToPadEvents, 0, sizeof(s_autoChrToPadEvents));

    script_env = getenv("GE007_AUTO_CHR_TO_PAD_SCRIPT");

    if (script_env != NULL && *script_env != '\0' &&
        !(script_env[0] == '0' && script_env[1] == '\0')) {
        while (*script_env != '\0' && s_autoChrToPadCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            char *endptr;
            long frame;
            long chrnum;
            long pad;
            long stop = 0;

            while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
                script_env++;
            }

            if (*script_env == '\0') {
                break;
            }

            frame = strtol(script_env, &endptr, 0);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            chrnum = strtol(script_env, &endptr, 0);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            pad = strtol(script_env, &endptr, 0);
            if (endptr == script_env) {
                break;
            }

            script_env = endptr;

            if (*script_env == ':') {
                script_env++;
                stop = strtol(script_env, &endptr, 0);
                if (endptr != script_env) {
                    script_env = endptr;
                }
            }

            s_autoChrToPadEvents[s_autoChrToPadCount].frame = (int)frame;
            s_autoChrToPadEvents[s_autoChrToPadCount].chrnum = (int)chrnum;
            s_autoChrToPadEvents[s_autoChrToPadCount].pad = (int)pad;
            s_autoChrToPadEvents[s_autoChrToPadCount].stop = stop != 0;
            s_autoChrToPadEvents[s_autoChrToPadCount].applied = 0;
            s_autoChrToPadCount++;
        }
    }

    if (s_autoChrToPadCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_CHR_TO_PAD_FRAME");
        const char *chr_env = getenv("GE007_AUTO_CHR_TO_PAD_CHRNUM");
        const char *pad_env = getenv("GE007_AUTO_CHR_TO_PAD_PAD");
        const char *stop_env = getenv("GE007_AUTO_CHR_TO_PAD_STOP");
        int frame = -1;
        int chrnum = -1;
        int pad = -1;
        int stop = 0;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 0);
        }

        if (chr_env != NULL && *chr_env != '\0') {
            chrnum = (int)strtol(chr_env, NULL, 0);
        }

        if (pad_env != NULL && *pad_env != '\0') {
            pad = (int)strtol(pad_env, NULL, 0);
        }

        if (stop_env != NULL && *stop_env != '\0') {
            stop = (int)strtol(stop_env, NULL, 0);
        }

        if (frame >= 0 && chrnum >= 0 && pad >= 0) {
            s_autoChrToPadEvents[0].frame = frame;
            s_autoChrToPadEvents[0].chrnum = chrnum;
            s_autoChrToPadEvents[0].pad = pad;
            s_autoChrToPadEvents[0].stop = stop != 0;
            s_autoChrToPadEvents[0].applied = 0;
            s_autoChrToPadCount = 1;
        }
    }
}

static void pcMaybeApplyScriptedChrToPad(int input_frame)
{
    int i;

    pcInitScriptedChrToPad();

    if (!g_deterministic || s_autoChrToPadCount == 0) {
        return;
    }

    for (i = 0; i < s_autoChrToPadCount; i++) {
        ChrRecord *chr;
        PadRecord *pad;
        coord3d pos;
        StandTile *stan;
        f32 facing;

        if (s_autoChrToPadEvents[i].applied ||
            s_autoChrToPadEvents[i].frame < 0 ||
            s_autoChrToPadEvents[i].chrnum < 0 ||
            s_autoChrToPadEvents[i].pad < 0 ||
            input_frame != s_autoChrToPadEvents[i].frame) {
            continue;
        }

        chr = chrFindByLiteralId(s_autoChrToPadEvents[i].chrnum);
        pad = pcResolveScriptedPad(s_autoChrToPadEvents[i].pad);

        if (chr == NULL || chr->prop == NULL || chr->model == NULL ||
            pad == NULL || pad->stan == NULL) {
            continue;
        }

        facing = atan2f(pad->look.x, pad->look.z);
        pos.x = pad->pos.x;
        pos.y = pad->pos.y;
        pos.z = pad->pos.z;
        stan = pad->stan;

        sub_GAME_7F03D058(chr->prop, FALSE);

        if (chrAdjustPosForSpawn(&pos, &stan, facing, TRUE)) {
            chr->prop->pos.x = pos.x;
            chr->prop->pos.y = pos.y;
            chr->prop->pos.z = pos.z;
            chr->prop->stan = stan;
            chr->chrflags |= CHRFLAG_INIT;
            setsubroty(chr->model, facing);
            setsuboffset(chr->model, &pos);
            chrPositionRelated7F020D94(chr);
            chr->sleep = 0;

            if (s_autoChrToPadEvents[i].stop) {
                check_set_actor_standing_still(chr, 0x0008, s_autoChrToPadEvents[i].pad);
            }

            s_autoChrToPadEvents[i].applied = 1;
        }

        sub_GAME_7F03D058(chr->prop, TRUE);
    }
}

static void pcParseScriptedForceChrEvents(const char *script_env)
{
    if (script_env == NULL || *script_env == '\0' ||
        (script_env[0] == '0' && script_env[1] == '\0')) {
        return;
    }

    while (*script_env != '\0' && s_autoForceChrCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
        char *endptr;
        long frame;
        long frame_end;
        long chrnum;
        float x;
        float y;
        float z;
        float yaw_deg;
        int pad = -1;
        int stop = 0;

        while (*script_env == ' ' || *script_env == '\t' ||
               *script_env == ',' || *script_env == ';') {
            script_env++;
        }

        if (*script_env == '\0') {
            break;
        }

        frame = strtol(script_env, &endptr, 10);
        if (endptr == script_env) {
            break;
        }

        frame_end = frame;
        script_env = endptr;

        if (*script_env == '-') {
            script_env++;
            frame_end = strtol(script_env, &endptr, 10);
            if (endptr == script_env) {
                break;
            }
            script_env = endptr;
        }

        if (*script_env != ':') {
            break;
        }

        script_env++;
        chrnum = strtol(script_env, &endptr, 10);
        if (endptr == script_env || *endptr != ':') {
            break;
        }

        script_env = endptr + 1;
        x = strtof(script_env, &endptr);
        if (endptr == script_env || *endptr != ':') {
            break;
        }

        script_env = endptr + 1;
        y = strtof(script_env, &endptr);
        if (endptr == script_env || *endptr != ':') {
            break;
        }

        script_env = endptr + 1;
        z = strtof(script_env, &endptr);
        if (endptr == script_env || *endptr != ':') {
            break;
        }

        script_env = endptr + 1;
        yaw_deg = strtof(script_env, &endptr);
        if (endptr == script_env) {
            break;
        }

        script_env = endptr;

        if (*script_env == ':') {
            long pad_value;

            script_env++;
            pad_value = strtol(script_env, &endptr, 10);
            if (endptr == script_env) {
                break;
            }
            pad = (int)pad_value;
            script_env = endptr;
        }

        if (*script_env == ':') {
            long stop_value;

            script_env++;
            stop_value = strtol(script_env, &endptr, 10);
            if (endptr == script_env) {
                break;
            }
            stop = stop_value != 0;
            script_env = endptr;
        }

        if (frame_end < frame) {
            long temp = frame;
            frame = frame_end;
            frame_end = temp;
        }

        while (frame <= frame_end && s_autoForceChrCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            s_autoForceChrEvents[s_autoForceChrCount].frame = (int)frame;
            s_autoForceChrEvents[s_autoForceChrCount].chrnum = (int)chrnum;
            s_autoForceChrEvents[s_autoForceChrCount].x = x;
            s_autoForceChrEvents[s_autoForceChrCount].y = y;
            s_autoForceChrEvents[s_autoForceChrCount].z = z;
            s_autoForceChrEvents[s_autoForceChrCount].yaw_deg = yaw_deg;
            s_autoForceChrEvents[s_autoForceChrCount].pad = pad;
            s_autoForceChrEvents[s_autoForceChrCount].stop = stop;
            s_autoForceChrEvents[s_autoForceChrCount].applied = 0;
            s_autoForceChrCount++;
            frame++;
        }
    }
}

static void pcInitScriptedForceChr(void)
{
    if (s_autoForceChrInitialized) {
        return;
    }

    s_autoForceChrInitialized = 1;
    memset(s_autoForceChrEvents, 0, sizeof(s_autoForceChrEvents));
    s_autoForceChrCount = 0;

    pcParseScriptedForceChrEvents(getenv("GE007_AUTO_FORCE_CHR_SCRIPT"));
    pcParseScriptedForceChrEvents(getenv("GE007_AUTO_FORCE_CHR_SCRIPT_EXTRA"));
}

static void pcMaybeApplyScriptedForceChr(int input_frame)
{
    int i;

    pcInitScriptedForceChr();

    if (!g_deterministic || s_autoForceChrCount == 0) {
        return;
    }

    for (i = 0; i < s_autoForceChrCount; i++) {
        PcScriptedForceChrEvent *event = &s_autoForceChrEvents[i];
        ChrRecord *chr;
        PadRecord *pad = NULL;
        StandTile *stan = NULL;
        coord3d pos;
        f32 yaw_rad;

        if (event->applied ||
            event->frame < 0 ||
            event->chrnum < 0 ||
            input_frame != event->frame) {
            continue;
        }

        chr = chrFindByLiteralId(event->chrnum);
        if (chr == NULL || chr->prop == NULL || chr->model == NULL) {
            continue;
        }

        if (event->pad >= 0) {
            pad = pcResolveScriptedPad(event->pad);
        }
        if (pad != NULL) {
            stan = pad->stan;
        }
        if (stan == NULL) {
            stan = chr->prop->stan;
        }
        if (stan == NULL) {
            continue;
        }

        pos.x = event->x;
        pos.y = event->y;
        pos.z = event->z;
        yaw_rad = pcWrapYawDegrees(event->yaw_deg) * (M_TAU_F / 360.0f);

        sub_GAME_7F03D058(chr->prop, FALSE);

        chr->prop->pos.x = pos.x;
        chr->prop->pos.y = pos.y;
        chr->prop->pos.z = pos.z;
        chr->prop->stan = stan;
        chr->chrflags |= CHRFLAG_INIT;
        setsubroty(chr->model, yaw_rad);
        setsuboffset(chr->model, &pos);
        chrPositionRelated7F020D94(chr);
        chr->sleep = 0;

        if (event->stop) {
            check_set_actor_standing_still(chr, 0x0008, event->pad);
        }

        event->applied = 1;

        sub_GAME_7F03D058(chr->prop, TRUE);

        if (getenv("GE007_VERBOSE")) {
            fprintf(stderr,
                    "[AUTO_FORCE_CHR] frame=%d chr=%d pos=(%.2f,%.2f,%.2f) yaw=%.2f pad=%d room=%d stop=%d\n",
                    input_frame,
                    event->chrnum,
                    pos.x,
                    pos.y,
                    pos.z,
                    pcWrapYawDegrees(event->yaw_deg),
                    event->pad,
                    stan->room,
                    event->stop);
            fflush(stderr);
        }
    }
}

static void pcInitScriptedDamageChr(void)
{
    const char *script_env;

    if (s_autoDamageChrInitialized) {
        return;
    }

    s_autoDamageChrInitialized = 1;
    memset(s_autoDamageChrEvents, 0, sizeof(s_autoDamageChrEvents));

    script_env = getenv("GE007_AUTO_DAMAGE_CHR_SCRIPT");

    if (script_env != NULL && *script_env != '\0' &&
        !(script_env[0] == '0' && script_env[1] == '\0')) {
        while (*script_env != '\0' && s_autoDamageChrCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            char *endptr;
            long frame;
            long chrnum;
            float amount = 3.0f;

            while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
                script_env++;
            }

            if (*script_env == '\0') {
                break;
            }

            frame = strtol(script_env, &endptr, 10);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            chrnum = strtol(script_env, &endptr, 10);
            if (endptr == script_env) {
                break;
            }

            script_env = endptr;

            if (*script_env == ':') {
                script_env++;
                amount = strtof(script_env, &endptr);
                if (endptr != script_env) {
                    script_env = endptr;
                }
            }

            s_autoDamageChrEvents[s_autoDamageChrCount].frame = (int)frame;
            s_autoDamageChrEvents[s_autoDamageChrCount].chrnum = (int)chrnum;
            s_autoDamageChrEvents[s_autoDamageChrCount].amount = amount;
            s_autoDamageChrEvents[s_autoDamageChrCount].applied = 0;
            s_autoDamageChrCount++;
        }
    }

    if (s_autoDamageChrCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_DAMAGE_CHR_FRAME");
        const char *chr_env = getenv("GE007_AUTO_DAMAGE_CHRNUM");
        const char *amount_env = getenv("GE007_AUTO_DAMAGE_CHR_AMOUNT");
        int frame = -1;
        int chrnum = -1;
        float amount = 3.0f;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 10);
        }

        if (chr_env != NULL && *chr_env != '\0') {
            chrnum = (int)strtol(chr_env, NULL, 10);
        }

        if (amount_env != NULL && *amount_env != '\0') {
            amount = strtof(amount_env, NULL);
        }

        if (frame >= 0 && chrnum >= 0) {
            s_autoDamageChrEvents[0].frame = frame;
            s_autoDamageChrEvents[0].chrnum = chrnum;
            s_autoDamageChrEvents[0].amount = amount;
            s_autoDamageChrEvents[0].applied = 0;
            s_autoDamageChrCount = 1;
        }
    }
}

static void pcMaybeApplyScriptedDamageChr(int input_frame)
{
    int i;

    pcInitScriptedDamageChr();

    if (!g_deterministic || s_autoDamageChrCount == 0) {
        return;
    }

    for (i = 0; i < s_autoDamageChrCount; i++) {
        ChrRecord *chr;
        float amount;

        if (s_autoDamageChrEvents[i].applied ||
            s_autoDamageChrEvents[i].frame < 0 ||
            s_autoDamageChrEvents[i].chrnum < 0 ||
            input_frame != s_autoDamageChrEvents[i].frame) {
            continue;
        }

        chr = chrFindByLiteralId(s_autoDamageChrEvents[i].chrnum);

        if (chr == NULL || chr->prop == NULL) {
            continue;
        }

        amount = s_autoDamageChrEvents[i].amount;
        if (amount <= 0.0f) {
            amount = 3.0f;
        }

        if (chrlvExplosionDamage(chr, &chr->prop->pos, amount, 0) != 0) {
            s_autoDamageChrEvents[i].applied = 1;
        }
    }
}

static void pcParseScriptedObjectTagEvents(const char *script_env,
                                           PcScriptedObjectTagEvent *events,
                                           int *count,
                                           float default_amount)
{
    if (script_env == NULL || *script_env == '\0' ||
        (script_env[0] == '0' && script_env[1] == '\0')) {
        return;
    }

    while (*script_env != '\0' && *count < PC_MAX_SCRIPTED_WARP_EVENTS) {
        char *endptr;
        long frame;
        long tag;
        float amount = default_amount;

        while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
            script_env++;
        }

        if (*script_env == '\0') {
            break;
        }

        frame = strtol(script_env, &endptr, 0);
        if (endptr == script_env || *endptr != ':') {
            break;
        }

        script_env = endptr + 1;
        tag = strtol(script_env, &endptr, 0);
        if (endptr == script_env) {
            break;
        }

        script_env = endptr;

        if (*script_env == ':') {
            script_env++;
            amount = strtof(script_env, &endptr);
            if (endptr != script_env) {
                script_env = endptr;
            }
        }

        events[*count].frame = (int)frame;
        events[*count].tag = (int)tag;
        events[*count].amount = amount;
        events[*count].applied = 0;
        (*count)++;
    }
}

static void pcInitScriptedCollectTag(void)
{
    if (s_autoCollectTagInitialized) {
        return;
    }

    s_autoCollectTagInitialized = 1;
    memset(s_autoCollectTagEvents, 0, sizeof(s_autoCollectTagEvents));

    pcParseScriptedObjectTagEvents(getenv("GE007_AUTO_COLLECT_TAG_SCRIPT"),
                                   s_autoCollectTagEvents,
                                   &s_autoCollectTagCount,
                                   0.0f);

    if (s_autoCollectTagCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_COLLECT_TAG_FRAME");
        const char *tag_env = getenv("GE007_AUTO_COLLECT_TAG");
        int frame = -1;
        int tag = -1;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 0);
        }

        if (tag_env != NULL && *tag_env != '\0') {
            tag = (int)strtol(tag_env, NULL, 0);
        }

        if (frame >= 0 && tag >= 0) {
            s_autoCollectTagEvents[0].frame = frame;
            s_autoCollectTagEvents[0].tag = tag;
            s_autoCollectTagEvents[0].amount = 0.0f;
            s_autoCollectTagEvents[0].applied = 0;
            s_autoCollectTagCount = 1;
        }
    }
}

static void pcMaybeApplyScriptedCollectTag(int input_frame)
{
    int i;

    pcInitScriptedCollectTag();

    if (!g_deterministic || s_autoCollectTagCount == 0) {
        return;
    }

    for (i = 0; i < s_autoCollectTagCount; i++) {
        ObjectRecord *obj;
        INV_ITEM_TYPE collect_type;

        if (s_autoCollectTagEvents[i].applied ||
            s_autoCollectTagEvents[i].frame < 0 ||
            s_autoCollectTagEvents[i].tag < 0 ||
            input_frame != s_autoCollectTagEvents[i].frame) {
            continue;
        }

        obj = objFindByTagId(s_autoCollectTagEvents[i].tag);
        if (obj == NULL || obj->prop == NULL) {
            continue;
        }

        if (bondinvHasPropInInv(obj->prop)) {
            s_autoCollectTagEvents[i].applied = 1;
            continue;
        }

        collect_type = collect_or_interact_object(obj->prop, FALSE);
        propExecuteTickOperation(obj->prop, collect_type);

        if (collect_type != INV_ITEM_NONE || bondinvHasPropInInv(obj->prop)) {
            s_autoCollectTagEvents[i].applied = 1;
        }
    }
}

static void pcInitScriptedDamageTag(void)
{
    if (s_autoDamageTagInitialized) {
        return;
    }

    s_autoDamageTagInitialized = 1;
    memset(s_autoDamageTagEvents, 0, sizeof(s_autoDamageTagEvents));

    pcParseScriptedObjectTagEvents(getenv("GE007_AUTO_DAMAGE_TAG_SCRIPT"),
                                   s_autoDamageTagEvents,
                                   &s_autoDamageTagCount,
                                   5.0f);

    if (s_autoDamageTagCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_DAMAGE_TAG_FRAME");
        const char *tag_env = getenv("GE007_AUTO_DAMAGE_TAG");
        const char *amount_env = getenv("GE007_AUTO_DAMAGE_TAG_AMOUNT");
        int frame = -1;
        int tag = -1;
        float amount = 5.0f;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 0);
        }

        if (tag_env != NULL && *tag_env != '\0') {
            tag = (int)strtol(tag_env, NULL, 0);
        }

        if (amount_env != NULL && *amount_env != '\0') {
            amount = strtof(amount_env, NULL);
        }

        if (frame >= 0 && tag >= 0) {
            s_autoDamageTagEvents[0].frame = frame;
            s_autoDamageTagEvents[0].tag = tag;
            s_autoDamageTagEvents[0].amount = amount;
            s_autoDamageTagEvents[0].applied = 0;
            s_autoDamageTagCount = 1;
        }
    }
}

static void pcMaybeApplyScriptedDamageTag(int input_frame)
{
    int i;

    pcInitScriptedDamageTag();

    if (!g_deterministic || s_autoDamageTagCount == 0) {
        return;
    }

    for (i = 0; i < s_autoDamageTagCount; i++) {
        ObjectRecord *obj;
        coord3d pos;
        float amount;

        if (s_autoDamageTagEvents[i].applied ||
            s_autoDamageTagEvents[i].frame < 0 ||
            s_autoDamageTagEvents[i].tag < 0 ||
            input_frame != s_autoDamageTagEvents[i].frame) {
            continue;
        }

        obj = objFindByTagId(s_autoDamageTagEvents[i].tag);
        if (obj == NULL || obj->prop == NULL) {
            continue;
        }

        if (objGetDestroyedLevel(obj) != 0) {
            s_autoDamageTagEvents[i].applied = 1;
            continue;
        }

        amount = s_autoDamageTagEvents[i].amount;
        if (amount <= 0.0f) {
            amount = 5.0f;
        }

        pos = obj->runtime_pos;
        maybe_detonate_object(obj, amount, &pos, ITEM_AK47, 0);

        if (objGetDestroyedLevel(obj) != 0) {
            s_autoDamageTagEvents[i].applied = 1;
        }
    }
}

static void pcParseScriptedStageFlagEvents(const char *script_env,
                                           PcScriptedStageFlagEvent *events,
                                           int *count)
{
    if (script_env == NULL || *script_env == '\0' ||
        (script_env[0] == '0' && script_env[1] == '\0')) {
        return;
    }

    while (*script_env != '\0' && *count < PC_MAX_SCRIPTED_WARP_EVENTS) {
        char *endptr;
        long frame;
        unsigned long flags;

        while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
            script_env++;
        }

        if (*script_env == '\0') {
            break;
        }

        frame = strtol(script_env, &endptr, 0);
        if (endptr == script_env || *endptr != ':') {
            break;
        }

        script_env = endptr + 1;
        flags = strtoul(script_env, &endptr, 0);
        if (endptr == script_env) {
            break;
        }

        script_env = endptr;

        events[*count].frame = (int)frame;
        events[*count].flags = (u32)flags;
        events[*count].applied = 0;
        (*count)++;
    }
}

static void pcInitScriptedSetStageFlags(void)
{
    if (s_autoSetStageFlagsInitialized) {
        return;
    }

    s_autoSetStageFlagsInitialized = 1;
    memset(s_autoSetStageFlagsEvents, 0, sizeof(s_autoSetStageFlagsEvents));
    pcParseScriptedStageFlagEvents(getenv("GE007_AUTO_SET_STAGE_FLAGS_SCRIPT"),
                                   s_autoSetStageFlagsEvents,
                                   &s_autoSetStageFlagsCount);
}

static void pcInitScriptedUnsetStageFlags(void)
{
    if (s_autoUnsetStageFlagsInitialized) {
        return;
    }

    s_autoUnsetStageFlagsInitialized = 1;
    memset(s_autoUnsetStageFlagsEvents, 0, sizeof(s_autoUnsetStageFlagsEvents));
    pcParseScriptedStageFlagEvents(getenv("GE007_AUTO_UNSET_STAGE_FLAGS_SCRIPT"),
                                   s_autoUnsetStageFlagsEvents,
                                   &s_autoUnsetStageFlagsCount);
}

static void pcMaybeApplyScriptedSetStageFlags(int input_frame)
{
    int i;

    pcInitScriptedSetStageFlags();

    if (!g_deterministic || s_autoSetStageFlagsCount == 0) {
        return;
    }

    for (i = 0; i < s_autoSetStageFlagsCount; i++) {
        if (s_autoSetStageFlagsEvents[i].applied ||
            s_autoSetStageFlagsEvents[i].frame < 0 ||
            s_autoSetStageFlagsEvents[i].flags == 0 ||
            input_frame != s_autoSetStageFlagsEvents[i].frame) {
            continue;
        }

        chrSetStageFlags(NULL, s_autoSetStageFlagsEvents[i].flags);
        s_autoSetStageFlagsEvents[i].applied = 1;
    }
}

static void pcMaybeApplyScriptedUnsetStageFlags(int input_frame)
{
    int i;

    pcInitScriptedUnsetStageFlags();

    if (!g_deterministic || s_autoUnsetStageFlagsCount == 0) {
        return;
    }

    for (i = 0; i < s_autoUnsetStageFlagsCount; i++) {
        if (s_autoUnsetStageFlagsEvents[i].applied ||
            s_autoUnsetStageFlagsEvents[i].frame < 0 ||
            s_autoUnsetStageFlagsEvents[i].flags == 0 ||
            input_frame != s_autoUnsetStageFlagsEvents[i].frame) {
            continue;
        }

        chrUnsetStageFlags(NULL, s_autoUnsetStageFlagsEvents[i].flags);
        s_autoUnsetStageFlagsEvents[i].applied = 1;
    }
}

static void pcInitScriptedAttackRollChr(void)
{
    const char *script_env;

    if (s_autoAttackRollChrInitialized) {
        return;
    }

    s_autoAttackRollChrInitialized = 1;
    memset(s_autoAttackRollChrEvents, 0, sizeof(s_autoAttackRollChrEvents));

    script_env = getenv("GE007_AUTO_ATTACKROLL_CHR_SCRIPT");

    if (script_env != NULL && *script_env != '\0' &&
        !(script_env[0] == '0' && script_env[1] == '\0')) {
        while (*script_env != '\0' && s_autoAttackRollChrCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            char *endptr;
            long frame;
            long chrnum;
            long side = GUNRIGHT;

            while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
                script_env++;
            }

            if (*script_env == '\0') {
                break;
            }

            frame = strtol(script_env, &endptr, 10);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            chrnum = strtol(script_env, &endptr, 10);
            if (endptr == script_env) {
                break;
            }

            script_env = endptr;

            if (*script_env == ':') {
                script_env++;
                if (*script_env == 'L' || *script_env == 'l') {
                    side = GUNLEFT;
                    script_env++;
                } else if (*script_env == 'R' || *script_env == 'r') {
                    side = GUNRIGHT;
                    script_env++;
                } else {
                    side = strtol(script_env, &endptr, 10);
                    if (endptr != script_env) {
                        script_env = endptr;
                    }
                }
            }

            s_autoAttackRollChrEvents[s_autoAttackRollChrCount].frame = (int)frame;
            s_autoAttackRollChrEvents[s_autoAttackRollChrCount].chrnum = (int)chrnum;
            s_autoAttackRollChrEvents[s_autoAttackRollChrCount].side = (int)side;
            s_autoAttackRollChrEvents[s_autoAttackRollChrCount].applied = 0;
            s_autoAttackRollChrCount++;
        }
    }

    if (s_autoAttackRollChrCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_ATTACKROLL_CHR_FRAME");
        const char *chr_env = getenv("GE007_AUTO_ATTACKROLL_CHRNUM");
        const char *side_env = getenv("GE007_AUTO_ATTACKROLL_CHR_SIDE");
        int frame = -1;
        int chrnum = -1;
        int side = GUNRIGHT;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 10);
        }

        if (chr_env != NULL && *chr_env != '\0') {
            chrnum = (int)strtol(chr_env, NULL, 10);
        }

        if (side_env != NULL && *side_env != '\0') {
            if (*side_env == 'L' || *side_env == 'l') {
                side = GUNLEFT;
            } else if (*side_env == 'R' || *side_env == 'r') {
                side = GUNRIGHT;
            } else {
                side = (int)strtol(side_env, NULL, 10);
            }
        }

        if (frame >= 0 && chrnum >= 0) {
            s_autoAttackRollChrEvents[0].frame = frame;
            s_autoAttackRollChrEvents[0].chrnum = chrnum;
            s_autoAttackRollChrEvents[0].side = side;
            s_autoAttackRollChrEvents[0].applied = 0;
            s_autoAttackRollChrCount = 1;
        }
    }
}

static void pcMaybeApplyScriptedAttackRollChr(int input_frame)
{
    int i;

    pcInitScriptedAttackRollChr();

    if (!g_deterministic || s_autoAttackRollChrCount == 0) {
        return;
    }

    for (i = 0; i < s_autoAttackRollChrCount; i++) {
        ChrRecord *chr;
        int side;

        if (s_autoAttackRollChrEvents[i].applied ||
            s_autoAttackRollChrEvents[i].frame < 0 ||
            s_autoAttackRollChrEvents[i].chrnum < 0 ||
            input_frame != s_autoAttackRollChrEvents[i].frame) {
            continue;
        }

        chr = chrFindByLiteralId(s_autoAttackRollChrEvents[i].chrnum);
        side = s_autoAttackRollChrEvents[i].side;

        if (chr == NULL || chr->prop == NULL || chr->model == NULL) {
            continue;
        }

        if (side != GUNLEFT) {
            side = GUNRIGHT;
        }

        chrlvInitActAttackRoll(chr, side);
        s_autoAttackRollChrEvents[i].applied = 1;
    }
}

static int pcParseScriptedAIMoveMode(const char *value, char **endptr)
{
    if (value == NULL) {
        if (endptr != NULL) {
            *endptr = (char *)value;
        }
        return 0;
    }

    if (strncmp(value, "runfrom", 7) == 0) {
        if (endptr != NULL) {
            *endptr = (char *)value + 7;
        }
        return PC_AI_MOVE_RUN_FROM_BOND;
    }

    if (strncmp(value, "cover", 5) == 0) {
        if (endptr != NULL) {
            *endptr = (char *)value + 5;
        }
        return PC_AI_MOVE_FIND_COVER;
    }

    if (strncmp(value, "sidestep", 8) == 0) {
        if (endptr != NULL) {
            *endptr = (char *)value + 8;
        }
        return PC_AI_MOVE_SIDESTEP;
    }

    if (strncmp(value, "sidehop", 7) == 0) {
        if (endptr != NULL) {
            *endptr = (char *)value + 7;
        }
        return PC_AI_MOVE_SIDEHOP;
    }

    if (strncmp(value, "siderun", 7) == 0) {
        if (endptr != NULL) {
            *endptr = (char *)value + 7;
        }
        return PC_AI_MOVE_SIDERUN;
    }

    if (strncmp(value, "firewalk", 8) == 0) {
        if (endptr != NULL) {
            *endptr = (char *)value + 8;
        }
        return PC_AI_MOVE_FIRE_WALK;
    }

    if (strncmp(value, "firerun", 7) == 0) {
        if (endptr != NULL) {
            *endptr = (char *)value + 7;
        }
        return PC_AI_MOVE_FIRE_RUN;
    }

    if (strncmp(value, "fireroll", 8) == 0) {
        if (endptr != NULL) {
            *endptr = (char *)value + 8;
        }
        return PC_AI_MOVE_FIRE_ROLL;
    }

    if (*value == '2' && *(value + 1) == '7') {
        if (endptr != NULL) {
            *endptr = (char *)value + 2;
        }
        return PC_AI_MOVE_RUN_FROM_BOND;
    }

    if ((*value == '2' && *(value + 1) == 'b') || (*value == '2' && *(value + 1) == 'B')) {
        if (endptr != NULL) {
            *endptr = (char *)value + 2;
        }
        return PC_AI_MOVE_FIND_COVER;
    }

    if ((*value == '2' && *(value + 1) == '0') || (*value == 's' && *(value + 1) == 's')) {
        if (endptr != NULL) {
            *endptr = (char *)value + 2;
        }
        return PC_AI_MOVE_SIDESTEP;
    }

    if ((*value == '2' && *(value + 1) == '1') || (*value == 's' && *(value + 1) == 'h')) {
        if (endptr != NULL) {
            *endptr = (char *)value + 2;
        }
        return PC_AI_MOVE_SIDEHOP;
    }

    if ((*value == '2' && *(value + 1) == '2') || (*value == 's' && *(value + 1) == 'r')) {
        if (endptr != NULL) {
            *endptr = (char *)value + 2;
        }
        return PC_AI_MOVE_SIDERUN;
    }

    if ((*value == '2' && *(value + 1) == '3') || (*value == 'f' && *(value + 1) == 'w')) {
        if (endptr != NULL) {
            *endptr = (char *)value + 2;
        }
        return PC_AI_MOVE_FIRE_WALK;
    }

    if ((*value == '2' && *(value + 1) == '4') || (*value == 'f' && *(value + 1) == 'r')) {
        if (endptr != NULL) {
            *endptr = (char *)value + 2;
        }
        return PC_AI_MOVE_FIRE_RUN;
    }

    if ((*value == '2' && *(value + 1) == '5') || (*value == 'f' && *(value + 1) == 'o')) {
        if (endptr != NULL) {
            *endptr = (char *)value + 2;
        }
        return PC_AI_MOVE_FIRE_ROLL;
    }

    if (endptr != NULL) {
        *endptr = (char *)value;
    }
    return 0;
}

static int pcParseScriptedAIReactMode(const char *value, char **endptr)
{
    if (value == NULL) {
        if (endptr != NULL) {
            *endptr = (char *)value;
        }
        return 0;
    }

    if (strncmp(value, "point", 5) == 0) {
        if (endptr != NULL) {
            *endptr = (char *)value + 5;
        }
        return PC_AI_REACT_POINT;
    }

    if (strncmp(value, "look", 4) == 0) {
        if (endptr != NULL) {
            *endptr = (char *)value + 4;
        }
        return PC_AI_REACT_LOOK;
    }

    if (strncmp(value, "surrender", 9) == 0) {
        if (endptr != NULL) {
            *endptr = (char *)value + 9;
        }
        return PC_AI_REACT_SURRENDER;
    }

    if (strncmp(value, "drop_surrender", 14) == 0) {
        if (endptr != NULL) {
            *endptr = (char *)value + 14;
        }
        return PC_AI_REACT_DROP_SURRENDER;
    }

    if (strncmp(value, "drop_fresh_right", 16) == 0) {
        if (endptr != NULL) {
            *endptr = (char *)value + 16;
        }
        return PC_AI_REACT_DROP_FRESH_RIGHT;
    }

    if (strncmp(value, "alarm", 5) == 0) {
        if (endptr != NULL) {
            *endptr = (char *)value + 5;
        }
        return PC_AI_REACT_STARTALARM;
    }

    {
        long parsed = strtol(value, endptr, 0);

        if (parsed >= PC_AI_REACT_POINT && parsed <= PC_AI_REACT_DROP_FRESH_RIGHT) {
            return (int)parsed;
        }
    }

    return 0;
}

static void pcInitScriptedAIMoveChr(void)
{
    const char *script_env;

    if (s_autoAIMoveChrInitialized) {
        return;
    }

    s_autoAIMoveChrInitialized = 1;
    memset(s_autoAIMoveChrEvents, 0, sizeof(s_autoAIMoveChrEvents));

    script_env = getenv("GE007_AUTO_AIMOVE_CHR_SCRIPT");

    if (script_env != NULL && *script_env != '\0' &&
        !(script_env[0] == '0' && script_env[1] == '\0')) {
        while (*script_env != '\0' && s_autoAIMoveChrCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            char *endptr;
            long frame;
            long chrnum;
            int mode;

            while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
                script_env++;
            }

            if (*script_env == '\0') {
                break;
            }

            frame = strtol(script_env, &endptr, 10);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            chrnum = strtol(script_env, &endptr, 10);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            mode = pcParseScriptedAIMoveMode(script_env, &endptr);
            if (mode == 0 || endptr == script_env) {
                break;
            }

            s_autoAIMoveChrEvents[s_autoAIMoveChrCount].frame = (int)frame;
            s_autoAIMoveChrEvents[s_autoAIMoveChrCount].chrnum = (int)chrnum;
            s_autoAIMoveChrEvents[s_autoAIMoveChrCount].mode = mode;
            s_autoAIMoveChrEvents[s_autoAIMoveChrCount].applied = 0;
            s_autoAIMoveChrCount++;
            script_env = endptr;
        }
    }

    if (s_autoAIMoveChrCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_AIMOVE_CHR_FRAME");
        const char *chr_env = getenv("GE007_AUTO_AIMOVE_CHRNUM");
        const char *mode_env = getenv("GE007_AUTO_AIMOVE_CHR_MODE");
        int frame = -1;
        int chrnum = -1;
        int mode = 0;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 10);
        }

        if (chr_env != NULL && *chr_env != '\0') {
            chrnum = (int)strtol(chr_env, NULL, 10);
        }

        if (mode_env != NULL && *mode_env != '\0') {
            mode = pcParseScriptedAIMoveMode(mode_env, NULL);
        }

        if (frame >= 0 && chrnum >= 0 && mode != 0) {
            s_autoAIMoveChrEvents[0].frame = frame;
            s_autoAIMoveChrEvents[0].chrnum = chrnum;
            s_autoAIMoveChrEvents[0].mode = mode;
            s_autoAIMoveChrEvents[0].applied = 0;
            s_autoAIMoveChrCount = 1;
        }
    }
}

static void pcMaybeApplyScriptedAIMoveChr(int input_frame)
{
    int i;

    pcInitScriptedAIMoveChr();

    if (!g_deterministic || s_autoAIMoveChrCount == 0) {
        return;
    }

    for (i = 0; i < s_autoAIMoveChrCount; i++) {
        ChrRecord *chr;
        int applied = 0;
        int mode = s_autoAIMoveChrEvents[i].mode;

        if (s_autoAIMoveChrEvents[i].applied ||
            s_autoAIMoveChrEvents[i].frame < 0 ||
            s_autoAIMoveChrEvents[i].chrnum < 0 ||
            input_frame != s_autoAIMoveChrEvents[i].frame) {
            continue;
        }

        chr = chrFindByLiteralId(s_autoAIMoveChrEvents[i].chrnum);

        if (chr == NULL || chr->prop == NULL || chr->model == NULL) {
            continue;
        }

        switch (mode) {
            case PC_AI_MOVE_RUN_FROM_BOND:
                applied = removed_animation_routine_27(chr);
                break;
            case PC_AI_MOVE_FIND_COVER:
                applied = removed_animation_routine_2B(chr);
                break;
            case PC_AI_MOVE_SIDESTEP:
                applied = actor_steps_sideways(chr);
                break;
            case PC_AI_MOVE_SIDEHOP:
                applied = actor_hops_sideways(chr);
                break;
            case PC_AI_MOVE_SIDERUN:
                applied = actor_jogs_sideways(chr);
                break;
            case PC_AI_MOVE_FIRE_WALK:
                applied = actor_walks_and_fires(chr);
                break;
            case PC_AI_MOVE_FIRE_RUN:
                applied = actor_runs_and_fires(chr);
                break;
            case PC_AI_MOVE_FIRE_ROLL:
                applied = actor_rolls_fires_crouched(chr);
                break;
        }

        if (applied) {
            s_autoAIMoveChrEvents[i].applied = 1;
        }
    }
}

static void pcInitScriptedAIReactChr(void)
{
    const char *script_env;

    if (s_autoAIReactChrInitialized) {
        return;
    }

    s_autoAIReactChrInitialized = 1;
    memset(s_autoAIReactChrEvents, 0, sizeof(s_autoAIReactChrEvents));

    script_env = getenv("GE007_AUTO_AIREACT_CHR_SCRIPT");

    if (script_env != NULL && *script_env != '\0' &&
        !(script_env[0] == '0' && script_env[1] == '\0')) {
        while (*script_env != '\0' && s_autoAIReactChrCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            char *endptr;
            long frame;
            long chrnum;
            int mode;

            while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
                script_env++;
            }

            if (*script_env == '\0') {
                break;
            }

            frame = strtol(script_env, &endptr, 10);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            chrnum = strtol(script_env, &endptr, 10);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            mode = pcParseScriptedAIReactMode(script_env, &endptr);
            if (mode == 0 || endptr == script_env) {
                break;
            }

            script_env = endptr;

            s_autoAIReactChrEvents[s_autoAIReactChrCount].frame = (int)frame;
            s_autoAIReactChrEvents[s_autoAIReactChrCount].chrnum = (int)chrnum;
            s_autoAIReactChrEvents[s_autoAIReactChrCount].mode = mode;
            s_autoAIReactChrEvents[s_autoAIReactChrCount].applied = 0;
            s_autoAIReactChrCount++;
        }
    }

    if (s_autoAIReactChrCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_AIREACT_CHR_FRAME");
        const char *chr_env = getenv("GE007_AUTO_AIREACT_CHRNUM");
        const char *mode_env = getenv("GE007_AUTO_AIREACT_CHR_MODE");
        int frame = -1;
        int chrnum = -1;
        int mode = 0;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 10);
        }

        if (chr_env != NULL && *chr_env != '\0') {
            chrnum = (int)strtol(chr_env, NULL, 10);
        }

        if (mode_env != NULL && *mode_env != '\0') {
            mode = pcParseScriptedAIReactMode(mode_env, NULL);
        }

        if (frame >= 0 && chrnum >= 0 && mode != 0) {
            s_autoAIReactChrEvents[0].frame = frame;
            s_autoAIReactChrEvents[0].chrnum = chrnum;
            s_autoAIReactChrEvents[0].mode = mode;
            s_autoAIReactChrEvents[0].applied = 0;
            s_autoAIReactChrCount = 1;
        }
    }
}

static void pcMaybeApplyScriptedAIReactChr(int input_frame)
{
    int i;

    pcInitScriptedAIReactChr();

    if (!g_deterministic || s_autoAIReactChrCount == 0) {
        return;
    }

    for (i = 0; i < s_autoAIReactChrCount; i++) {
        ChrRecord *chr;
        int applied = 0;

        if (s_autoAIReactChrEvents[i].applied ||
            s_autoAIReactChrEvents[i].frame < 0 ||
            s_autoAIReactChrEvents[i].chrnum < 0 ||
            input_frame != s_autoAIReactChrEvents[i].frame) {
            continue;
        }

        chr = chrFindByLiteralId(s_autoAIReactChrEvents[i].chrnum);

        if (chr == NULL || chr->prop == NULL || chr->model == NULL) {
            continue;
        }

        switch (s_autoAIReactChrEvents[i].mode) {
            case PC_AI_REACT_POINT:
                applied = chrTrySurprisedOneHand(chr) ? 1 : 0;
                break;
            case PC_AI_REACT_LOOK:
                applied = chrTrySurprisedLookAround(chr) ? 1 : 0;
                break;
            case PC_AI_REACT_SURRENDER:
                applied = chrTrySurprisedSurrender(chr) ? 1 : 0;
                break;
            case PC_AI_REACT_STARTALARM:
                applied = chrTryStartAlarm(chr, PAD_PRESET1) ? 1 : 0;
                break;
            case PC_AI_REACT_DROP_SURRENDER:
                applied = chrTrySurrender(chr) ? 1 : 0;
                break;
            case PC_AI_REACT_DROP_FRESH_RIGHT:
                if (chr->weapons_held[GUNRIGHT] != NULL &&
                    chr->weapons_held[GUNRIGHT]->weapon != NULL) {
                    WeaponObjRecord *weapon = chr->weapons_held[GUNRIGHT]->weapon;
                    applied = chrDropItem(chr, weapon->obj, weapon->weaponnum) ? 1 : 0;
                }
                break;
        }

        if (applied) {
            s_autoAIReactChrEvents[i].applied = 1;
        }
    }
}

static void pcInitScriptedFaceChr(void)
{
    const char *script_env;

    if (s_autoFaceChrInitialized) {
        return;
    }

    s_autoFaceChrInitialized = 1;
    memset(s_autoFaceChrEvents, 0, sizeof(s_autoFaceChrEvents));

    script_env = getenv("GE007_AUTO_FACE_CHR_SCRIPT");

    if (script_env != NULL && *script_env != '\0' &&
        !(script_env[0] == '0' && script_env[1] == '\0')) {
        while (*script_env != '\0' && s_autoFaceChrCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            char *endptr;
            long frame;
            long chrnum;
            float yaw_offset_deg = 0.0f;

            while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
                script_env++;
            }

            if (*script_env == '\0') {
                break;
            }

            frame = strtol(script_env, &endptr, 10);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            chrnum = strtol(script_env, &endptr, 10);
            if (endptr == script_env) {
                break;
            }

            script_env = endptr;

            if (*script_env == ':') {
                script_env++;
                yaw_offset_deg = strtof(script_env, &endptr);
                if (endptr != script_env) {
                    script_env = endptr;
                }
            }

            s_autoFaceChrEvents[s_autoFaceChrCount].frame = (int)frame;
            s_autoFaceChrEvents[s_autoFaceChrCount].chrnum = (int)chrnum;
            s_autoFaceChrEvents[s_autoFaceChrCount].yaw_offset_deg = yaw_offset_deg;
            s_autoFaceChrEvents[s_autoFaceChrCount].applied = 0;
            s_autoFaceChrCount++;
        }
    }

    if (s_autoFaceChrCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_FACE_CHR_FRAME");
        const char *chr_env = getenv("GE007_AUTO_FACE_CHRNUM");
        const char *yaw_env = getenv("GE007_AUTO_FACE_CHR_YAW_OFFSET");
        int frame = -1;
        int chrnum = -1;
        float yaw_offset_deg = 0.0f;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 10);
        }

        if (chr_env != NULL && *chr_env != '\0') {
            chrnum = (int)strtol(chr_env, NULL, 10);
        }

        if (yaw_env != NULL && *yaw_env != '\0') {
            yaw_offset_deg = strtof(yaw_env, NULL);
        }

        if (frame >= 0 && chrnum >= 0) {
            s_autoFaceChrEvents[0].frame = frame;
            s_autoFaceChrEvents[0].chrnum = chrnum;
            s_autoFaceChrEvents[0].yaw_offset_deg = yaw_offset_deg;
            s_autoFaceChrEvents[0].applied = 0;
            s_autoFaceChrCount = 1;
        }
    }
}

static void pcMaybeApplyScriptedFaceChr(int input_frame)
{
    int i;

    pcInitScriptedFaceChr();

    if (!g_deterministic || s_autoFaceChrCount == 0 || g_CurrentPlayer == NULL || g_CurrentPlayer->prop == NULL) {
        return;
    }

    for (i = 0; i < s_autoFaceChrCount; i++) {
        ChrRecord *chr;
        float dx;
        float dz;
        float yaw_deg;
        float angle_rad;

        if (s_autoFaceChrEvents[i].applied ||
            s_autoFaceChrEvents[i].frame < 0 ||
            s_autoFaceChrEvents[i].chrnum < 0 ||
            input_frame != s_autoFaceChrEvents[i].frame) {
            continue;
        }

        chr = chrFindByLiteralId(s_autoFaceChrEvents[i].chrnum);

        if (chr == NULL || chr->prop == NULL) {
            continue;
        }

        dx = chr->prop->pos.x - g_CurrentPlayer->prop->pos.x;
        dz = chr->prop->pos.z - g_CurrentPlayer->prop->pos.z;

        if ((dx * dx) + (dz * dz) < 1.0f) {
            continue;
        }

        angle_rad = M_TAU_F - atan2f(dx, dz);
        yaw_deg = pcWrapYawDegrees((angle_rad * 360.0f / M_TAU_F) + s_autoFaceChrEvents[i].yaw_offset_deg);

        g_CurrentPlayer->vv_theta = yaw_deg;
        bondviewApplyVertaTheta();
        s_autoFaceChrEvents[i].applied = 1;

        if (getenv("GE007_VERBOSE")) {
            fprintf(stderr,
                    "[AUTO_FACE_CHR] frame=%d chr=%d yaw=%.2f offset=%.2f dx=%.1f dz=%.1f\n",
                    input_frame, s_autoFaceChrEvents[i].chrnum,
                    yaw_deg, s_autoFaceChrEvents[i].yaw_offset_deg, dx, dz);
            fflush(stderr);
        }
    }
}

static void pcParseScriptedFaceCoordEvents(const char *script_env)
{
    if (script_env == NULL || *script_env == '\0' ||
        (script_env[0] == '0' && script_env[1] == '\0')) {
        return;
    }

    while (*script_env != '\0' && s_autoFaceCoordCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
        char *endptr;
        long frame;
        long frame_end;
        float x;
        float y;
        float z;
        float yaw_offset_deg = 0.0f;
        float pitch_offset_deg = 0.0f;

        while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
            script_env++;
        }

        if (*script_env == '\0') {
            break;
        }

        frame = strtol(script_env, &endptr, 10);
        if (endptr == script_env) {
            break;
        }
        frame_end = frame;
        if (*endptr == '-') {
            char *range_endptr;
            frame_end = strtol(endptr + 1, &range_endptr, 10);
            if (range_endptr == endptr + 1 || frame_end < frame) {
                break;
            }
            endptr = range_endptr;
        }
        if (*endptr != ':') {
            break;
        }

        script_env = endptr + 1;
        x = strtof(script_env, &endptr);
        if (endptr == script_env || *endptr != ':') {
            break;
        }

        script_env = endptr + 1;
        y = strtof(script_env, &endptr);
        if (endptr == script_env || *endptr != ':') {
            break;
        }

        script_env = endptr + 1;
        z = strtof(script_env, &endptr);
        if (endptr == script_env) {
            break;
        }

        script_env = endptr;

        if (*script_env == ':') {
            script_env++;
            yaw_offset_deg = strtof(script_env, &endptr);
            if (endptr != script_env) {
                script_env = endptr;
            }
        }

        if (*script_env == ':') {
            script_env++;
            pitch_offset_deg = strtof(script_env, &endptr);
            if (endptr != script_env) {
                script_env = endptr;
            }
        }

        while (frame <= frame_end && s_autoFaceCoordCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            s_autoFaceCoordEvents[s_autoFaceCoordCount].frame = (int)frame;
            s_autoFaceCoordEvents[s_autoFaceCoordCount].x = x;
            s_autoFaceCoordEvents[s_autoFaceCoordCount].y = y;
            s_autoFaceCoordEvents[s_autoFaceCoordCount].z = z;
            s_autoFaceCoordEvents[s_autoFaceCoordCount].yaw_offset_deg = yaw_offset_deg;
            s_autoFaceCoordEvents[s_autoFaceCoordCount].pitch_offset_deg = pitch_offset_deg;
            s_autoFaceCoordEvents[s_autoFaceCoordCount].applied = 0;
            s_autoFaceCoordCount++;
            frame++;
        }
    }
}

static void pcInitScriptedFaceCoord(void)
{
    if (s_autoFaceCoordInitialized) {
        return;
    }

    s_autoFaceCoordInitialized = 1;
    memset(s_autoFaceCoordEvents, 0, sizeof(s_autoFaceCoordEvents));
    s_autoFaceCoordCount = 0;

    pcParseScriptedFaceCoordEvents(getenv("GE007_AUTO_FACE_COORD_SCRIPT"));
    pcParseScriptedFaceCoordEvents(getenv("GE007_AUTO_FACE_COORD_SCRIPT_EXTRA"));

    if (s_autoFaceCoordCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_FACE_COORD_FRAME");
        const char *x_env = getenv("GE007_AUTO_FACE_COORD_X");
        const char *y_env = getenv("GE007_AUTO_FACE_COORD_Y");
        const char *z_env = getenv("GE007_AUTO_FACE_COORD_Z");
        const char *yaw_env = getenv("GE007_AUTO_FACE_COORD_YAW_OFFSET");
        const char *pitch_env = getenv("GE007_AUTO_FACE_COORD_PITCH_OFFSET");
        int frame = -1;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw_offset_deg = 0.0f;
        float pitch_offset_deg = 0.0f;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 10);
        }

        if (x_env != NULL && *x_env != '\0') {
            x = strtof(x_env, NULL);
        }

        if (y_env != NULL && *y_env != '\0') {
            y = strtof(y_env, NULL);
        }

        if (z_env != NULL && *z_env != '\0') {
            z = strtof(z_env, NULL);
        }

        if (yaw_env != NULL && *yaw_env != '\0') {
            yaw_offset_deg = strtof(yaw_env, NULL);
        }

        if (pitch_env != NULL && *pitch_env != '\0') {
            pitch_offset_deg = strtof(pitch_env, NULL);
        }

        if (frame >= 0 &&
            x_env != NULL && *x_env != '\0' &&
            y_env != NULL && *y_env != '\0' &&
            z_env != NULL && *z_env != '\0') {
            s_autoFaceCoordEvents[0].frame = frame;
            s_autoFaceCoordEvents[0].x = x;
            s_autoFaceCoordEvents[0].y = y;
            s_autoFaceCoordEvents[0].z = z;
            s_autoFaceCoordEvents[0].yaw_offset_deg = yaw_offset_deg;
            s_autoFaceCoordEvents[0].pitch_offset_deg = pitch_offset_deg;
            s_autoFaceCoordEvents[0].applied = 0;
            s_autoFaceCoordCount = 1;
        }
    }
}

static void pcMaybeApplyScriptedFaceCoord(int input_frame)
{
    int i;

    pcInitScriptedFaceCoord();

    if (!g_deterministic || s_autoFaceCoordCount == 0 || g_CurrentPlayer == NULL || g_CurrentPlayer->prop == NULL) {
        return;
    }

    for (i = 0; i < s_autoFaceCoordCount; i++) {
        float dx;
        float dy;
        float dz;
        float yaw_deg;
        float pitch_deg;
        float angle_rad;
        float horiz_dist;

        if (s_autoFaceCoordEvents[i].applied ||
            s_autoFaceCoordEvents[i].frame < 0 ||
            input_frame != s_autoFaceCoordEvents[i].frame) {
            continue;
        }

        dx = s_autoFaceCoordEvents[i].x - g_CurrentPlayer->prop->pos.x;
        dy = s_autoFaceCoordEvents[i].y - g_CurrentPlayer->prop->pos.y;
        dz = s_autoFaceCoordEvents[i].z - g_CurrentPlayer->prop->pos.z;
        horiz_dist = sqrtf((dx * dx) + (dz * dz));

        if (horiz_dist < 1.0f && fabsf(dy) < 1.0f) {
            continue;
        }

        angle_rad = M_TAU_F - atan2f(dx, dz);
        yaw_deg = pcWrapYawDegrees(
            (angle_rad * 360.0f / M_TAU_F) + s_autoFaceCoordEvents[i].yaw_offset_deg);
        pitch_deg = (pcSignedAtan2f(dy, horiz_dist) * 360.0f / M_TAU_F)
            + s_autoFaceCoordEvents[i].pitch_offset_deg;

        g_CurrentPlayer->vv_theta = yaw_deg;
        g_CurrentPlayer->vv_verta = pitch_deg;
        g_CurrentPlayer->speedtheta = 0.0f;
        g_CurrentPlayer->speedverta = 0.0f;
        bondviewApplyVertaTheta();
        s_autoFaceCoordEvents[i].applied = 1;

        if (getenv("GE007_VERBOSE")) {
            fprintf(stderr,
                    "[AUTO_FACE_COORD] frame=%d target=(%.1f,%.1f,%.1f) yaw=%.2f pitch=%.2f yoff=%.2f poff=%.2f dx=%.1f dy=%.1f dz=%.1f\n",
                    input_frame,
                    s_autoFaceCoordEvents[i].x,
                    s_autoFaceCoordEvents[i].y,
                    s_autoFaceCoordEvents[i].z,
                    yaw_deg,
                    pitch_deg,
                    s_autoFaceCoordEvents[i].yaw_offset_deg,
                    s_autoFaceCoordEvents[i].pitch_offset_deg,
                    dx,
                    dy,
                    dz);
            fflush(stderr);
        }
    }
}

static void pcParseScriptedForcePlayerEvents(const char *script_env)
{
    if (script_env == NULL || *script_env == '\0' ||
        (script_env[0] == '0' && script_env[1] == '\0')) {
        return;
    }

    while (*script_env != '\0' && s_autoForcePlayerCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
        char *endptr;
        long frame;
        long frame_end;
        float x;
        float y;
        float z;
        float yaw_deg;
        float pitch_deg;
        float camera_height = 0.0f;
        int has_camera_height = 0;
        int pad = -1;

        while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
            script_env++;
        }

        if (*script_env == '\0') {
            break;
        }

        frame = strtol(script_env, &endptr, 10);
        if (endptr == script_env) {
            break;
        }

        frame_end = frame;
        if (*endptr == '-') {
            char *range_endptr;
            frame_end = strtol(endptr + 1, &range_endptr, 10);
            if (range_endptr == endptr + 1 || frame_end < frame) {
                break;
            }
            endptr = range_endptr;
        }

        if (*endptr != ':') {
            break;
        }

        script_env = endptr + 1;
        x = strtof(script_env, &endptr);
        if (endptr == script_env || *endptr != ':') {
            break;
        }

        script_env = endptr + 1;
        y = strtof(script_env, &endptr);
        if (endptr == script_env || *endptr != ':') {
            break;
        }

        script_env = endptr + 1;
        z = strtof(script_env, &endptr);
        if (endptr == script_env || *endptr != ':') {
            break;
        }

        script_env = endptr + 1;
        yaw_deg = strtof(script_env, &endptr);
        if (endptr == script_env || *endptr != ':') {
            break;
        }

        script_env = endptr + 1;
        pitch_deg = strtof(script_env, &endptr);
        if (endptr == script_env) {
            break;
        }

        script_env = endptr;

        if (*script_env == ':') {
            script_env++;
            camera_height = strtof(script_env, &endptr);
            if (endptr == script_env) {
                break;
            }
            has_camera_height = 1;
            script_env = endptr;
        }

        if (*script_env == ':') {
            long pad_value;

            script_env++;
            pad_value = strtol(script_env, &endptr, 10);
            if (endptr == script_env) {
                break;
            }
            pad = (int)pad_value;
            script_env = endptr;
        }

        while (frame <= frame_end && s_autoForcePlayerCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            s_autoForcePlayerEvents[s_autoForcePlayerCount].frame = (int)frame;
            s_autoForcePlayerEvents[s_autoForcePlayerCount].x = x;
            s_autoForcePlayerEvents[s_autoForcePlayerCount].y = y;
            s_autoForcePlayerEvents[s_autoForcePlayerCount].z = z;
            s_autoForcePlayerEvents[s_autoForcePlayerCount].yaw_deg = yaw_deg;
            s_autoForcePlayerEvents[s_autoForcePlayerCount].pitch_deg = pitch_deg;
            s_autoForcePlayerEvents[s_autoForcePlayerCount].has_camera_height = has_camera_height;
            s_autoForcePlayerEvents[s_autoForcePlayerCount].camera_height = camera_height;
            s_autoForcePlayerEvents[s_autoForcePlayerCount].pad = pad;
            s_autoForcePlayerEvents[s_autoForcePlayerCount].applied = 0;
            s_autoForcePlayerCount++;
            frame++;
        }
    }
}

static void pcInitScriptedForcePlayer(void)
{
    if (s_autoForcePlayerInitialized) {
        return;
    }

    s_autoForcePlayerInitialized = 1;
    memset(s_autoForcePlayerEvents, 0, sizeof(s_autoForcePlayerEvents));
    s_autoForcePlayerCount = 0;

    pcParseScriptedForcePlayerEvents(getenv("GE007_AUTO_FORCE_PLAYER_SCRIPT"));
    pcParseScriptedForcePlayerEvents(getenv("GE007_AUTO_FORCE_PLAYER_SCRIPT_EXTRA"));
}

static void pcMaybeApplyScriptedForcePlayer(int input_frame)
{
    int i;

    pcInitScriptedForcePlayer();

    if (!g_deterministic || s_autoForcePlayerCount == 0 ||
        g_CurrentPlayer == NULL || g_CurrentPlayer->prop == NULL) {
        return;
    }

    for (i = 0; i < s_autoForcePlayerCount; i++) {
        PcScriptedForcePlayerEvent *event = &s_autoForcePlayerEvents[i];
        PadRecord *pad = NULL;
        StandTile *stan;
        struct coord3d pos;
        float camera_height;
        float floor_y;

        if (event->applied ||
            event->frame < 0 ||
            input_frame != event->frame) {
            continue;
        }

        if (event->pad >= 0) {
            pad = pcResolveScriptedPad(event->pad);
        }

        stan = pad != NULL ? pad->stan : g_CurrentPlayer->field_488.current_tile_ptr;
        if (stan == NULL) {
            stan = g_CurrentPlayer->prop->stan;
        }
        if (stan == NULL) {
            continue;
        }

        camera_height = event->has_camera_height
            ? event->camera_height
            : g_CurrentPlayer->field_29BC;
        if (camera_height <= 1.0f) {
            camera_height = g_CurrentPlayer->standheight > 1.0f
                ? g_CurrentPlayer->standheight
                : 160.33334f;
        }

        if (event->has_camera_height) {
            g_CurrentPlayer->field_29BC = camera_height;
        }

        pos.x = event->x;
        pos.y = event->y;
        pos.z = event->z;
        floor_y = event->y - camera_height;

        g_CurrentPlayer->field_70 = floor_y;
        g_CurrentPlayer->stanHeight = floor_y;
        g_CurrentPlayer->field_6C = floor_y / PC_SCRIPTED_FIELD_6C_FACTOR;
        g_CurrentPlayer->field_7C = 0.0f;
        g_CurrentPlayer->field_84 = 0.0f;
        g_CurrentPlayer->field_88 = 0.0f;
        g_CurrentPlayer->vertical_bounce_adjust = 0.0f;
        g_CurrentPlayer->speedforwards = 0.0f;
        g_CurrentPlayer->speedsideways = 0.0f;
        g_CurrentPlayer->speedtheta = 0.0f;
        g_CurrentPlayer->speedverta = 0.0f;
        g_CurrentPlayer->bondshotspeed.x = 0.0f;
        g_CurrentPlayer->bondshotspeed.y = 0.0f;
        g_CurrentPlayer->bondshotspeed.z = 0.0f;

        change_player_pos_to_target(&g_CurrentPlayer->field_488, &pos, stan);

        g_CurrentPlayer->prop->pos.x = pos.x;
        g_CurrentPlayer->prop->pos.y = pos.y;
        g_CurrentPlayer->prop->pos.z = pos.z;
        g_CurrentPlayer->prop->stan = stan;
        g_CurrentPlayer->bondprevpos.x = pos.x;
        g_CurrentPlayer->bondprevpos.y = pos.y;
        g_CurrentPlayer->bondprevpos.z = pos.z;
        g_CurrentPlayer->field_3B8.f[0] = g_CurrentPlayer->field_488.pos.f[0] / PC_SCRIPTED_FIELD_3B8_FACTOR;
        g_CurrentPlayer->field_3B8.f[1] = g_CurrentPlayer->field_488.pos.f[1] / PC_SCRIPTED_FIELD_3B8_FACTOR;
        g_CurrentPlayer->field_3B8.f[2] = g_CurrentPlayer->field_488.pos.f[2] / PC_SCRIPTED_FIELD_3B8_FACTOR;

        g_CurrentPlayer->vv_theta = pcWrapYawDegrees(event->yaw_deg);
        g_CurrentPlayer->vv_verta = event->pitch_deg;
        bondviewApplyVertaTheta();
        bondviewSyncViewBasisFromHead();

        {
            extern s32 g_BgCurrentRoom;
            g_BgCurrentRoom = stan->room;
        }

        event->applied = 1;

        if (getenv("GE007_VERBOSE")) {
            fprintf(stderr,
                    "[AUTO_FORCE_PLAYER] frame=%d pos=(%.2f,%.2f,%.2f) yaw=%.2f pitch=%.2f height=%.2f pad=%d room=%d\n",
                    input_frame,
                    pos.x,
                    pos.y,
                    pos.z,
                    g_CurrentPlayer->vv_theta,
                    g_CurrentPlayer->vv_verta,
                    camera_height,
                    event->pad,
                    stan->room);
            fflush(stderr);
        }
    }
}

static void pcInitScriptedTankFaceCoord(void)
{
    const char *script_env;

    if (s_autoTankFaceCoordInitialized) {
        return;
    }

    s_autoTankFaceCoordInitialized = 1;
    memset(s_autoTankFaceCoordEvents, 0, sizeof(s_autoTankFaceCoordEvents));

    script_env = getenv("GE007_AUTO_TANK_FACE_COORD_SCRIPT");

    if (script_env != NULL && *script_env != '\0' &&
        !(script_env[0] == '0' && script_env[1] == '\0')) {
        while (*script_env != '\0' && s_autoTankFaceCoordCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            char *endptr;
            long frame;
            float x;
            float y;
            float z;

            while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
                script_env++;
            }

            if (*script_env == '\0') {
                break;
            }

            frame = strtol(script_env, &endptr, 10);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            x = strtof(script_env, &endptr);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            y = strtof(script_env, &endptr);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            z = strtof(script_env, &endptr);
            if (endptr == script_env) {
                break;
            }

            script_env = endptr;

            s_autoTankFaceCoordEvents[s_autoTankFaceCoordCount].frame = (int)frame;
            s_autoTankFaceCoordEvents[s_autoTankFaceCoordCount].x = x;
            s_autoTankFaceCoordEvents[s_autoTankFaceCoordCount].y = y;
            s_autoTankFaceCoordEvents[s_autoTankFaceCoordCount].z = z;
            s_autoTankFaceCoordEvents[s_autoTankFaceCoordCount].applied = 0;
            s_autoTankFaceCoordCount++;
        }
    }

    if (s_autoTankFaceCoordCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_TANK_FACE_COORD_FRAME");
        const char *x_env = getenv("GE007_AUTO_TANK_FACE_COORD_X");
        const char *y_env = getenv("GE007_AUTO_TANK_FACE_COORD_Y");
        const char *z_env = getenv("GE007_AUTO_TANK_FACE_COORD_Z");
        int frame = -1;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        if (frame_env != NULL && *frame_env != '\0') {
            frame = (int)strtol(frame_env, NULL, 10);
        }

        if (x_env != NULL && *x_env != '\0') {
            x = strtof(x_env, NULL);
        }

        if (y_env != NULL && *y_env != '\0') {
            y = strtof(y_env, NULL);
        }

        if (z_env != NULL && *z_env != '\0') {
            z = strtof(z_env, NULL);
        }

        if (frame >= 0 &&
            x_env != NULL && *x_env != '\0' &&
            y_env != NULL && *y_env != '\0' &&
            z_env != NULL && *z_env != '\0') {
            s_autoTankFaceCoordEvents[0].frame = frame;
            s_autoTankFaceCoordEvents[0].x = x;
            s_autoTankFaceCoordEvents[0].y = y;
            s_autoTankFaceCoordEvents[0].z = z;
            s_autoTankFaceCoordEvents[0].applied = 0;
            s_autoTankFaceCoordCount = 1;
        }
    }
}

static void pcMaybeApplyScriptedTankFaceCoord(int input_frame)
{
    int i;

    pcInitScriptedTankFaceCoord();

    if (!g_deterministic || s_autoTankFaceCoordCount == 0) {
        return;
    }

    for (i = 0; i < s_autoTankFaceCoordCount; i++) {
        if (s_autoTankFaceCoordEvents[i].applied ||
            s_autoTankFaceCoordEvents[i].frame < 0 ||
            input_frame != s_autoTankFaceCoordEvents[i].frame) {
            continue;
        }

        if (!isBondInTank()) {
            continue;
        }

        portTankAimAtCoord(s_autoTankFaceCoordEvents[i].x,
                           s_autoTankFaceCoordEvents[i].y,
                           s_autoTankFaceCoordEvents[i].z);
        s_autoTankFaceCoordEvents[i].applied = 1;

        if (getenv("GE007_VERBOSE")) {
            fprintf(stderr,
                    "[AUTO_TANK_FACE_COORD] frame=%d target=(%.1f,%.1f,%.1f)\n",
                    input_frame,
                    s_autoTankFaceCoordEvents[i].x,
                    s_autoTankFaceCoordEvents[i].y,
                    s_autoTankFaceCoordEvents[i].z);
            fflush(stderr);
        }
    }
}

static void pcInitScriptedArmor(void)
{
    if (s_autoArmorInitialized) {
        return;
    }

    s_autoArmorInitialized = 1;

    {
        const char *frame_env = getenv("GE007_AUTO_ARMOR_FRAME");
        const char *amount_env = getenv("GE007_AUTO_ARMOR_AMOUNT");

        if (frame_env != NULL && *frame_env != '\0') {
            s_autoArmorFrame = (int)strtol(frame_env, NULL, 10);
        }

        if (amount_env != NULL && *amount_env != '\0') {
            s_autoArmorAmount = strtof(amount_env, NULL);
            if (s_autoArmorAmount < 0.0f) {
                s_autoArmorAmount = 0.0f;
            }
        }
    }
}

static void pcMaybeApplyScriptedArmor(int input_frame)
{
    pcInitScriptedArmor();

    if (!g_deterministic || s_autoArmorDone || s_autoArmorFrame < 0 || s_autoArmorAmount <= 0.0f) {
        return;
    }

    if (input_frame != s_autoArmorFrame) {
        return;
    }

    if (g_CurrentPlayer == NULL || g_playerPerm == NULL) {
        return;
    }

    bondviewAddCurrentPlayerArmor(s_autoArmorAmount);
    s_autoArmorDone = 1;
}

static void pcInitScriptedMpMenu(void)
{
    if (s_autoMpMenuInitialized) {
        return;
    }

    s_autoMpMenuInitialized = 1;

    {
        const char *frame_env = getenv("GE007_AUTO_MPMENU");

        if (frame_env != NULL && *frame_env != '\0') {
            s_autoMpMenuFrame = (int)strtol(frame_env, NULL, 10);
        }
    }
}

/* Hold the MP watch/pause menu open on all panes from s_autoMpMenuFrame onward.
 * Mirrors the real setter (mp_watch.c:708-709: mpmenuon=TRUE, mpmenumode=3),
 * but drives every pane so a pane the scripted-input router can't reach (pads
 * 1-3) can still be tested. No-op in solo (getPlayerCount()==1) — the same
 * guard mp_watch.c:561 uses — so it can never perturb single-player state. */
static void pcMaybeApplyScriptedMpMenu(int input_frame)
{
    int count;
    int i;

    pcInitScriptedMpMenu();

    if (!g_deterministic || s_autoMpMenuFrame < 0 || input_frame < s_autoMpMenuFrame) {
        return;
    }

    count = getPlayerCount();
    if (count <= 1) {
        return;
    }

    for (i = 0; i < count; i++) {
        if (g_playerPointers[i] == NULL) {
            continue;
        }
        g_playerPointers[i]->mpmenuon = TRUE;
        g_playerPointers[i]->mpmenumode = 3;
    }
}

static void pcInitScriptedDamageBond(void)
{
    if (s_autoDamageBondInitialized) {
        return;
    }

    s_autoDamageBondInitialized = 1;

    {
        const char *frame_env = getenv("GE007_AUTO_DAMAGE_BOND_FRAME");
        const char *amount_env = getenv("GE007_AUTO_DAMAGE_BOND_AMOUNT");
        const char *angle_env = getenv("GE007_AUTO_DAMAGE_BOND_ANGLE");
        const char *armor_env = getenv("GE007_AUTO_DAMAGE_BOND_AFFECTS_ARMOR");

        if (frame_env != NULL && *frame_env != '\0') {
            s_autoDamageBondFrame = (int)strtol(frame_env, NULL, 10);
        }

        if (amount_env != NULL && *amount_env != '\0') {
            s_autoDamageBondAmount = strtof(amount_env, NULL);
            if (s_autoDamageBondAmount < 0.0f) {
                s_autoDamageBondAmount = 0.0f;
            }
        }

        if (angle_env != NULL && *angle_env != '\0') {
            s_autoDamageBondAngle = strtof(angle_env, NULL);
        }

        if (armor_env != NULL && *armor_env != '\0') {
            s_autoDamageBondAffectsArmor = (int)strtol(armor_env, NULL, 0) != 0;
        }
    }
}

static void pcMaybeApplyScriptedDamageBond(int input_frame)
{
    pcInitScriptedDamageBond();

    if (!g_deterministic || s_autoDamageBondDone ||
        s_autoDamageBondFrame < 0 || s_autoDamageBondAmount <= 0.0f) {
        return;
    }

    if (input_frame != s_autoDamageBondFrame) {
        return;
    }

    if (g_CurrentPlayer == NULL || g_playerPerm == NULL || g_CurrentPlayer->prop == NULL) {
        return;
    }

    bondviewCallRecordDamageKills(s_autoDamageBondAmount,
                                  s_autoDamageBondAngle,
                                  -1,
                                  s_autoDamageBondAffectsArmor);
    s_autoDamageBondDone = 1;
}

static int pcParseScriptedCameraMode(const char *value)
{
    char *end = NULL;
    long parsed;

    if (value == NULL || *value == '\0') {
        return -1;
    }

    if (strcmp(value, "swirl") == 0) {
        return CAMERAMODE_SWIRL;
    }

    if (strcmp(value, "death") == 0 || strcmp(value, "death_sp") == 0) {
        return CAMERAMODE_DEATH_CAM_SP;
    }

    if (strcmp(value, "death_mp") == 0) {
        return CAMERAMODE_DEATH_CAM_MP;
    }

    if (strcmp(value, "posend") == 0 || strcmp(value, "end") == 0) {
        return CAMERAMODE_POSEND;
    }

    if (strcmp(value, "fp") == 0) {
        return CAMERAMODE_FP;
    }

    parsed = strtol(value, &end, 0);
    if (end == value || *end != '\0') {
        return -1;
    }

    switch ((int)parsed) {
        case CAMERAMODE_SWIRL:
        case CAMERAMODE_DEATH_CAM_SP:
        case CAMERAMODE_DEATH_CAM_MP:
        case CAMERAMODE_POSEND:
        case CAMERAMODE_FP:
            return (int)parsed;
        default:
            return -1;
    }
}

static void pcInitScriptedCameraMode(void)
{
    if (s_autoCameraModeInitialized) {
        return;
    }

    s_autoCameraModeInitialized = 1;

    {
        const char *frame_env = getenv("GE007_AUTO_CAMERA_MODE_FRAME");
        const char *mode_env = getenv("GE007_AUTO_CAMERA_MODE");
        const char *posend_pad_env = getenv("GE007_AUTO_CAMERA_POSEND_PAD");

        if (frame_env != NULL && *frame_env != '\0') {
            s_autoCameraModeFrame = (int)strtol(frame_env, NULL, 10);
        }

        s_autoCameraMode = pcParseScriptedCameraMode(mode_env);

        if (posend_pad_env != NULL && *posend_pad_env != '\0') {
            s_autoCameraPosEndPad = (int)strtol(posend_pad_env, NULL, 0);
        }

        if (s_autoCameraModeFrame >= 0 && s_autoCameraMode < 0) {
            fprintf(stderr,
                    "[GE007-PC] invalid GE007_AUTO_CAMERA_MODE='%s'; "
                    "camera-mode automation disabled\n",
                    mode_env != NULL ? mode_env : "");
            s_autoCameraModeFrame = -1;
        }
    }
}

static void pcMaybeApplyScriptedCameraMode(int input_frame)
{
    PadRecord *posend_pad;

    pcInitScriptedCameraMode();

    if (!g_deterministic || s_autoCameraModeDone ||
        s_autoCameraModeFrame < 0 || s_autoCameraMode < 0) {
        return;
    }

    if (input_frame < s_autoCameraModeFrame) {
        return;
    }

    if (bossGetStageNum() == LEVELID_TITLE ||
        lvlGetCurrentStageToLoad() == LEVELID_TITLE ||
        g_CurrentPlayer == NULL || g_CurrentPlayer->prop == NULL) {
        return;
    }

    if (s_autoCameraMode == CAMERAMODE_DEATH_CAM_SP ||
        s_autoCameraMode == CAMERAMODE_DEATH_CAM_MP) {
        if (g_CurrentPlayer->bonddead == 0) {
            if (!s_autoCameraModeWarned) {
                fprintf(stderr,
                        "[GE007-PC] waiting to force death camera until Bond is dead; "
                        "use GE007_AUTO_DAMAGE_BOND_FRAME/AMOUNT for a natural setup\n");
                s_autoCameraModeWarned = 1;
            }
            return;
        }
    }

    if (s_autoCameraMode == CAMERAMODE_POSEND) {
        if (s_autoCameraPosEndPad >= 0) {
            posend_pad = pcResolveScriptedPad(s_autoCameraPosEndPad);
            if (posend_pad == NULL) {
                if (!s_autoCameraModeWarned) {
                    fprintf(stderr,
                            "[GE007-PC] waiting to force posend camera; "
                            "GE007_AUTO_CAMERA_POSEND_PAD=%d is not available yet\n",
                            s_autoCameraPosEndPad);
                    s_autoCameraModeWarned = 1;
                }
                return;
            }

            dword_CODE_bss_800799F8 = posend_pad;
            gBondViewCutscene = NULL;
        } else if (dword_CODE_bss_800799F8 == NULL && gBondViewCutscene == NULL) {
            fprintf(stderr,
                    "[GE007-PC] refusing unseeded posend camera force; "
                    "set GE007_AUTO_CAMERA_POSEND_PAD to a valid setup pad\n");
            s_autoCameraModeDone = 1;
            return;
        }
    }

    bondviewSetCameraMode(s_autoCameraMode);
    s_autoCameraModeDone = 1;
}

static void pcInitScriptedWeaponAmmo(void)
{
    if (s_autoWeaponAmmoInitialized) {
        return;
    }

    s_autoWeaponAmmoInitialized = 1;

    {
        const char *frame_env = getenv("GE007_AUTO_ADD_WEAPON_AMMO_FRAME");
        const char *item_env = getenv("GE007_AUTO_ADD_WEAPON_AMMO");
        const char *amount_env = getenv("GE007_AUTO_ADD_WEAPON_AMMO_AMOUNT");

        if (frame_env != NULL && *frame_env != '\0') {
            s_autoWeaponAmmoFrame = (int)strtol(frame_env, NULL, 10);
        }

        if (item_env != NULL && *item_env != '\0') {
            s_autoWeaponAmmoItem = (int)strtol(item_env, NULL, 0);
        }

        if (amount_env != NULL && *amount_env != '\0') {
            s_autoWeaponAmmoAmount = (int)strtol(amount_env, NULL, 10);
        }
    }
}

static void pcMaybeApplyScriptedWeaponAmmo(int input_frame)
{
    WeaponStats *stats;
    int hand;

    pcInitScriptedWeaponAmmo();

    if (!g_deterministic || s_autoWeaponAmmoDone ||
        s_autoWeaponAmmoFrame < 0 || s_autoWeaponAmmoItem < 0 ||
        s_autoWeaponAmmoAmount < 0) {
        return;
    }

    if (input_frame != s_autoWeaponAmmoFrame) {
        return;
    }

    if (g_CurrentPlayer == NULL || g_playerPerm == NULL) {
        return;
    }

    add_ammo_to_weapon((ITEM_IDS)s_autoWeaponAmmoItem, s_autoWeaponAmmoAmount);

    stats = get_ptr_item_statistics((ITEM_IDS)s_autoWeaponAmmoItem);
    if (stats != NULL && stats->MagSize > 0) {
        for (hand = GUNRIGHT; hand <= GUNLEFT; hand++) {
            if (getCurrentPlayerWeaponId(hand) == s_autoWeaponAmmoItem &&
                g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine <= 0) {
                g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine =
                    (s_autoWeaponAmmoAmount < stats->MagSize)
                    ? s_autoWeaponAmmoAmount
                    : stats->MagSize;
            }
        }
    }

    s_autoWeaponAmmoDone = 1;
}

static void pcInitScriptedSetHandAmmo(void)
{
    const char *script_env;

    if (s_autoSetHandAmmoInitialized) {
        return;
    }

    s_autoSetHandAmmoInitialized = 1;
    memset(s_autoSetHandAmmoEvents, 0, sizeof(s_autoSetHandAmmoEvents));
    s_autoSetHandAmmoTrace = getenv("GE007_TRACE_SCRIPTED_AMMO") != NULL;

    script_env = getenv("GE007_AUTO_SET_HAND_AMMO_SCRIPT");

    if (script_env != NULL && *script_env != '\0' &&
        !(script_env[0] == '0' && script_env[1] == '\0')) {
        while (*script_env != '\0' && s_autoSetHandAmmoCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            char *endptr;
            long frame;
            long hand;
            long mag;
            long reserve;

            while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
                script_env++;
            }

            if (*script_env == '\0') {
                break;
            }

            frame = strtol(script_env, &endptr, 10);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            hand = strtol(script_env, &endptr, 10);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            mag = strtol(script_env, &endptr, 10);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            reserve = strtol(script_env, &endptr, 10);
            if (endptr == script_env) {
                break;
            }

            script_env = endptr;

            s_autoSetHandAmmoEvents[s_autoSetHandAmmoCount].frame = (int)frame;
            s_autoSetHandAmmoEvents[s_autoSetHandAmmoCount].hand = (int)hand;
            s_autoSetHandAmmoEvents[s_autoSetHandAmmoCount].mag = (int)mag;
            s_autoSetHandAmmoEvents[s_autoSetHandAmmoCount].reserve = (int)reserve;
            s_autoSetHandAmmoEvents[s_autoSetHandAmmoCount].applied = 0;
            s_autoSetHandAmmoCount++;
        }
    }

    if (s_autoSetHandAmmoCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_SET_HAND_AMMO_FRAME");
        const char *hand_env = getenv("GE007_AUTO_SET_HAND_AMMO_HAND");
        const char *mag_env = getenv("GE007_AUTO_SET_HAND_AMMO_MAG");
        const char *reserve_env = getenv("GE007_AUTO_SET_HAND_AMMO_RESERVE");

        if (frame_env != NULL && *frame_env != '\0') {
            s_autoSetHandAmmoFrame = (int)strtol(frame_env, NULL, 10);
        }

        if (hand_env != NULL && *hand_env != '\0') {
            s_autoSetHandAmmoHand = (int)strtol(hand_env, NULL, 10);
        }

        if (mag_env != NULL && *mag_env != '\0') {
            s_autoSetHandAmmoMag = (int)strtol(mag_env, NULL, 10);
        }

        if (reserve_env != NULL && *reserve_env != '\0') {
            s_autoSetHandAmmoReserve = (int)strtol(reserve_env, NULL, 10);
        }
    }
}

static void pcApplyScriptedSetHandAmmoEvent(int input_frame, int hand, int mag, int reserve)
{
    int ammo_type;

    g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine = mag;
    ammo_type = get_ammo_type_for_weapon(getCurrentPlayerWeaponId((GUNHAND)hand));

    if (ammo_type > 0 &&
        ammo_type < (int)(sizeof(g_CurrentPlayer->ammoheldarr) /
                          sizeof(g_CurrentPlayer->ammoheldarr[0]))) {
        g_CurrentPlayer->ammoheldarr[ammo_type] = reserve;
    }

    if (s_autoSetHandAmmoTrace) {
        fprintf(stderr,
                "[AUTO_SET_HAND_AMMO] frame=%d hand=%d weapon=%d ammo_type=%d mag=%d reserve=%d\n",
                input_frame,
                hand,
                getCurrentPlayerWeaponId((GUNHAND)hand),
                ammo_type,
                g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine,
                (ammo_type > 0 &&
                 ammo_type < (int)(sizeof(g_CurrentPlayer->ammoheldarr) /
                                   sizeof(g_CurrentPlayer->ammoheldarr[0])))
                    ? g_CurrentPlayer->ammoheldarr[ammo_type]
                    : -1);
    }
}

static void pcMaybeApplyScriptedSetHandAmmo(int input_frame)
{
    int i;

    pcInitScriptedSetHandAmmo();

    if (!g_deterministic || g_CurrentPlayer == NULL || g_playerPerm == NULL) {
        return;
    }

    if (s_autoSetHandAmmoCount > 0) {
        for (i = 0; i < s_autoSetHandAmmoCount; i++) {
            if (s_autoSetHandAmmoEvents[i].applied ||
                s_autoSetHandAmmoEvents[i].frame < 0 ||
                s_autoSetHandAmmoEvents[i].hand < 0 ||
                s_autoSetHandAmmoEvents[i].hand >= GUNHANDS ||
                s_autoSetHandAmmoEvents[i].mag < 0 ||
                s_autoSetHandAmmoEvents[i].reserve < 0 ||
                input_frame != s_autoSetHandAmmoEvents[i].frame) {
                continue;
            }

            pcApplyScriptedSetHandAmmoEvent(
                input_frame,
                s_autoSetHandAmmoEvents[i].hand,
                s_autoSetHandAmmoEvents[i].mag,
                s_autoSetHandAmmoEvents[i].reserve);
            s_autoSetHandAmmoEvents[i].applied = 1;
        }
        return;
    }

    if (s_autoSetHandAmmoDone ||
        s_autoSetHandAmmoFrame < 0 || s_autoSetHandAmmoHand < 0 ||
        s_autoSetHandAmmoHand >= GUNHANDS || s_autoSetHandAmmoMag < 0 ||
        s_autoSetHandAmmoReserve < 0 ||
        input_frame != s_autoSetHandAmmoFrame) {
        return;
    }

    pcApplyScriptedSetHandAmmoEvent(
        input_frame,
        s_autoSetHandAmmoHand,
        s_autoSetHandAmmoMag,
        s_autoSetHandAmmoReserve);
    s_autoSetHandAmmoDone = 1;
}

static void pcInitScriptedEquip(void)
{
    const char *script_env;

    if (s_autoEquipInitialized) {
        return;
    }

    s_autoEquipInitialized = 1;
    memset(s_autoEquipEvents, 0, sizeof(s_autoEquipEvents));

    script_env = getenv("GE007_AUTO_EQUIP_ITEM_SCRIPT");

    if (script_env != NULL && *script_env != '\0' &&
        !(script_env[0] == '0' && script_env[1] == '\0')) {
        while (*script_env != '\0' && s_autoEquipCount < PC_MAX_SCRIPTED_WARP_EVENTS) {
            char *endptr;
            long frame;
            long item;

            while (*script_env == ' ' || *script_env == '\t' || *script_env == ',') {
                script_env++;
            }

            if (*script_env == '\0') {
                break;
            }

            frame = strtol(script_env, &endptr, 10);
            if (endptr == script_env || *endptr != ':') {
                break;
            }

            script_env = endptr + 1;
            item = strtol(script_env, &endptr, 0);
            if (endptr == script_env) {
                break;
            }

            script_env = endptr;

            s_autoEquipEvents[s_autoEquipCount].frame = (int)frame;
            s_autoEquipEvents[s_autoEquipCount].item = (int)item;
            s_autoEquipEvents[s_autoEquipCount].applied = 0;
            s_autoEquipCount++;
        }
    }

    if (s_autoEquipCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_EQUIP_ITEM_FRAME");
        const char *item_env = getenv("GE007_AUTO_EQUIP_ITEM");

        if (frame_env != NULL && *frame_env != '\0') {
            s_autoEquipFrame = (int)strtol(frame_env, NULL, 10);
        }

        if (item_env != NULL && *item_env != '\0') {
            s_autoEquipItem = (int)strtol(item_env, NULL, 0);
        }
    }
}

static void pcMaybeApplyScriptedEquip(int input_frame)
{
    int i;

    pcInitScriptedEquip();

    if (!g_deterministic) {
        return;
    }

    if (s_autoEquipCount > 0) {
        for (i = 0; i < s_autoEquipCount; i++) {
            if (s_autoEquipEvents[i].applied ||
                input_frame != s_autoEquipEvents[i].frame ||
                s_autoEquipEvents[i].item < 0) {
                continue;
            }

            if (g_CurrentPlayer == NULL || g_playerPerm == NULL) {
                return;
            }

            currentPlayerEquipWeaponWrapper(GUNRIGHT, s_autoEquipEvents[i].item);
            currentPlayerEquipWeaponWrapper(GUNLEFT, ITEM_UNARMED);
            s_autoEquipEvents[i].applied = 1;
        }

        return;
    }

    if (s_autoEquipDone || s_autoEquipFrame < 0 || s_autoEquipItem < 0 ||
        input_frame != s_autoEquipFrame) {
        return;
    }

    if (g_CurrentPlayer == NULL || g_playerPerm == NULL) {
        return;
    }

    currentPlayerEquipWeaponWrapper(GUNRIGHT, s_autoEquipItem);
    currentPlayerEquipWeaponWrapper(GUNLEFT, ITEM_UNARMED);
    s_autoEquipDone = 1;
}

static void pcInitScriptedAddItem(void)
{
    if (s_autoAddItemInitialized) {
        return;
    }

    s_autoAddItemInitialized = 1;

    {
        const char *frame_env = getenv("GE007_AUTO_ADD_ITEM_FRAME");
        const char *item_env = getenv("GE007_AUTO_ADD_ITEM");

        if (frame_env != NULL && *frame_env != '\0') {
            s_autoAddItemFrame = (int)strtol(frame_env, NULL, 10);
        }

        if (item_env != NULL && *item_env != '\0') {
            s_autoAddItem = (int)strtol(item_env, NULL, 0);
        }
    }
}

static void pcMaybeApplyScriptedAddItem(int input_frame)
{
    pcInitScriptedAddItem();

    if (!g_deterministic || s_autoAddItemDone || s_autoAddItemFrame < 0 || s_autoAddItem < 0) {
        return;
    }

    if (input_frame != s_autoAddItemFrame) {
        return;
    }

    if (g_CurrentPlayer == NULL || g_playerPerm == NULL) {
        return;
    }

    if (bondinvAddInvItem((ITEM_IDS)s_autoAddItem) != 0) {
        s_autoAddItemDone = 1;
    }
}

static void pcParseScriptedSfxEvents(const char *script_env)
{
    if (script_env == NULL || *script_env == '\0' ||
        (script_env[0] == '0' && script_env[1] == '\0')) {
        return;
    }

    while (*script_env != '\0' && s_autoPlaySfxCount < PC_MAX_SCRIPTED_SFX_EVENTS) {
        char *endptr;
        long frame;
        long sound;

        while (*script_env == ',' || *script_env == ';' || *script_env == ' ') {
            script_env++;
        }
        if (*script_env == '\0') {
            break;
        }

        frame = strtol(script_env, &endptr, 0);
        if (endptr == script_env || *endptr != ':') {
            break;
        }
        script_env = endptr + 1;

        sound = strtol(script_env, &endptr, 0);
        if (endptr == script_env) {
            break;
        }
        script_env = endptr;

        if (frame >= 0 && sound > 0) {
            s_autoPlaySfxEvents[s_autoPlaySfxCount].frame = (int)frame;
            s_autoPlaySfxEvents[s_autoPlaySfxCount].sound = (int)sound;
            s_autoPlaySfxEvents[s_autoPlaySfxCount].applied = 0;
            s_autoPlaySfxCount++;
        }
    }
}

static void pcInitScriptedPlaySfx(void)
{
    if (s_autoPlaySfxInitialized) {
        return;
    }

    s_autoPlaySfxInitialized = 1;
    memset(s_autoPlaySfxEvents, 0, sizeof(s_autoPlaySfxEvents));
    s_autoPlaySfxCount = 0;

    pcParseScriptedSfxEvents(getenv("GE007_AUTO_PLAY_SFX_SCRIPT"));
    if (s_autoPlaySfxCount == 0) {
        const char *frame_env = getenv("GE007_AUTO_PLAY_SFX_FRAME");
        const char *sound_env = getenv("GE007_AUTO_PLAY_SFX");

        if (frame_env != NULL && *frame_env != '\0' &&
            sound_env != NULL && *sound_env != '\0') {
            s_autoPlaySfxEvents[0].frame = (int)strtol(frame_env, NULL, 0);
            s_autoPlaySfxEvents[0].sound = (int)strtol(sound_env, NULL, 0);
            s_autoPlaySfxEvents[0].applied = 0;
            s_autoPlaySfxCount = 1;
        }
    }
}

static int pcScriptedSfxAllowLive(void)
{
    static int initialized = 0;
    static int allow_live = 0;

    if (!initialized) {
        const char *env = getenv("GE007_AUTO_SFX_ALLOW_LIVE");

        initialized = 1;
        allow_live = (env != NULL && *env != '\0' && !(env[0] == '0' && env[1] == '\0'));
    }

    return allow_live;
}

static void pcMaybeApplyScriptedPlaySfx(int input_frame)
{
    int i;

    pcInitScriptedPlaySfx();

    if ((!g_deterministic && !pcScriptedSfxAllowLive()) ||
        s_autoPlaySfxCount == 0 ||
        g_musicSfxBufferPtr == NULL) {
        return;
    }

    for (i = 0; i < s_autoPlaySfxCount; i++) {
        if (s_autoPlaySfxEvents[i].applied ||
            s_autoPlaySfxEvents[i].frame != input_frame ||
            s_autoPlaySfxEvents[i].sound <= 0) {
            continue;
        }

        s_autoPlaySfxLastState =
            sndPlaySfx(g_musicSfxBufferPtr, (s16)s_autoPlaySfxEvents[i].sound, NULL);
        s_autoPlaySfxEvents[i].applied = 1;
    }
}

static void pcInitScriptedStopSfx(void)
{
    if (s_autoStopSfxInitialized) {
        return;
    }

    s_autoStopSfxInitialized = 1;

    {
        const char *frame_env = getenv("GE007_AUTO_STOP_SFX_FRAME");

        if (frame_env != NULL && *frame_env != '\0') {
            s_autoStopSfxFrame = (int)strtol(frame_env, NULL, 0);
        }
    }
}

static void pcMaybeApplyScriptedStopSfx(int input_frame)
{
    pcInitScriptedStopSfx();

    if ((!g_deterministic && !pcScriptedSfxAllowLive()) ||
        s_autoStopSfxDone ||
        s_autoStopSfxFrame != input_frame ||
        s_autoPlaySfxLastState == NULL) {
        return;
    }

    sndDeactivate(s_autoPlaySfxLastState);
    s_autoPlaySfxLastState = NULL;
    s_autoStopSfxDone = 1;
}

static void pcInitScriptedFxSfx(void)
{
    if (s_autoFxSfxInitialized) {
        return;
    }

    s_autoFxSfxInitialized = 1;

    {
        const char *frame_env = getenv("GE007_AUTO_FX_SFX_FRAME");
        const char *mix_env = getenv("GE007_AUTO_FX_SFX_MIX");

        if (frame_env != NULL && *frame_env != '\0') {
            s_autoFxSfxFrame = (int)strtol(frame_env, NULL, 0);
        }
        if (mix_env != NULL && *mix_env != '\0') {
            s_autoFxSfxMix = (int)strtol(mix_env, NULL, 0);
            if (s_autoFxSfxMix < 0) {
                s_autoFxSfxMix = 0;
            } else if (s_autoFxSfxMix > 127) {
                s_autoFxSfxMix = 127;
            }
        }
    }
}

static void pcMaybeApplyScriptedFxSfx(int input_frame)
{
    enum { PC_AL_SNDP_FX_EVT = (1 << 8) };

    pcInitScriptedFxSfx();

    if ((!g_deterministic && !pcScriptedSfxAllowLive()) ||
        s_autoFxSfxDone ||
        s_autoFxSfxFrame != input_frame ||
        s_autoPlaySfxLastState == NULL) {
        return;
    }

    sndCreatePostEvent(s_autoPlaySfxLastState, PC_AL_SNDP_FX_EVT, s_autoFxSfxMix);
    s_autoFxSfxDone = 1;
}

static void pcInitScriptedAddDualItem(void)
{
    if (s_autoAddDualInitialized) {
        return;
    }

    s_autoAddDualInitialized = 1;

    {
        const char *frame_env = getenv("GE007_AUTO_ADD_DUAL_FRAME");
        const char *right_env = getenv("GE007_AUTO_ADD_DUAL_RIGHT");
        const char *left_env = getenv("GE007_AUTO_ADD_DUAL_LEFT");

        if (frame_env != NULL && *frame_env != '\0') {
            s_autoAddDualFrame = (int)strtol(frame_env, NULL, 10);
        }

        if (right_env != NULL && *right_env != '\0') {
            s_autoAddDualRight = (int)strtol(right_env, NULL, 0);
        }

        if (left_env != NULL && *left_env != '\0') {
            s_autoAddDualLeft = (int)strtol(left_env, NULL, 0);
        }
    }
}

static void pcMaybeApplyScriptedAddDualItem(int input_frame)
{
    pcInitScriptedAddDualItem();

    if (!g_deterministic || s_autoAddDualDone || s_autoAddDualFrame < 0 ||
        s_autoAddDualRight < 0 || s_autoAddDualLeft < 0) {
        return;
    }

    if (input_frame != s_autoAddDualFrame) {
        return;
    }

    if (g_CurrentPlayer == NULL || g_playerPerm == NULL) {
        return;
    }

    if (bondinvAddDoublesInvItem((ITEM_IDS)s_autoAddDualRight,
                                 (ITEM_IDS)s_autoAddDualLeft) != 0) {
        s_autoAddDualDone = 1;
    }
}

static void pcInitScriptedEquipDual(void)
{
    if (s_autoEquipDualInitialized) {
        return;
    }

    s_autoEquipDualInitialized = 1;

    {
        const char *frame_env = getenv("GE007_AUTO_EQUIP_DUAL_FRAME");
        const char *right_env = getenv("GE007_AUTO_EQUIP_DUAL_RIGHT");
        const char *left_env = getenv("GE007_AUTO_EQUIP_DUAL_LEFT");

        if (frame_env != NULL && *frame_env != '\0') {
            s_autoEquipDualFrame = (int)strtol(frame_env, NULL, 10);
        }

        if (right_env != NULL && *right_env != '\0') {
            s_autoEquipDualRight = (int)strtol(right_env, NULL, 0);
        }

        if (left_env != NULL && *left_env != '\0') {
            s_autoEquipDualLeft = (int)strtol(left_env, NULL, 0);
        }
    }
}

static void pcMaybeApplyScriptedEquipDual(int input_frame)
{
    pcInitScriptedEquipDual();

    if (!g_deterministic || s_autoEquipDualDone || s_autoEquipDualFrame < 0 ||
        s_autoEquipDualRight < 0 || s_autoEquipDualLeft < 0) {
        return;
    }

    if (input_frame != s_autoEquipDualFrame) {
        return;
    }

    if (g_CurrentPlayer == NULL || g_playerPerm == NULL) {
        return;
    }

    currentPlayerEquipWeaponWrapper(GUNRIGHT, s_autoEquipDualRight);
    currentPlayerEquipWeaponWrapper(GUNLEFT, s_autoEquipDualLeft);
    s_autoEquipDualDone = 1;
}

static int pcParseMissionEndResult(const char *value)
{
    if (value == NULL || *value == '\0') {
        return PC_MISSION_END_SUCCESS;
    }

    if (strcmp(value, "success") == 0 || strcmp(value, "complete") == 0 ||
        strcmp(value, "completed") == 0) {
        return PC_MISSION_END_SUCCESS;
    }

    if (strcmp(value, "fail") == 0 || strcmp(value, "failed") == 0) {
        return PC_MISSION_END_FAIL;
    }

    if (strcmp(value, "abort") == 0 || strcmp(value, "aborted") == 0) {
        return PC_MISSION_END_ABORT;
    }

    if (strcmp(value, "kia") == 0 || strcmp(value, "dead") == 0) {
        return PC_MISSION_END_KIA;
    }

    return PC_MISSION_END_INVALID;
}

static void pcInitScriptedMissionEnd(void)
{
    if (s_autoMissionEndInitialized) {
        return;
    }

    s_autoMissionEndInitialized = 1;

    {
        const char *frame_env = getenv("GE007_AUTO_MISSION_END_FRAME");
        const char *result_env = getenv("GE007_AUTO_MISSION_END_RESULT");

        if (frame_env != NULL && *frame_env != '\0') {
            s_autoMissionEndFrame = (int)strtol(frame_env, NULL, 10);
        }

        s_autoMissionEndResult = pcParseMissionEndResult(result_env);
        if (s_autoMissionEndResult == PC_MISSION_END_INVALID) {
            fprintf(stderr,
                    "[GE007-PC] invalid GE007_AUTO_MISSION_END_RESULT='%s'; "
                    "mission-end automation disabled\n",
                    result_env != NULL ? result_env : "");
            s_autoMissionEndFrame = -1;
        }
    }
}

static void pcMaybeRestoreScriptedMissionEndState(void)
{
    extern MENU current_menu;
    extern s32 debug_all_obj_complete_flag;

    if (!s_autoMissionEndRestoreDebugFlag) {
        return;
    }

    if (bossGetStageNum() != LEVELID_TITLE ||
        lvlGetCurrentStageToLoad() != LEVELID_TITLE ||
        current_menu != MENU_MISSION_SELECT) {
        return;
    }

    debug_all_obj_complete_flag = s_autoMissionEndSavedDebugFlag;
    s_autoMissionEndRestoreDebugFlag = 0;
}

static void pcInitAutoExitOnTitle(void)
{
    const char *exit_env;
    const char *delay_env;

    if (s_autoExitOnTitleInitialized) {
        return;
    }

    s_autoExitOnTitleInitialized = 1;

    exit_env = getenv("GE007_AUTO_EXIT_ON_TITLE");
    s_autoExitOnTitle = (exit_env != NULL && exit_env[0] != '\0' && exit_env[0] != '0');

    delay_env = getenv("GE007_AUTO_EXIT_ON_TITLE_DELAY");
    if (delay_env != NULL && delay_env[0] != '\0') {
        s_autoExitOnTitleDelay = (int)strtol(delay_env, NULL, 10);
        if (s_autoExitOnTitleDelay < 0) {
            s_autoExitOnTitleDelay = 0;
        }
    }
}

static void pcMaybeApplyAutoExitOnTitle(int input_frame)
{
    pcInitAutoExitOnTitle();

    if (!g_deterministic || !s_autoExitOnTitle || s_autoExitOnTitleApplied) {
        return;
    }

    if (bossGetStageNum() != LEVELID_TITLE || lvlGetCurrentStageToLoad() != LEVELID_TITLE) {
        s_autoExitOnTitleSeenFrame = -1;
        return;
    }

    if (s_autoExitOnTitleSeenFrame < 0) {
        s_autoExitOnTitleSeenFrame = input_frame;
    }

    if (input_frame < s_autoExitOnTitleSeenFrame + s_autoExitOnTitleDelay) {
        return;
    }

    fprintf(stderr,
            "[GE007-PC] deterministic title return observed at frame %d; exiting\n",
            input_frame);
    s_autoExitOnTitleApplied = 1;
#ifdef PROJECT_TITAN_TGR
    TitanGeRuntime_Fail();
    return;
#else
    exit(0);
#endif
}

static void pcInitAutoExitFrame(void)
{
    const char *frame_env;

    if (s_autoExitFrameInitialized) {
        return;
    }

    s_autoExitFrameInitialized = 1;

    frame_env = getenv("GE007_AUTO_EXIT_FRAME");
    if (frame_env != NULL && frame_env[0] != '\0') {
        s_autoExitFrame = (int)strtol(frame_env, NULL, 10);
        if (s_autoExitFrame < 0) {
            s_autoExitFrame = -1;
        }
    }
}

static void pcMaybeApplyAutoExitFrame(int input_frame)
{
    pcInitAutoExitFrame();

    if (!g_deterministic || s_autoExitFrameApplied || s_autoExitFrame < 0) {
        return;
    }

    /*
     * Input hooks run before the frame's render/trace. Exit on the first tick
     * after the requested frame so trace-based validators can require that
     * GE007_AUTO_EXIT_FRAME itself was observed.
     */
    if (input_frame <= s_autoExitFrame) {
        return;
    }

    fprintf(stderr,
            "[GE007-PC] deterministic frame exit observed after frame %d; exiting\n",
            s_autoExitFrame);
    s_autoExitFrameApplied = 1;
#ifdef PROJECT_TITAN_TGR
    TitanGeRuntime_Fail();
    return;
#else
    exit(0);
#endif
}

static long pcParseLongToken(const char **cursor, int *ok)
{
    char *endptr;
    long value;

    if (cursor == NULL || *cursor == NULL || ok == NULL) {
        if (ok != NULL) {
            *ok = 0;
        }
        return 0;
    }

    while (**cursor == ' ' || **cursor == '\t') {
        (*cursor)++;
    }

    value = strtol(*cursor, &endptr, 10);
    if (endptr == *cursor) {
        *ok = 0;
        return 0;
    }

    *cursor = endptr;
    *ok = 1;
    return value;
}

static void pcInitAutoUnlockSolo(void)
{
    const char *spec;
    const char *folder_env;
    long folder;
    char *endptr;

    if (s_autoUnlockSoloInitialized) {
        return;
    }

    s_autoUnlockSoloInitialized = 1;
    s_autoUnlockSoloSpec[0] = '\0';

    spec = getenv("GE007_AUTO_UNLOCK_SOLO");
    if (spec == NULL || *spec == '\0') {
        return;
    }

    strncpy(s_autoUnlockSoloSpec, spec, sizeof(s_autoUnlockSoloSpec) - 1);
    s_autoUnlockSoloSpec[sizeof(s_autoUnlockSoloSpec) - 1] = '\0';

    folder_env = getenv("GE007_AUTO_UNLOCK_FOLDER");
    if (folder_env != NULL && *folder_env != '\0') {
        folder = strtol(folder_env, &endptr, 10);
        if (endptr != folder_env && folder >= FOLDER1 && folder < MAX_FOLDER_COUNT) {
            s_autoUnlockSoloFolder = (int)folder;
        }
    }
}

static int pcParseAutoUnlockEntry(const char **cursor,
                                  int *out_start_stage,
                                  int *out_end_stage,
                                  int *out_difficulty,
                                  int *out_time)
{
    int ok = 1;
    int start_stage;
    int end_stage;
    int difficulty = DIFFICULTY_00;
    int time = 99999999;

    if (cursor == NULL || *cursor == NULL ||
        out_start_stage == NULL || out_end_stage == NULL ||
        out_difficulty == NULL || out_time == NULL) {
        return 0;
    }

    while (**cursor == ' ' || **cursor == '\t' || **cursor == ',') {
        (*cursor)++;
    }

    if (**cursor == '\0') {
        return 0;
    }

    if (strncmp(*cursor, "all", 3) == 0) {
        *cursor += 3;
        start_stage = SP_LEVEL_DAM;
        end_stage = SP_LEVEL_EGYPT;
    } else {
        start_stage = (int)pcParseLongToken(cursor, &ok);
        if (!ok) {
            return 0;
        }

        end_stage = start_stage;
        if (**cursor == '-') {
            (*cursor)++;
            end_stage = (int)pcParseLongToken(cursor, &ok);
            if (!ok) {
                return 0;
            }
        }
    }

    if (**cursor == ':') {
        (*cursor)++;
        difficulty = (int)pcParseLongToken(cursor, &ok);
        if (!ok) {
            return 0;
        }

        if (**cursor == ':') {
            (*cursor)++;
            time = (int)pcParseLongToken(cursor, &ok);
            if (!ok) {
                return 0;
            }
        }
    }

    while (**cursor != '\0' && **cursor != ',') {
        (*cursor)++;
    }

    if (start_stage < SP_LEVEL_DAM) {
        start_stage = SP_LEVEL_DAM;
    }
    if (end_stage >= SP_LEVEL_MAX) {
        end_stage = SP_LEVEL_MAX - 1;
    }
    if (end_stage < start_stage ||
        difficulty < DIFFICULTY_AGENT ||
        difficulty >= DIFFICULTY_MAX) {
        return 0;
    }

    *out_start_stage = start_stage;
    *out_end_stage = end_stage;
    *out_difficulty = difficulty;
    *out_time = time;
    return 1;
}

static void pcMaybeApplyAutoUnlockSolo(void)
{
    extern MENU current_menu;
    save_data *save;
    const char *cursor;

    pcInitAutoUnlockSolo();

    if (!g_deterministic || s_autoUnlockSoloDone || s_autoUnlockSoloSpec[0] == '\0') {
        return;
    }

    if (current_menu < MENU_FILE_SELECT) {
        return;
    }

    save = fileGetSaveForFoldernum((u32)s_autoUnlockSoloFolder);
    if (save == NULL) {
        return;
    }

    cursor = s_autoUnlockSoloSpec;
    while (*cursor != '\0') {
        int start_stage;
        int end_stage;
        int difficulty;
        int time;
        int stage;
        int seeded_count = 0;
        int visible_count = 0;

        if (!pcParseAutoUnlockEntry(&cursor, &start_stage, &end_stage, &difficulty, &time)) {
            fprintf(stderr,
                    "[GE007-PC] invalid GE007_AUTO_UNLOCK_SOLO='%s'; "
                    "solo unlock seeding stopped\n",
                    s_autoUnlockSoloSpec);
            break;
        }

        for (stage = start_stage; stage <= end_stage; stage++) {
            int current_difficulty;

            for (current_difficulty = difficulty;
                 current_difficulty >= DIFFICULTY_AGENT;
                 current_difficulty--) {
                fileCheckSaveStageDifficultyTime(
                    save,
                    (LEVEL_SOLO_SEQUENCE)stage,
                    (DIFFICULTY)current_difficulty,
                    current_difficulty == difficulty ? time : 99999999);
            }
            seeded_count++;
            if (fileIsStageUnlockedAtDifficulty(
                    s_autoUnlockSoloFolder,
                    (LEVEL_SOLO_SEQUENCE)stage,
                    (DIFFICULTY)difficulty) != STAGESTATUS_LOCKED) {
                visible_count++;
            }
        }

        fileWriteSave(save);

        fprintf(stderr,
                "[GE007-PC] seeded %d solo unlock(s) folder=%d stages=%d-%d difficulty=%d visible=%d\n",
                seeded_count,
                s_autoUnlockSoloFolder,
                start_stage,
                end_stage,
                difficulty,
                visible_count);
    }

    s_autoUnlockSoloDone = 1;
}

static void pcMaybeApplyScriptedMissionEnd(int input_frame)
{
    extern s32 debug_all_obj_complete_flag;
    extern s32 mission_failed_or_aborted;
    extern s32 g_isBondKIA;

    pcInitScriptedMissionEnd();
    pcMaybeRestoreScriptedMissionEndState();
    pcMaybeApplyAutoUnlockSolo();
    pcMaybeApplyAutoExitOnTitle(input_frame);

    if (!g_deterministic || s_autoMissionEndApplied || s_autoMissionEndFrame < 0) {
        return;
    }

    if (input_frame < s_autoMissionEndFrame) {
        return;
    }

    if (bossGetStageNum() == LEVELID_TITLE || lvlGetCurrentStageToLoad() == LEVELID_TITLE) {
        return;
    }

    s_autoMissionEndSavedDebugFlag = debug_all_obj_complete_flag;
    s_autoMissionEndRestoreDebugFlag = 1;

    switch (s_autoMissionEndResult) {
        case PC_MISSION_END_ABORT:
            debug_all_obj_complete_flag = 0;
            mission_failed_or_aborted = TRUE;
            g_isBondKIA = FALSE;
            bossRunTitleStage();
            break;

        case PC_MISSION_END_KIA:
            debug_all_obj_complete_flag = 0;
            mission_failed_or_aborted = FALSE;
            g_isBondKIA = TRUE;
            bossRunTitleStage();
            break;

        case PC_MISSION_END_FAIL:
            debug_all_obj_complete_flag = 0;
            mission_failed_or_aborted = FALSE;
            g_isBondKIA = FALSE;
            bossReturnTitleStage();
            break;

        case PC_MISSION_END_SUCCESS:
        default:
            debug_all_obj_complete_flag = 1;
            mission_failed_or_aborted = FALSE;
            g_isBondKIA = FALSE;
            bossReturnTitleStage();
            break;
    }

    s_autoMissionEndApplied = 1;
}

static void pcParseScriptedInputPattern(PcScriptedInputPattern *pattern,
                                        const char *env_name)
{
    const char *env;

    if (pattern == NULL) {
        return;
    }

    memset(pattern, 0, sizeof(*pattern));
    pattern->initialized = 1;

    env = getenv(env_name);
    if (env == NULL || *env == '\0' ||
        (env[0] == '0' && env[1] == '\0')) {
        return;
    }

    while (*env != '\0' && pattern->count < PC_MAX_SCRIPTED_INPUT_WINDOWS) {
        char *endptr;
        long start_frame;
        long duration = 2;

        while (*env == ' ' || *env == '\t' || *env == ',') {
            env++;
        }

        if (*env == '\0') {
            break;
        }

        start_frame = strtol(env, &endptr, 10);
        if (endptr == env) {
            break;
        }

        env = endptr;

        if (*env == ':') {
            env++;
            duration = strtol(env, &endptr, 10);
            if (endptr != env) {
                env = endptr;
            }
        }

        if (duration < 1) {
            duration = 1;
        }

        pattern->windows[pattern->count].start_frame = (int)start_frame;
        pattern->windows[pattern->count].end_frame =
            (int)start_frame + (int)duration - 1;
        pattern->count++;

        while (*env != '\0' && *env != ',') {
            env++;
        }
    }
}

static int pcScriptedInputPatternActive(PcScriptedInputPattern *pattern,
                                        const char *env_name,
                                        int input_frame)
{
    int i;

    if (pattern == NULL) {
        return 0;
    }

    if (!pattern->initialized) {
        pcParseScriptedInputPattern(pattern, env_name);
    }

    for (i = 0; i < pattern->count; i++) {
        if (input_frame >= pattern->windows[i].start_frame &&
            input_frame <= pattern->windows[i].end_frame) {
            return 1;
        }
    }

    return 0;
}

static int pcNativeFrontendInputActive(void)
{
    extern MENU current_menu;
    extern s32 g_CurrentStageToLoad;

    if (current_menu <= MENU_INVALID || current_menu >= MENU_MAX) {
        return 0;
    }

    if (current_menu == MENU_RUN_STAGE) {
        return 0;
    }

    return g_CurrentStageToLoad == LEVELID_TITLE;
}

static void pcApplyMenuDirection(u16 *buttons, int *stick_x, int *stick_y,
                                 int up, int down, int left, int right)
{
    if (up) {
        *buttons |= U_JPAD | U_CBUTTONS;
        *stick_y += 80;
    }
    if (down) {
        *buttons |= D_JPAD | D_CBUTTONS;
        *stick_y -= 80;
    }
    if (left) {
        *buttons |= L_JPAD | L_CBUTTONS;
        *stick_x -= 80;
    }
    if (right) {
        *buttons |= R_JPAD | R_CBUTTONS;
        *stick_x += 80;
    }
}

static void pcApplyScriptedFrontendDirection(u16 *buttons, int *stick_x,
                                             int *stick_y, int input_frame)
{
    pcApplyMenuDirection(
        buttons,
        stick_x,
        stick_y,
        pcScriptedInputPatternActive(&s_autoFrontendUpPattern,
                                     "GE007_AUTO_FRONTEND_UP", input_frame),
        pcScriptedInputPatternActive(&s_autoFrontendDownPattern,
                                     "GE007_AUTO_FRONTEND_DOWN", input_frame),
        pcScriptedInputPatternActive(&s_autoFrontendLeftPattern,
                                     "GE007_AUTO_FRONTEND_LEFT", input_frame),
        pcScriptedInputPatternActive(&s_autoFrontendRightPattern,
                                     "GE007_AUTO_FRONTEND_RIGHT", input_frame));
}

/* Map the movement (left) stick to the N64 analog range (-80..80). Shares the
 * aim stick's radial deadzone + rescale-from-edge via platformApplyRadialDeadzone()
 * so slow-walk and diagonal creep are smooth instead of per-axis notchy (M2.1).
 * lx/ly are raw SDL axes (-32767..32767); ly is SDL-oriented (down = +) so we
 * invert for N64 (forward = +y). When Input.GamepadRadialDeadzone is 0 the legacy
 * per-axis square deadzone is used as an escape hatch (pre-M2.1 feel). */
static void pcMapMovementStick(int lx, int ly, int *out_x, int *out_y) {
    extern float g_pcGamepadDeadzone;
    extern int g_pcGamepadRadialDeadzone;
    /* Pure map factored into radial_deadzone.c so the runtime path and the
     * ROM-free unit test share it (FID-0015 / M2.1). */
    pcMapMovementStickN64(lx, ly, g_pcGamepadDeadzone, g_pcGamepadRadialDeadzone,
                          GAMEPAD_DEADZONE, out_x, out_y);
}

/* Map a single opened pad (slot k) to an N64 OSContPad. Used for players 2..4;
 * player 1 (slot 0) takes the richer inline path below that also merges
 * keyboard/mouse and the P1-only edge-triggered weapon/crouch state.
 *
 * Buttons here come from platformGetPadButtons(), a raw bitmask of
 * (1u << SDL_CONTROLLER_BUTTON_*). The N64 mapping mirrors the pad0 path so all
 * players share one mapping. Absent pads (handle == NULL) yield a zeroed pad. */
static void pcFillPadFromController(OSContPad *pad, int k) {
    unsigned int raw;
    int lx = 0, ly = 0;
    int lt = 0, rt = 0;
    u16 buttons = 0;
    int stick_x = 0, stick_y = 0;

    if (!pad) {
        return;
    }

    raw = platformGetPadButtons(k);

    /* Face buttons (mirror pad0 mapping in osContGetReadData). */
    if (raw & (1u << SDL_CONTROLLER_BUTTON_A))             buttons |= A_BUTTON;
    if (raw & (1u << SDL_CONTROLLER_BUTTON_B))             buttons |= B_BUTTON;
    if (raw & (1u << SDL_CONTROLLER_BUTTON_X))             buttons |= B_BUTTON; /* X = reload (B in GE) */
    if (raw & (1u << SDL_CONTROLLER_BUTTON_START))         buttons |= START_BUTTON;

    /* Bumpers */
    if (raw & (1u << SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  buttons |= L_TRIG;
    if (raw & (1u << SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) buttons |= Z_TRIG; /* RB = alt fire */

    /* D-pad */
    if (raw & (1u << SDL_CONTROLLER_BUTTON_DPAD_UP))       buttons |= U_JPAD;
    if (raw & (1u << SDL_CONTROLLER_BUTTON_DPAD_DOWN))     buttons |= D_JPAD;
    if (raw & (1u << SDL_CONTROLLER_BUTTON_DPAD_LEFT))     buttons |= L_JPAD;
    if (raw & (1u << SDL_CONTROLLER_BUTTON_DPAD_RIGHT))    buttons |= R_JPAD;

    /* Analog triggers → R_TRIG (aim) and Z_TRIG (fire). */
    platformGetPadTriggers(k, &lt, &rt);
    if (lt > GAMEPAD_DEADZONE) buttons |= R_TRIG; /* LT = aim mode */
    if (rt > GAMEPAD_DEADZONE) buttons |= Z_TRIG; /* RT = fire */

    /* Left stick → N64 analog stick (movement). Radial deadzone + rescale
     * shared with the aim stick (M2.1). */
    platformGetPadLeftStick(k, &lx, &ly);
    {
        int mvx, mvy;
        pcMapMovementStick(lx, ly, &mvx, &mvy);
        stick_x += mvx;
        stick_y += mvy;
    }

    if (stick_x > 80) stick_x = 80;
    if (stick_x < -80) stick_x = -80;
    if (stick_y > 80) stick_y = 80;
    if (stick_y < -80) stick_y = -80;

    pad->button  = buttons;
    pad->stick_x = (s8)stick_x;
    pad->stick_y = (s8)stick_y;
    pad->errnum  = 0;
}

/* ===== Overlay open/close latch-discharge (review F1+F4) =====
 * While the F1/Back overlay is open osContGetReadData returns neutral pads, so
 * the sim's previous-button state is 0. The physical A/B that closes the overlay
 * is still held on the next poll, and gamepadBindingActive reads live state, so
 * it would fire as a fresh gameplay edge (reload/interact/weapon-switch). Mirror
 * the ui_bindings.cpp:90 waitRelease pattern: on the open/close transition
 * discharge the same-batch Esc/crouch/wheel latches and the P1 weapon/crouch edge
 * trackers, and on close hold neutral input until every close-capable button is
 * released. No-op in automation (no overlay hooks -> wants_input == 0 always, so
 * the transition never fires) and byte-identity is preserved. */
static int g_pcPrevWeaponNext = 0;
static int g_pcPrevWeaponPrev = 0;
static int g_pcPrevCrouch = 0;

static int pcAnyOverlayCloseButtonHeld(void) {
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    /* Keyboard analogues of A (Resume) / B (back). F1 toggles the overlay but is
     * not a gameplay button, so it does not need to gate the release latch. */
    if (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_BACKSPACE])
        return 1;
    if (g_gameController) {
        int b;
        for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
            if (SDL_GameControllerGetButton(g_gameController, (SDL_GameControllerButton)b))
                return 1;
        if (SDL_GameControllerGetAxis(g_gameController, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000)
            return 1;
        if (SDL_GameControllerGetAxis(g_gameController, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000)
            return 1;
    }
    return 0;
}

s32 osContGetReadData(OSContPad *data) {
#ifdef PROJECT_TITAN_TGR
    extern int platformTitanFillN64Pads(OSContPad *pads,int capacity);
    if (!data) return 0;
    sndStubTick();
    return platformTitanFillN64Pads(data,MAXCONTROLLERS);
#else
    extern int g_frame_count_diag;
    int input_frame;
    u16 scripted_buttons = 0;
    int scripted_stick_x = 0;
    int scripted_stick_y = 0;
    int scripted_mouse_dx = 0;
    int scripted_mouse_dy = 0;
    int scripted_look_step = pcGetScriptedLookStep();
    int scripted_frontend_input;

    if (!data) return 0;
    memset(data, 0, sizeof(OSContPad) * MAXCONTROLLERS);

    /* Input gathered here affects the upcoming frame, not the one that was
     * just rendered. Advance by one so scripted windows line up with captures. */
    input_frame = g_frame_count_diag + 1;
    scripted_frontend_input = pcNativeFrontendInputActive();
    sndStubTick();
    pcMaybeApplyScriptedRngSeed(input_frame);
    pcMaybeApplyScriptedAutoAim(input_frame);
    pcMaybeApplyScriptedWarp(input_frame);
    pcMaybeApplyScriptedWarpChr(input_frame);
    pcMaybeApplyScriptedGuardSpawn(input_frame);
    pcMaybeApplyScriptedSetChrAI(input_frame);
    pcMaybeApplyScriptedSetChrFlag(input_frame);
    pcMaybeApplyScriptedChrToPad(input_frame);
    pcMaybeApplyScriptedForceChr(input_frame);
    pcMaybeApplyScriptedDamageChr(input_frame);
    pcMaybeApplyScriptedCollectTag(input_frame);
    pcMaybeApplyScriptedDamageTag(input_frame);
    pcMaybeApplyScriptedSetStageFlags(input_frame);
    pcMaybeApplyScriptedUnsetStageFlags(input_frame);
    pcMaybeApplyScriptedAttackRollChr(input_frame);
    pcMaybeApplyScriptedAIMoveChr(input_frame);
    pcMaybeApplyScriptedAIReactChr(input_frame);
    pcMaybeApplyScriptedFaceChr(input_frame);
    pcMaybeApplyScriptedFaceCoord(input_frame);
    pcMaybeApplyScriptedForcePlayer(input_frame);
    pcMaybeApplyScriptedTankFaceCoord(input_frame);
    pcMaybeApplyScriptedArmor(input_frame);
    pcMaybeApplyScriptedMpMenu(input_frame);
    pcMaybeApplyScriptedDamageBond(input_frame);
    pcMaybeApplyScriptedCameraMode(input_frame);
    pcMaybeApplyScriptedWeaponAmmo(input_frame);
    pcMaybeApplyScriptedSetHandAmmo(input_frame);
    pcMaybeApplyScriptedAddItem(input_frame);
    pcMaybeApplyScriptedPlaySfx(input_frame);
    pcMaybeApplyScriptedFxSfx(input_frame);
    pcMaybeApplyScriptedStopSfx(input_frame);
    pcMaybeApplyScriptedAddDualItem(input_frame);
    pcMaybeApplyScriptedEquip(input_frame);
    pcMaybeApplyScriptedEquipDual(input_frame);
    pcMaybeApplyScriptedMissionEnd(input_frame);
    pcMaybeApplyAutoExitFrame(input_frame);
    if (pcScriptedInputPatternActive(&s_autoReloadPattern,
                                     "GE007_AUTO_RELOAD", input_frame)) {
        attempt_reload_item_in_hand(GUNRIGHT);
        attempt_reload_item_in_hand(GUNLEFT);
    }
    if (pcScriptedInputPatternActive(&s_autoFirePattern,
                                     "GE007_AUTO_FIRE", input_frame)) {
        scripted_buttons |= Z_TRIG;
    }
    if (pcScriptedInputPatternActive(&s_autoAimPattern,
                                     "GE007_AUTO_AIM", input_frame)) {
        scripted_buttons |= R_TRIG;
    }
    if (pcScriptedInputPatternActive(&s_autoAPattern,
                                     "GE007_AUTO_A", input_frame)) {
        scripted_buttons |= A_BUTTON;
    }
    if (pcScriptedInputPatternActive(&s_autoBPattern,
                                     "GE007_AUTO_B", input_frame)) {
        scripted_buttons |= B_BUTTON;
    }
    if (pcScriptedInputPatternActive(&s_autoStartPattern,
                                     "GE007_AUTO_START", input_frame)) {
        scripted_buttons |= START_BUTTON;
    }
    if (pcScriptedInputPatternActive(&s_autoLPattern,
                                     "GE007_AUTO_L", input_frame)) {
        scripted_buttons |= L_TRIG;
    }
    if (pcScriptedInputPatternActive(&s_autoRPattern,
                                     "GE007_AUTO_R", input_frame)) {
        scripted_buttons |= R_TRIG;
    }
    if (pcScriptedInputPatternActive(&s_autoCUpPattern,
                                     "GE007_AUTO_CUP", input_frame)) {
        scripted_buttons |= U_CBUTTONS;
    }
    if (pcScriptedInputPatternActive(&s_autoCDownPattern,
                                     "GE007_AUTO_CDOWN", input_frame)) {
        scripted_buttons |= D_CBUTTONS;
    }
    if (pcScriptedInputPatternActive(&s_autoCLeftPattern,
                                     "GE007_AUTO_CLEFT", input_frame)) {
        scripted_buttons |= L_CBUTTONS;
    }
    if (pcScriptedInputPatternActive(&s_autoCRightPattern,
                                     "GE007_AUTO_CRIGHT", input_frame)) {
        scripted_buttons |= R_CBUTTONS;
    }
    {
        int auto_crouch_active =
            pcScriptedInputPatternActive(&s_autoCrouchPattern,
                                         "GE007_AUTO_CROUCH", input_frame);

        if (!scripted_frontend_input && auto_crouch_active && !s_autoCrouchPrevActive) {
            g_pcCrouchRequest = 1;
        }

        s_autoCrouchPrevActive = auto_crouch_active;
    }
    if (pcScriptedInputPatternActive(&s_autoDPadUpPattern,
                                     "GE007_AUTO_DPAD_UP", input_frame)) {
        scripted_buttons |= U_JPAD;
    }
    if (pcScriptedInputPatternActive(&s_autoDPadDownPattern,
                                     "GE007_AUTO_DPAD_DOWN", input_frame)) {
        scripted_buttons |= D_JPAD;
    }
    if (pcScriptedInputPatternActive(&s_autoDPadLeftPattern,
                                     "GE007_AUTO_DPAD_LEFT", input_frame)) {
        scripted_buttons |= L_JPAD;
    }
    if (pcScriptedInputPatternActive(&s_autoDPadRightPattern,
                                     "GE007_AUTO_DPAD_RIGHT", input_frame)) {
        scripted_buttons |= R_JPAD;
    }
    if (scripted_frontend_input) {
        pcApplyMenuDirection(
            &scripted_buttons,
            &scripted_stick_x,
            &scripted_stick_y,
            pcScriptedInputPatternActive(&s_autoMenuUpPattern,
                                         "GE007_AUTO_MENU_UP", input_frame),
            pcScriptedInputPatternActive(&s_autoMenuDownPattern,
                                         "GE007_AUTO_MENU_DOWN", input_frame),
            pcScriptedInputPatternActive(&s_autoMenuLeftPattern,
                                         "GE007_AUTO_MENU_LEFT", input_frame),
            pcScriptedInputPatternActive(&s_autoMenuRightPattern,
                                         "GE007_AUTO_MENU_RIGHT", input_frame));
        pcApplyScriptedFrontendDirection(&scripted_buttons,
                                         &scripted_stick_x,
                                         &scripted_stick_y,
                                         input_frame);
    }
    if (pcScriptedInputPatternActive(&s_autoForwardPattern,
                                     "GE007_AUTO_FORWARD", input_frame)) {
        scripted_stick_y += 80;
    }
    if (pcScriptedInputPatternActive(&s_autoBackPattern,
                                     "GE007_AUTO_BACK", input_frame)) {
        scripted_stick_y -= 80;
    }
    if (pcScriptedInputPatternActive(&s_autoLeftPattern,
                                     "GE007_AUTO_LEFT", input_frame)) {
        scripted_stick_x -= 80;
    }
    if (pcScriptedInputPatternActive(&s_autoRightPattern,
                                     "GE007_AUTO_RIGHT", input_frame)) {
        scripted_stick_x += 80;
    }
    if (pcScriptedInputPatternActive(&s_autoLookLeftPattern,
                                     "GE007_AUTO_LOOK_LEFT", input_frame)) {
        scripted_mouse_dx -= scripted_look_step;
    }
    if (pcScriptedInputPatternActive(&s_autoLookRightPattern,
                                     "GE007_AUTO_LOOK_RIGHT", input_frame)) {
        scripted_mouse_dx += scripted_look_step;
    }
    if (pcScriptedInputPatternActive(&s_autoLookUpPattern,
                                     "GE007_AUTO_LOOK_UP", input_frame)) {
        scripted_mouse_dy -= scripted_look_step;
    }
    if (pcScriptedInputPatternActive(&s_autoLookDownPattern,
                                     "GE007_AUTO_LOOK_DOWN", input_frame)) {
        scripted_mouse_dy += scripted_look_step;
    }
    if (pcScriptedInputPatternActive(&s_autoWeaponNextPattern,
                                     "GE007_AUTO_WEAPON_NEXT", input_frame)) {
        pcQueueWeaponCycleSteps(&g_pcWeaponCycleForward, 1);
    }
    if (pcScriptedInputPatternActive(&s_autoWeaponPrevPattern,
                                     "GE007_AUTO_WEAPON_PREV", input_frame)) {
        pcQueueWeaponCycleSteps(&g_pcWeaponCycleBack, 1);
    }

    g_pcScriptedMouseDeltaX = scripted_mouse_dx;
    g_pcScriptedMouseDeltaY = scripted_mouse_dy;

    if (scripted_stick_x > 80) scripted_stick_x = 80;
    if (scripted_stick_x < -80) scripted_stick_x = -80;
    if (scripted_stick_y > 80) scripted_stick_y = 80;
    if (scripted_stick_y < -80) scripted_stick_y = -80;

    /* Freeze all input for deterministic screenshots */
    extern int g_freezeInput;
    if (g_freezeInput) {
        data[0].button = scripted_buttons;
        data[0].stick_x = (s8)scripted_stick_x;
        data[0].stick_y = (s8)scripted_stick_y;
        return 0;
    }

    /* When fly camera is active, don't feed input to N64 controller.
     * This prevents gameplay/menu input from leaking through while
     * navigating with the debug fly camera. */
    if (g_pcDebugFlyCamera) {
        return 0;
    }

    /* In-game overlay gate (MC.1): when the F1/Start overlay is open it owns the
     * pad + keyboard for ImGui nav. Feed the game neutral input so nav does not
     * also drive gameplay — the polled-input analogue of the event-swallow in
     * platformPollEvents(). No-op in automation (no overlay hooks registered ->
     * wants_input == 0), so byte-identity and scripted input are unaffected;
     * scripted side-effects above have already run. All pads were zeroed by the
     * memset at function entry. */
    {
        extern int platformOverlayWantsInput(void);
        static int s_overlayWasOpen = 0;
        static int s_overlayReleaseLatch = 0;
        int overlay_now = platformOverlayWantsInput();

        if (overlay_now != s_overlayWasOpen) {
            /* Overlay open/close transition (F4): discharge any latch armed by an
             * event in the same SDL batch as the toggle press so it can't leak as
             * a gameplay edge on the first post-close poll. */
            g_pcEscapePressed = 0;
            g_pcCrouchToggle = 0;
            (void)platformGetMouseWheel();  /* drain same-batch wheel accumulator */
            g_pcPrevWeaponNext = g_pcPrevWeaponPrev = g_pcPrevCrouch = 0;
            if (!overlay_now) {
                s_overlayReleaseLatch = 1;  /* just closed: arm wait-for-release */
            }
            s_overlayWasOpen = overlay_now;
        }

        if (overlay_now) {
            return 0;
        }

        if (s_overlayReleaseLatch) {
            /* F1: hold neutral input until the closing button(s) are all released,
             * so the held A/B never reaches the sim as a fresh edge. */
            if (pcAnyOverlayCloseButtonHeld()) {
                return 0;
            }
            s_overlayReleaseLatch = 0;
        }
    }

#ifdef MACOS_APP_BUNDLE
    /* When running as a macOS app bundle, input arrives from the Swift app
     * shell via game_set_input() → gameBridgeConsumeInput(). The bridge
     * provides fully mapped N64 controller state — no SDL key scanning needed.
     * SDL events still run for window management but input is authoritative
     * from the bridge. */
    {
        typedef struct {
            u16 buttons; s8 stick_x; s8 stick_y;
            float mouse_dx; float mouse_dy;
            s32 mouse_wheel;
            float right_stick_x; float right_stick_y;
        } GameInputState;
        extern void gameBridgeConsumeInput(GameInputState *out);
        GameInputState bridge_input;
        int frontend_input = pcNativeFrontendInputActive();
        memset(&bridge_input, 0, sizeof(bridge_input));
        gameBridgeConsumeInput(&bridge_input);
        data[0].button = bridge_input.buttons | scripted_buttons;
        {
            int combined_x = (int)bridge_input.stick_x + scripted_stick_x;
            int combined_y = (int)bridge_input.stick_y + scripted_stick_y;
            if (combined_x > 80) combined_x = 80;
            if (combined_x < -80) combined_x = -80;
            if (combined_y > 80) combined_y = 80;
            if (combined_y < -80) combined_y = -80;
            data[0].stick_x = (s8)combined_x;
            data[0].stick_y = (s8)combined_y;
        }
        /* Mouse deltas from bridge are injected into the scripted mouse
         * delta accumulators. platformGetMouseDelta() already adds these
         * to the SDL deltas, so the existing mouse-look code picks them up. */
        if (!frontend_input) {
            g_pcScriptedMouseDeltaX += (int)bridge_input.mouse_dx;
            g_pcScriptedMouseDeltaY += (int)bridge_input.mouse_dy;
            if (bridge_input.mouse_wheel > 0) {
                pcQueueWeaponCycleSteps(&g_pcWeaponCycleForward, (int)bridge_input.mouse_wheel);
            } else if (bridge_input.mouse_wheel < 0) {
                pcQueueWeaponCycleSteps(&g_pcWeaponCycleBack, (int)-bridge_input.mouse_wheel);
            }
        }
        g_pcBridgeRightStickX = frontend_input ? 0 : (int)(bridge_input.right_stick_x * 32767.0f);
        g_pcBridgeRightStickY = frontend_input ? 0 : (int)(-bridge_input.right_stick_y * 32767.0f);
        if (g_pcBridgeRightStickX > 32767) g_pcBridgeRightStickX = 32767;
        if (g_pcBridgeRightStickX < -32767) g_pcBridgeRightStickX = -32767;
        if (g_pcBridgeRightStickY > 32767) g_pcBridgeRightStickY = 32767;
        if (g_pcBridgeRightStickY < -32767) g_pcBridgeRightStickY = -32767;
        return 0;
    }
#endif /* MACOS_APP_BUNDLE */

    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    u16 buttons = scripted_buttons;
    int stick_x = scripted_stick_x, stick_y = scripted_stick_y;
    int frontend_input = pcNativeFrontendInputActive();

    if (frontend_input) {
        if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE]) {
            buttons |= A_BUTTON;
        }
        if (keys[SDL_SCANCODE_TAB]) {
            buttons |= START_BUTTON;
        }
        if (keys[SDL_SCANCODE_BACKSPACE]) {
            buttons |= B_BUTTON;
        }

        pcApplyMenuDirection(
            &buttons,
            &stick_x,
            &stick_y,
            keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W],
            keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S],
            keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A],
            keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]);
        if (keys[SDL_SCANCODE_I]) buttons |= U_JPAD;
        if (keys[SDL_SCANCODE_K]) buttons |= D_JPAD;
        if (keys[SDL_SCANCODE_J]) buttons |= L_JPAD;
        if (keys[SDL_SCANCODE_L]) buttons |= R_JPAD;
    } else {
        /* ===== Keyboard → N64 button mapping (Modern PC FPS layout) =====
         *   WASD   = move (analog stick)
         *   Mouse  = look (direct injection in lvl.c)
         *   LClick = fire (Z_TRIG)        RClick = aim (R_TRIG)
         *   Scroll = weapon cycle          R/F    = reload/interact (B_BUTTON)
         *   Q/E    = lean left/right (C-buttons, active in aim mode)
         *   C/Ctrl = crouch toggle         Esc    = pause
         */

        if (keys[SDL_SCANCODE_RETURN])  buttons |= A_BUTTON;
        if (keys[SDL_SCANCODE_TAB])     buttons |= START_BUTTON;

        /* Rebindable actions read the binding registry (defaults == the keys
         * below); F/Backspace and the Left/Right-arrow lean stay as fixed
         * alternates. Under --deterministic the registry forces defaults, so
         * scripted input is unaffected. */
        if (keys[inputBindingScancode(IB_RELOAD)] || keys[SDL_SCANCODE_F] || keys[SDL_SCANCODE_BACKSPACE])
            buttons |= B_BUTTON;

        if (keys[inputBindingScancode(IB_FIRE)])  buttons |= Z_TRIG;
        if (keys[inputBindingScancode(IB_AIM)])   buttons |= R_TRIG;
        if (keys[SDL_SCANCODE_LALT])    buttons |= L_TRIG;

        if (keys[inputBindingScancode(IB_LEAN_L)] || keys[SDL_SCANCODE_LEFT])   buttons |= L_CBUTTONS;
        if (keys[inputBindingScancode(IB_LEAN_R)] || keys[SDL_SCANCODE_RIGHT])  buttons |= R_CBUTTONS;
        if (keys[SDL_SCANCODE_UP])      buttons |= U_CBUTTONS;
        if (keys[SDL_SCANCODE_DOWN])    buttons |= D_CBUTTONS;

        if (keys[SDL_SCANCODE_I])       buttons |= U_JPAD;
        if (keys[SDL_SCANCODE_K])       buttons |= D_JPAD;
        if (keys[SDL_SCANCODE_J])       buttons |= L_JPAD;
        if (keys[SDL_SCANCODE_L])       buttons |= R_JPAD;

        if (keys[inputBindingScancode(IB_FORWARD)]) stick_y += 80;
        if (keys[inputBindingScancode(IB_BACK)])    stick_y -= 80;
        if (keys[inputBindingScancode(IB_LEFT)])    stick_x -= 80;
        if (keys[inputBindingScancode(IB_RIGHT)])   stick_x += 80;

        /* WEB-017: a pointer-lock recapture click spans 4-9 input polls, but the
         * old g_pcMouseRegrabFrame flag suppressed exactly ONE — so the button,
         * still physically held, discharged the weapon on the very next poll
         * (alerting guards / wasting ammo on every recapture). Arm a
         * wait-for-release latch on the regrab signal (mirrors the
         * s_overlayReleaseLatch pattern above) and suppress the whole
         * mouse->fire/aim/interact mapping until EVERY mouse button is observed
         * up. */
        static int s_mouseRegrabReleaseLatch = 0;
        Uint32 mouse = SDL_GetMouseState(NULL, NULL);
        const Uint32 anyMouseButton = SDL_BUTTON(SDL_BUTTON_LEFT) |
                                      SDL_BUTTON(SDL_BUTTON_RIGHT) |
                                      SDL_BUTTON(SDL_BUTTON_MIDDLE);
        if (g_pcMouseRegrabFrame) {
            s_mouseRegrabReleaseLatch = 1;
        }
        if (s_mouseRegrabReleaseLatch) {
            if (mouse & anyMouseButton) {
                mouse = 0;                      /* still held from the recapture */
            } else {
                s_mouseRegrabReleaseLatch = 0;  /* all released: resume mapping */
            }
        }
        if (mouse & SDL_BUTTON(SDL_BUTTON_LEFT))   buttons |= Z_TRIG;
        if (mouse & SDL_BUTTON(SDL_BUTTON_RIGHT))  buttons |= R_TRIG;
        if (mouse & SDL_BUTTON(SDL_BUTTON_MIDDLE)) buttons |= B_BUTTON;
    }
    g_pcMouseRegrabFrame = 0;

    /* Escape: pause (START) when in gameplay, back (B) when in menus.
     * platform_sdl.c sets 1 = was-grabbed (definitely gameplay) or 2 = ungrabbed.
     * WEB-042: value 2 used to hardcode B via the g_mouseGrabbed proxy, which is
     * wrong for keyboard-only players (never grabbed => Esc could never pause a
     * mission) and after browser lock churn. Decide from REAL game state instead:
     * B backs out of the title/frontend menus, START pauses (opens the watch)
     * during a mission. The grabbed path (1) is unchanged. */
    if (g_pcEscapePressed == 1) {
        buttons |= START_BUTTON;
        g_pcEscapePressed = 0;
    } else if (g_pcEscapePressed == 2) {
        buttons |= frontend_input ? B_BUTTON : START_BUTTON;
        g_pcEscapePressed = 0;
    }

    if (frontend_input) {
        (void)platformGetMouseWheel();
        g_pcCrouchToggle = 0;
    } else {
        int wheel = platformGetMouseWheel();
        if (wheel > 0) pcQueueWeaponCycleSteps(&g_pcWeaponCycleForward, wheel);
        else if (wheel < 0) pcQueueWeaponCycleSteps(&g_pcWeaponCycleBack, -wheel);
    }

    if (!frontend_input && g_pcCrouchToggle) {
        g_pcCrouchRequest = 1;
        g_pcCrouchToggle = 0;
    }

    /* ===== Gamepad input (OR'd with keyboard/mouse) =====
     * P1 discrete buttons/triggers route through the rebindable gamepad table
     * (MC.3). Sticks stay hardcoded: movement keeps the shared radial-deadzone
     * mapping (pcMapMovementStick, M2.1), look stays the lvl.c right-stick
     * injection. Defaults reproduce the previous fixed map, except weapon-prev:
     * its old Back button is now the app-overlay toggle (MC.1), so it moved to
     * the R-stick click. Start stays N64 Start (watch). */
    if (g_gameController) {
        void *gc = g_gameController;

        if (gamepadBindingActive(gc, GB_JUMP))     buttons |= A_BUTTON;
        if (gamepadBindingActive(gc, GB_RELOAD))   buttons |= B_BUTTON;
        /* X stays a fixed alternate for reload (mirrors keyboard F/Backspace). */
        if (SDL_GameControllerGetButton(g_gameController, SDL_CONTROLLER_BUTTON_X))
            buttons |= B_BUTTON;
        if (gamepadBindingActive(gc, GB_PAUSE))    buttons |= START_BUTTON;

        if (gamepadBindingActive(gc, GB_LOOK))     buttons |= L_TRIG;
        if (gamepadBindingActive(gc, GB_ALT_FIRE)) buttons |= Z_TRIG;
        if (gamepadBindingActive(gc, GB_AIM))      buttons |= R_TRIG;
        if (gamepadBindingActive(gc, GB_FIRE))     buttons |= Z_TRIG;

        if (gamepadBindingActive(gc, GB_LOOK_UP))    buttons |= U_JPAD;
        if (gamepadBindingActive(gc, GB_LOOK_DOWN))  buttons |= D_JPAD;
        if (gamepadBindingActive(gc, GB_LOOK_LEFT))  buttons |= L_JPAD;
        if (gamepadBindingActive(gc, GB_LOOK_RIGHT)) buttons |= R_JPAD;

        if (!frontend_input) {
            /* Edge trackers live at file scope so the overlay open/close
             * transition can reset them (F4): a button held across an overlay
             * close must not synthesize a spurious weapon/crouch edge. */
            int cur_next   = gamepadBindingActive(gc, GB_WEAPON_NEXT);
            int cur_prev   = gamepadBindingActive(gc, GB_WEAPON_PREV);
            int cur_crouch = gamepadBindingActive(gc, GB_CROUCH);
            /* Weapon cycling queues steps (T6) so pad edges share the wheel's
             * clamped multi-step queue instead of clobbering it. */
            if (cur_next && !g_pcPrevWeaponNext)     pcQueueWeaponCycleSteps(&g_pcWeaponCycleForward, 1);
            if (cur_prev && !g_pcPrevWeaponPrev)     pcQueueWeaponCycleSteps(&g_pcWeaponCycleBack, 1);
            if (cur_crouch && !g_pcPrevCrouch) g_pcCrouchRequest = 1;
            g_pcPrevWeaponNext = cur_next; g_pcPrevWeaponPrev = cur_prev; g_pcPrevCrouch = cur_crouch;
        }

        /* Left stick → N64 analog stick (movement). Radial deadzone + rescale
         * shared with the aim stick (M2.1). */
        {
            int lx = SDL_GameControllerGetAxis(g_gameController, SDL_CONTROLLER_AXIS_LEFTX);
            int ly = SDL_GameControllerGetAxis(g_gameController, SDL_CONTROLLER_AXIS_LEFTY);
            int mvx, mvy;
            pcMapMovementStick(lx, ly, &mvx, &mvy);
            stick_x += mvx;
            stick_y += mvy;
        }

        /* Right stick → direct angle injection in lvl.c (not C-buttons).
         * This bypasses C-button acceleration for smoother gamepad aiming. */
    }

    /* Clamp combined stick values */
    if (stick_x > 80) stick_x = 80;
    if (stick_x < -80) stick_x = -80;
    if (stick_y > 80) stick_y = 80;
    if (stick_y < -80) stick_y = -80;

    data[0].button = buttons;
    data[0].stick_x = (s8)stick_x;
    data[0].stick_y = (s8)stick_y;
    data[0].errnum = 0;

    /* Players 2..4: pad slot k drives data[k] directly (no keyboard/mouse
     * merge — those belong to P1 only). Absent pads were already zeroed by the
     * memset above; pcFillPadFromController() leaves them neutral. */
    {
        int k;
        for (k = 1; k < MAXCONTROLLERS; k++) {
            pcFillPadFromController(&data[k], k);
        }
    }

    /* Input logging removed — verified working */

    return 0;
#endif
}
void osContSetCh(u8 ch) { (void)ch; }

/* ===== Rumble Pak stubs ===== */

s32 osMotorInit(OSMesgQueue *mq, OSPfs *pfs, s32 channel) {
    (void)mq; (void)pfs; (void)channel; return 0;
}
s32 osMotorStart(OSPfs *pfs) { (void)pfs; return 0; }
s32 osMotorStop(OSPfs *pfs) { (void)pfs; return 0; }

/* NOTE: <errno.h>/<unistd.h> are included HERE, not at file top. Historically
 * this was load-bearing: libultra's OSContStatus/OSContPad had a field
 * literally named `errno` (see data[i].errno / pad->errno throughout this
 * file), and including <errno.h> at file scope expands `errno` to the libc
 * macro, breaking every such field access (4 compile errors). That field has
 * since been renamed to `errnum` (platform_os.h) precisely to kill this class
 * of collision -- it was also silently fatal on MinGW, where errno is a
 * macro too. The scoped include is kept here anyway (harmless, avoids
 * unrelated churn) rather than hoisted back to file top.
 * Everything below this point (the EEPROM writer) needs errno/fsync.
 * (Deviation from audit §1.1, which put these at file top; the fix is otherwise
 * verbatim.) */
#ifndef PROJECT_TITAN_TGR
#include <errno.h>
#ifdef _WIN32
#define NOMINMAX      /* keep windows.h from defining min()/max() macros */
#include <windows.h>
#include <io.h>       /* _commit, _fileno */
/* windows.h (minwindef.h) defines the legacy empty macros `near`/`far`. Those
 * would mangle guPerspectiveF()/guPerspective() defined below in this TU (they
 * take `near`/`far` parameters). Undo the pollution — this file only needs
 * MoveFileExA/_commit from the Windows headers. */
#undef near
#undef far
#else
#include <unistd.h>   /* fsync */
#endif
#endif

/* ===== EEPROM — file-backed persistence ===== */

/* EEPROM_TYPE_16K = 16K bits = 2048 bytes = 256 blocks × 8 bytes/block.
 * Persisted via savedir (ge007_eeprom.bin). */
#define EEPROM_FILE_SIZE 2048
#define EEPROM_FILENAME  "ge007_eeprom.bin"

static u8 s_eeprom_data[EEPROM_FILE_SIZE];
static int s_eeprom_loaded = 0;

static void eeprom_load_from_file(void) {
#ifdef PROJECT_TITAN_TGR
    const TitanGameApiV1 *api=TitanGeRuntime_GetApi();
    char path[640];
    int descriptor;
    u32 done=0u;
    if(s_eeprom_loaded)return;
    s_eeprom_loaded=1;
    memset(s_eeprom_data,0,sizeof(s_eeprom_data));
    if(api==0||!TitanGeRuntime_MakePath("data/ge007_eeprom.bin",path,sizeof(path)))return;
    descriptor=api->storage_open(path,TITAN_GAME_STORAGE_READ);
    if(descriptor<0)return;
    while(done<EEPROM_FILE_SIZE){
        int64_t result=api->storage_read(descriptor,s_eeprom_data+done,EEPROM_FILE_SIZE-done);
        if(result<=0||(u64)result>EEPROM_FILE_SIZE-done)break;
        done+=(u32)result;
    }
    (void)api->storage_close(descriptor);
    if(done!=EEPROM_FILE_SIZE)memset(s_eeprom_data,0,sizeof(s_eeprom_data));
#else
    FILE *f;
    char path[1024];
    if (s_eeprom_loaded) return;
    s_eeprom_loaded = 1;
    memset(s_eeprom_data, 0, EEPROM_FILE_SIZE);
    snprintf(path, sizeof(path), "%s", savedirPath(EEPROM_FILENAME));
    f = fopen(path, "rb");
    if (f) {
        size_t read_count = fread(s_eeprom_data, 1, EEPROM_FILE_SIZE, f);
        if (read_count != EEPROM_FILE_SIZE) {
            int was_error = ferror(f);
            /* Truncated/corrupt save: fread left a PARTIAL real prefix in the
             * buffer (the pre-read memset only zeroed the untouched suffix), so
             * re-zero here to actually degrade to blank. Otherwise the stale
             * partial prefix stays live and eeprom_save_to_file() would write it
             * back over the original -- silent half-save corruption (AUDIT-0041). */
            memset(s_eeprom_data, 0, EEPROM_FILE_SIZE);
            fclose(f);
            /* Preserve the original so a later save can't silently overwrite a
             * potentially-recoverable file. Best-effort. */
            {
                char bak[1024 + 8];
                snprintf(bak, sizeof(bak), "%s.corrupt", path);
                if (rename(path, bak) != 0) {
                    fprintf(stderr,
                            "[GE007-PC] (could not preserve corrupt EEPROM as %s: %s)\n",
                            bak, strerror(errno));
                }
            }
            fprintf(stderr,
                    "[GE007-PC] WARNING: EEPROM save file is %s (read %zu of %d bytes); "
                    "treating as blank; original preserved as %s.corrupt.\n",
                    was_error ? "unreadable" : "truncated",
                    read_count, EEPROM_FILE_SIZE, EEPROM_FILENAME);
        } else {
            fclose(f);
        }
    }
#endif
}

/* Atomically persist the EEPROM image: write to a temp file in the same
 * directory, flush to disk, then rename over the real file. Returns 0 on
 * success, -1 on failure (logged; in-memory copy stays authoritative so a
 * later write can retry). Mirrors configSave() in config_pc.c. */
static int eeprom_save_to_file(void) {
#ifdef PROJECT_TITAN_TGR
    const TitanGameApiV1 *api=TitanGeRuntime_GetApi();
    char directory[640],path[640],temporary[648];
    int descriptor;
    u32 done=0u;
    if(api==0||
       !TitanGeRuntime_MakePath("data",directory,sizeof(directory))||
       !TitanGeRuntime_MakePath("data/ge007_eeprom.bin",path,sizeof(path))||
       !TitanGeRuntime_MakePath("data/ge007_eeprom.bin.tmp",temporary,sizeof(temporary)))
        return -1;
    (void)api->storage_mkdir(directory);
    descriptor=api->storage_open(temporary,TITAN_GAME_STORAGE_WRITE|
        TITAN_GAME_STORAGE_CREATE|TITAN_GAME_STORAGE_TRUNCATE);
    if(descriptor<0)return -1;
    while(done<EEPROM_FILE_SIZE){
        int64_t result=api->storage_write(descriptor,s_eeprom_data+done,EEPROM_FILE_SIZE-done);
        if(result<=0||(u64)result>EEPROM_FILE_SIZE-done)break;
        done+=(u32)result;
    }
    (void)api->storage_close(descriptor);
    if(done!=EEPROM_FILE_SIZE){(void)api->storage_remove(temporary);return -1;}
    (void)api->storage_remove(path);
    if(!api->storage_rename(temporary,path)){(void)api->storage_remove(temporary);return -1;}
    return 0;
#else
    char path[1024];
    char tmp_path[1024 + 8];
    FILE *f;

    /* savedirPath() returns a static buffer -- copy before reuse. */
    snprintf(path, sizeof(path), "%s", savedirPath(EEPROM_FILENAME));
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    f = fopen(tmp_path, "wb");
    if (!f) {
        fprintf(stderr, "[GE007-PC] EEPROM save failed: fopen(%s): %s\n",
                tmp_path, strerror(errno));
        return -1;
    }

    if (fwrite(s_eeprom_data, 1, EEPROM_FILE_SIZE, f) != EEPROM_FILE_SIZE) {
        fprintf(stderr, "[GE007-PC] EEPROM save failed: short write to %s: %s\n",
                tmp_path, strerror(errno));
        fclose(f);
        remove(tmp_path);
        return -1;
    }

    if (fflush(f) != 0
#ifdef _WIN32
        || _commit(_fileno(f)) != 0
#else
        || fsync(fileno(f)) != 0
#endif
    ) {
        fprintf(stderr, "[GE007-PC] EEPROM save failed: flush/sync %s: %s\n",
                tmp_path, strerror(errno));
        fclose(f);
        remove(tmp_path);
        return -1;
    }

    if (fclose(f) != 0) {
        fprintf(stderr, "[GE007-PC] EEPROM save failed: fclose(%s): %s\n",
                tmp_path, strerror(errno));
        remove(tmp_path);
        return -1;
    }

#ifdef _WIN32
    if (!MoveFileExA(tmp_path, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fprintf(stderr, "[GE007-PC] EEPROM save failed: replace %s: Windows error %lu\n",
                path, (unsigned long)GetLastError());
        remove(tmp_path);
        return -1;
    }
#else
    if (rename(tmp_path, path) != 0) {
        fprintf(stderr, "[GE007-PC] EEPROM save failed: rename %s -> %s: %s\n",
                tmp_path, path, strerror(errno));
        remove(tmp_path);
        return -1;
    }
#endif

    return 0;
#endif
}

s32 osEepromProbe(OSMesgQueue *mq) { (void)mq; return EEPROM_TYPE_16K; }

s32 osEepromLongRead(OSMesgQueue *mq, u8 address, u8 *buffer, s32 nbytes) {
    s32 offset;
    (void)mq;
    eeprom_load_from_file();
    offset = (s32)address * EEPROM_BLOCK_SIZE;
    /* [AUDIT-0042] validate the block offset and clamp the length with subtraction
     * so the range check cannot signed-overflow (offset + nbytes was UB). An
     * out-of-range block is a no-op leaving the persisted image unchanged. */
    if (offset < 0 || offset >= EEPROM_FILE_SIZE) return 0;
    if (nbytes > EEPROM_FILE_SIZE - offset) nbytes = EEPROM_FILE_SIZE - offset;
    if (nbytes > 0 && buffer) memcpy(buffer, s_eeprom_data + offset, nbytes);
    return 0;
}

s32 osEepromLongWrite(OSMesgQueue *mq, u8 address, u8 *buffer, s32 nbytes) {
    s32 offset;
    (void)mq;
    eeprom_load_from_file();
    offset = (s32)address * EEPROM_BLOCK_SIZE;
    /* [AUDIT-0042] validate the block offset and clamp the length with subtraction
     * so the range check cannot signed-overflow (offset + nbytes was UB). An
     * out-of-range block is a no-op leaving the persisted image unchanged. */
    if (offset < 0 || offset >= EEPROM_FILE_SIZE) return 0;
    if (nbytes > EEPROM_FILE_SIZE - offset) nbytes = EEPROM_FILE_SIZE - offset;
    if (nbytes > 0 && buffer) {
        memcpy(s_eeprom_data + offset, buffer, nbytes);
        if (eeprom_save_to_file() != 0) return -1;
    }
    return 0;
}

s32 osEepromRead(OSMesgQueue *mq, u8 address, u8 *buffer) {
    return osEepromLongRead(mq, address, buffer, EEPROM_BLOCK_SIZE);
}
s32 osEepromWrite(OSMesgQueue *mq, u8 address, u8 *buffer) {
    return osEepromLongWrite(mq, address, buffer, EEPROM_BLOCK_SIZE);
}

/* ===== Controller Pak stubs ===== */
/* Return PFS_ERR_NOPACK to tell the game no Controller Pak is connected.
   GE handles this gracefully — it skips pak operations when init fails. */

s32 osPfsInitPak(OSMesgQueue *mq, OSPfs *pfs, s32 channel) {
    (void)mq; (void)pfs; (void)channel; return PFS_ERR_NOPACK;
}
s32 osPfsRepairId(OSPfs *pfs) { (void)pfs; return PFS_ERR_NOPACK; }
s32 osPfsInit(OSMesgQueue *mq, OSPfs *pfs, s32 channel) {
    (void)mq; (void)pfs; (void)channel; return PFS_ERR_NOPACK;
}
s32 osPfsIsPlug(OSMesgQueue *mq, u8 *bitpattern) {
    (void)mq; if (bitpattern) *bitpattern = 0; return 0;
}
s32 osPfsAllocateFile(OSPfs *pfs, u16 company_code, u32 game_code,
                      u8 *game_name, u8 *ext_name, s32 file_size, s32 *file_no) {
    (void)pfs; (void)company_code; (void)game_code;
    (void)game_name; (void)ext_name; (void)file_size;
    if (file_no) *file_no = -1;
    return PFS_ERR_NOPACK;
}
s32 osPfsFindFile(OSPfs *pfs, u16 company_code, u32 game_code,
                  u8 *game_name, u8 *ext_name, s32 *file_no) {
    (void)pfs; (void)company_code; (void)game_code;
    (void)game_name; (void)ext_name;
    if (file_no) *file_no = -1;
    return PFS_ERR_NOPACK;
}
s32 osPfsDeleteFile(OSPfs *pfs, u16 company_code, u32 game_code,
                    u8 *game_name, u8 *ext_name) {
    (void)pfs; (void)company_code; (void)game_code;
    (void)game_name; (void)ext_name;
    return PFS_ERR_NOPACK;
}
s32 osPfsReadWriteFile(OSPfs *pfs, s32 file_no, u8 flag, s32 offset,
                       s32 nbytes, u8 *data_buffer) {
    (void)pfs; (void)file_no; (void)flag; (void)offset;
    (void)nbytes; (void)data_buffer;
    return PFS_ERR_NOPACK;
}
s32 osPfsFileState(OSPfs *pfs, s32 file_no, OSPfsState *state) {
    (void)pfs; (void)file_no; (void)state;
    return PFS_ERR_NOPACK;
}
s32 osPfsFreeBlocks(OSPfs *pfs, s32 *bytes_not_used) {
    (void)pfs; if (bytes_not_used) *bytes_not_used = 0;
    return PFS_ERR_NOPACK;
}
s32 osPfsNumFiles(OSPfs *pfs, s32 *max_files, s32 *files_used) {
    (void)pfs;
    if (max_files) *max_files = 0;
    if (files_used) *files_used = 0;
    return PFS_ERR_NOPACK;
}

/* ===== Misc stubs ===== */

void osInitialize(void) {}
void __osInitialize_common(void) {}

void osProfileInit(OSProf *prof, u32 count) { (void)prof; (void)count; }
void osProfileStart(u32 index) { (void)index; }
void osProfileStop(void) {}
void osProfileFlush(void) {}

/* ===== GU math — real implementations ===== */

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void guMtxIdentF(f32 mf[4][4]) {
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            mf[i][j] = (i == j) ? 1.0f : 0.0f;
}

static inline s32 f32_to_fixed16(f32 v) {
    f32 scaled = v * 65536.0f;
    if (scaled >= 2147483647.0f) return 0x7FFFFFFF;
    if (scaled <= -2147483648.0f) return INT32_MIN;
    return (s32)scaled;
}

void guMtxF2L(f32 mf[4][4], Mtx *m) {
    int i, j;
    s32 *ai = (s32 *)&m->m[0][0];
    s32 *af = (s32 *)&m->m[2][0];
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 2; j++) {
            s32 e1 = f32_to_fixed16(mf[i][j*2]);
            s32 e2 = f32_to_fixed16(mf[i][j*2+1]);
            *(ai++) = (e1 & 0xFFFF0000) | ((u32)e2 >> 16);
            *(af++) = ((u32)e1 << 16) | ((u32)e2 & 0xFFFF);
        }
    }
}

void guMtxL2F(f32 mf[4][4], Mtx *m) {
    int i, j;
    u32 *ai = (u32 *)&m->m[0][0];
    u32 *af = (u32 *)&m->m[2][0];
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 2; j++) {
            u32 hi = *ai++;
            u32 lo = *af++;
            s32 e1 = (s32)((hi & 0xFFFF0000) | (lo >> 16));
            s32 e2 = (s32)(((hi & 0xFFFF) << 16) | (lo & 0xFFFF));
            mf[i][j*2]   = (f32)e1 / 65536.0f;
            mf[i][j*2+1] = (f32)e2 / 65536.0f;
        }
    }
}

void guMtxIdent(Mtx *m) {
    f32 mf[4][4];
    guMtxIdentF(mf);
    guMtxF2L(mf, m);
}

void guOrthoF(f32 mf[4][4], f32 l, f32 r, f32 b, f32 t, f32 n, f32 f, f32 scale) {
    guMtxIdentF(mf);
    mf[0][0] = 2.0f / (r - l);
    mf[1][1] = 2.0f / (t - b);
    mf[2][2] = -2.0f / (f - n);
    mf[3][0] = -(r + l) / (r - l);
    mf[3][1] = -(t + b) / (t - b);
    mf[3][2] = -(f + n) / (f - n);
    mf[3][3] = 1.0f;
    /* N64 scales by 'scale' to fit fixed-point range */
    if (scale != 1.0f) {
        int i, j;
        for (i = 0; i < 4; i++)
            for (j = 0; j < 4; j++)
                mf[i][j] *= scale;
    }
}

void guOrtho(Mtx *m, f32 l, f32 r, f32 b, f32 t, f32 n, f32 f, f32 scale) {
    f32 mf[4][4];
    guOrthoF(mf, l, r, b, t, n, f, scale);
    guMtxF2L(mf, m);
}

void guPerspectiveF(f32 mf[4][4], u16 *perspNorm, f32 fovy, f32 aspect,
                    f32 near, f32 far, f32 scale) {
    f32 cot;
    int i, j;

    guMtxIdentF(mf);

    fovy = fovy * (f32)M_PI / 180.0f;
    cot = cosf(fovy / 2.0f) / sinf(fovy / 2.0f);

    mf[0][0] = cot / aspect;
    mf[1][1] = cot;
    mf[2][2] = (near + far) / (near - far);
    mf[2][3] = -1.0f;
    mf[3][2] = 2.0f * near * far / (near - far);
    mf[3][3] = 0.0f;

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            mf[i][j] *= scale;

    if (perspNorm != NULL) {
        if (near + far <= 2.0f) {
            *perspNorm = 0xFFFF;
        } else {
            *perspNorm = (u16)(2.0f * 65536.0f / (near + far));
            if (*perspNorm <= 0) *perspNorm = 1;
        }
    }
}

void guPerspective(Mtx *m, u16 *perspNorm, f32 fovy, f32 aspect,
                   f32 near, f32 far, f32 scale) {
    f32 mf[4][4];
    guPerspectiveF(mf, perspNorm, fovy, aspect, near, far, scale);
    guMtxF2L(mf, m);
}

void guLookAtF(f32 mf[4][4], f32 xEye, f32 yEye, f32 zEye,
               f32 xAt, f32 yAt, f32 zAt,
               f32 xUp, f32 yUp, f32 zUp) {
    f32 len;
    f32 xLook;
    f32 yLook;
    f32 zLook;
    f32 xRight;
    f32 yRight;
    f32 zRight;

    guMtxIdentF(mf);

    xLook = xAt - xEye;
    yLook = yAt - yEye;
    zLook = zAt - zEye;

    /* Match libultra: positive Z is behind the camera. */
    len = -1.0f / sqrtf(xLook * xLook + yLook * yLook + zLook * zLook);
    xLook *= len;
    yLook *= len;
    zLook *= len;

    /* Right = Up x Look */
    xRight = yUp * zLook - zUp * yLook;
    yRight = zUp * xLook - xUp * zLook;
    zRight = xUp * yLook - yUp * xLook;
    len = 1.0f / sqrtf(xRight * xRight + yRight * yRight + zRight * zRight);
    xRight *= len;
    yRight *= len;
    zRight *= len;

    /* Up = Look x Right */
    xUp = yLook * zRight - zLook * yRight;
    yUp = zLook * xRight - xLook * zRight;
    zUp = xLook * yRight - yLook * xRight;
    len = 1.0f / sqrtf(xUp * xUp + yUp * yUp + zUp * zUp);
    xUp *= len;
    yUp *= len;
    zUp *= len;

    mf[0][0] = xRight;
    mf[1][0] = yRight;
    mf[2][0] = zRight;
    mf[3][0] = -(xEye * xRight + yEye * yRight + zEye * zRight);

    mf[0][1] = xUp;
    mf[1][1] = yUp;
    mf[2][1] = zUp;
    mf[3][1] = -(xEye * xUp + yEye * yUp + zEye * zUp);

    mf[0][2] = xLook;
    mf[1][2] = yLook;
    mf[2][2] = zLook;
    mf[3][2] = -(xEye * xLook + yEye * yLook + zEye * zLook);

    mf[0][3] = 0.0f;
    mf[1][3] = 0.0f;
    mf[2][3] = 0.0f;
    mf[3][3] = 1.0f;
}

void guLookAt(Mtx *m, f32 xEye, f32 yEye, f32 zEye,
              f32 xAt, f32 yAt, f32 zAt,
              f32 xUp, f32 yUp, f32 zUp) {
    f32 mf[4][4];
    guLookAtF(mf, xEye, yEye, zEye, xAt, yAt, zAt, xUp, yUp, zUp);
    guMtxF2L(mf, m);
}

void guLookAtReflect(Mtx *m, LookAt *l, f32 xEye, f32 yEye, f32 zEye,
                     f32 xAt, f32 yAt, f32 zAt,
                     f32 xUp, f32 yUp, f32 zUp) {
    f32 mf[4][4];
    guLookAtF(mf, xEye, yEye, zEye, xAt, yAt, zAt, xUp, yUp, zUp);
    guMtxF2L(mf, m);
    if (l != NULL) {
        f32 len;
        f32 xLook = xAt - xEye;
        f32 yLook = yAt - yEye;
        f32 zLook = zAt - zEye;
        f32 xRight;
        f32 yRight;
        f32 zRight;

        len = -1.0f / sqrtf(xLook * xLook + yLook * yLook + zLook * zLook);
        xLook *= len;
        yLook *= len;
        zLook *= len;

        xRight = yUp * zLook - zUp * yLook;
        yRight = zUp * xLook - xUp * zLook;
        zRight = xUp * yLook - yUp * xLook;
        len = 1.0f / sqrtf(xRight * xRight + yRight * yRight + zRight * zRight);
        xRight *= len;
        yRight *= len;
        zRight *= len;

        xUp = yLook * zRight - zLook * yRight;
        yUp = zLook * xRight - xLook * zRight;
        zUp = xLook * yRight - yLook * xRight;
        len = 1.0f / sqrtf(xUp * xUp + yUp * yUp + zUp * zUp);
        xUp *= len;
        yUp *= len;
        zUp *= len;

        l->l[0].l.dir[0] = FTOFRAC8(xRight);
        l->l[0].l.dir[1] = FTOFRAC8(yRight);
        l->l[0].l.dir[2] = FTOFRAC8(zRight);
        l->l[1].l.dir[0] = FTOFRAC8(xUp);
        l->l[1].l.dir[1] = FTOFRAC8(yUp);
        l->l[1].l.dir[2] = FTOFRAC8(zUp);
        l->l[0].l.col[0] = 0x00;
        l->l[0].l.col[1] = 0x00;
        l->l[0].l.col[2] = 0x00;
        l->l[0].l.pad1 = 0x00;
        l->l[0].l.colc[0] = 0x00;
        l->l[0].l.colc[1] = 0x00;
        l->l[0].l.colc[2] = 0x00;
        l->l[0].l.pad2 = 0x00;
        l->l[1].l.col[0] = 0x00;
        l->l[1].l.col[1] = 0x80;
        l->l[1].l.col[2] = 0x00;
        l->l[1].l.pad1 = 0x00;
        l->l[1].l.colc[0] = 0x00;
        l->l[1].l.colc[1] = 0x80;
        l->l[1].l.colc[2] = 0x00;
        l->l[1].l.pad2 = 0x00;
    }
}

void guRotateF(f32 mf[4][4], f32 angle, f32 x, f32 y, f32 z) {
    f32 rad = angle * (f32)M_PI / 180.0f;
    f32 c = cosf(rad);
    f32 s = sinf(rad);
    f32 t = 1.0f - c;
    f32 len = sqrtf(x*x + y*y + z*z);

    if (len > 0.0f) { x /= len; y /= len; z /= len; }

    guMtxIdentF(mf);
    mf[0][0] = t*x*x + c;    mf[0][1] = t*x*y + s*z;  mf[0][2] = t*x*z - s*y;
    mf[1][0] = t*x*y - s*z;  mf[1][1] = t*y*y + c;    mf[1][2] = t*y*z + s*x;
    mf[2][0] = t*x*z + s*y;  mf[2][1] = t*y*z - s*x;  mf[2][2] = t*z*z + c;
}

void guRotate(Mtx *m, f32 angle, f32 x, f32 y, f32 z) {
    f32 mf[4][4];
    guRotateF(mf, angle, x, y, z);
    guMtxF2L(mf, m);
}

void guScaleF(f32 mf[4][4], f32 x, f32 y, f32 z) {
    guMtxIdentF(mf);
    mf[0][0] = x;
    mf[1][1] = y;
    mf[2][2] = z;
}

void guScale(Mtx *m, f32 x, f32 y, f32 z) {
    f32 mf[4][4];
    guScaleF(mf, x, y, z);
    guMtxF2L(mf, m);
}

void guTranslateF(f32 mf[4][4], f32 x, f32 y, f32 z) {
    guMtxIdentF(mf);
    mf[3][0] = x;
    mf[3][1] = y;
    mf[3][2] = z;
}

void guTranslate(Mtx *m, f32 x, f32 y, f32 z) {
    f32 mf[4][4];
    guTranslateF(mf, x, y, z);
    guMtxF2L(mf, m);
}

void guNormalize(f32 *x, f32 *y, f32 *z) {
    f32 len = sqrtf((*x)*(*x) + (*y)*(*y) + (*z)*(*z));
    if (len > 0.0f) { *x /= len; *y /= len; *z /= len; }
}

void guMtxCatF(f32 mf1[4][4], f32 mf2[4][4], f32 result[4][4]) {
    f32 tmp[4][4];
    int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            tmp[i][j] = 0.0f;
            for (k = 0; k < 4; k++)
                tmp[i][j] += mf1[i][k] * mf2[k][j];
        }
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            result[i][j] = tmp[i][j];
}

void guMtxCatL(Mtx *m1, Mtx *m2, Mtx *result) {
    f32 mf1[4][4], mf2[4][4], mfr[4][4];
    guMtxL2F(mf1, m1);
    guMtxL2F(mf2, m2);
    guMtxCatF(mf1, mf2, mfr);
    guMtxF2L(mfr, result);
}

void guMtxXFMF(f32 mf[4][4], f32 x, f32 y, f32 z, f32 *ox, f32 *oy, f32 *oz) {
    *ox = mf[0][0]*x + mf[1][0]*y + mf[2][0]*z + mf[3][0];
    *oy = mf[0][1]*x + mf[1][1]*y + mf[2][1]*z + mf[3][1];
    *oz = mf[0][2]*x + mf[1][2]*y + mf[2][2]*z + mf[3][2];
}

void guMtxXFML(Mtx *m, f32 x, f32 y, f32 z, f32 *ox, f32 *oy, f32 *oz) {
    f32 mf[4][4];
    guMtxL2F(mf, m);
    guMtxXFMF(mf, x, y, z, ox, oy, oz);
}

void guAlignF(f32 mf[4][4], f32 a, f32 x, f32 y, f32 z) {
    /* guAlign is just guRotate with angle in degrees — same thing */
    guRotateF(mf, a, x, y, z);
}

s32 guRandom(void) { return rand(); }

/* ===== Hardware globals ===== */
u32 osTvType = 1; /* OS_TV_NTSC */

/* rmonMain is now provided by rmon.c */

/* ===== Audio function stubs ===== */
#if 0
void alSynNew(ALSynth *s, ALSynConfig *c) { (void)s; (void)c; }
void alSynDelete(ALSynth *s) { (void)s; }
void alSynAddPlayer(ALSynth *s, void *p) { (void)s; (void)p; }
void alSynRemovePlayer(ALSynth *s, void *p) { (void)s; (void)p; }
s32 alSynAllocVoice(ALSynth *s, ALVoice *v, s32 p) { (void)s; (void)v; (void)p; return 0; }
void alSynFreeVoice(ALSynth *s, ALVoice *v) { (void)s; (void)v; }
void alSynStartVoice(ALSynth *s, ALVoice *v, void *w) { (void)s; (void)v; (void)w; }
void alSynStartVoiceParams(ALSynth *s, ALVoice *v, void *w, f32 vol, ALPan pan, s32 pri) {
    (void)s; (void)v; (void)w; (void)vol; (void)pan; (void)pri;
}
void alSynStopVoice(ALSynth *s, ALVoice *v) { (void)s; (void)v; }
void alSynSetVol(ALSynth *s, ALVoice *v, s16 vol) { (void)s; (void)v; (void)vol; }
void alSynSetPitch(ALSynth *s, ALVoice *v, f32 r) { (void)s; (void)v; (void)r; }
void alSynSetPan(ALSynth *s, ALVoice *v, ALPan p) { (void)s; (void)v; (void)p; }
void alSynSetFXMix(ALSynth *s, ALVoice *v, u8 m) { (void)s; (void)v; (void)m; }
void alSynSetPriority(ALSynth *s, ALVoice *v, s32 p) { (void)s; (void)v; (void)p; }
s32 alSynAllocFX(ALSynth *s, s32 bus, ALSynConfig *c) { (void)s; (void)bus; (void)c; return 0; }

void alSeqpNew(ALSeqPlayer *p, ALSeqpConfig *c) { (void)p; (void)c; }
void alSeqpDelete(ALSeqPlayer *p) { (void)p; }
void alSeqpSetSeq(ALSeqPlayer *p, ALSeq *s) { (void)p; (void)s; }
ALSeq *alSeqpGetSeq(ALSeqPlayer *p) { (void)p; return NULL; }
void alSeqpPlay(ALSeqPlayer *p) { (void)p; }
void alSeqpStop(ALSeqPlayer *p) { (void)p; }
s32 alSeqpGetState(ALSeqPlayer *p) { (void)p; return AL_SEQ_STOPPED; }
void alSeqpSetVol(ALSeqPlayer *p, s16 v) { (void)p; (void)v; }
s16 alSeqpGetVol(ALSeqPlayer *p) { (void)p; return 0; }
void alSeqpSetTempo(ALSeqPlayer *p, s32 t) { (void)p; (void)t; }
s32 alSeqpGetTempo(ALSeqPlayer *p) { (void)p; return 120; }
void alSeqpSetBank(ALSeqPlayer *p, ALBank *b) { (void)p; (void)b; }
void alSeqpSendMidi(ALSeqPlayer *p, s32 t, u8 s, u8 b1, u8 b2) {
    (void)p; (void)t; (void)s; (void)b1; (void)b2;
}

void alCSPNew(ALCSeqPlayer *p, ALSeqpConfig *c) { (void)p; (void)c; }
void alCSPDelete(ALCSeqPlayer *p) { (void)p; }
void alCSPSetSeq(ALCSeqPlayer *p, ALCSeq *s) { (void)p; (void)s; }
ALCSeq *alCSPGetSeq(ALCSeqPlayer *p) { (void)p; return NULL; }
void alCSPPlay(ALCSeqPlayer *p) { (void)p; }
void alCSPStop(ALCSeqPlayer *p) { (void)p; }
s32 alCSPGetState(ALCSeqPlayer *p) { (void)p; return AL_SEQ_STOPPED; }
void alCSPSetVol(ALCSeqPlayer *p, s16 v) { (void)p; (void)v; }
s16 alCSPGetVol(ALCSeqPlayer *p) { (void)p; return 0; }
void alCSPSetTempo(ALCSeqPlayer *p, s32 t) { (void)p; (void)t; }
s32 alCSPGetTempo(ALCSeqPlayer *p) { (void)p; return 120; }
void alCSPSetBank(ALCSeqPlayer *p, ALBank *b) { (void)p; (void)b; }

void alSeqNew(ALSeq *s, u8 *d, s32 l) { if(s){s->base=d;s->cur=d;s->len=l;} }
void alSeqNextEvent(ALSeq *s, ALEvent *e) { (void)s; (void)e; }
void alSeqNewMarker(ALSeq *s, ALSeqMarker *m, u32 t) { (void)s; (void)m; (void)t; }
void alSeqSetLoc(ALSeq *s, ALSeqMarker *m) { (void)s; (void)m; }
s32 alSeqGetTicks(ALSeq *s) { (void)s; return 0; }

void alCSeqNew(ALCSeq *s, u8 *d) { if(s){s->base=d;s->cur=d;} }
void alCSeqNextEvent(ALCSeq *s, ALEvent *e) { (void)s; (void)e; }

void alSndpNew(ALSndPlayer *p, ALSndpConfig *c) { (void)p; (void)c; }
void alSndpDelete(ALSndPlayer *p) { (void)p; }
ALSndId alSndpAllocate(ALSndPlayer *p, ALSound *s) { (void)p; (void)s; return 0; }
void alSndpDeallocate(ALSndPlayer *p, ALSndId id) { (void)p; (void)id; }
void alSndpSetSound(ALSndPlayer *p, ALSndId id) { (void)p; (void)id; }
void alSndpPlay(ALSndPlayer *p) { (void)p; }
void alSndpPlayAt(ALSndPlayer *p, ALMicroTime d) { (void)p; (void)d; }
void alSndpStop(ALSndPlayer *p) { (void)p; }
s32 alSndpGetState(ALSndPlayer *p) { (void)p; return AL_STOPPED; }
void alSndpSetVol(ALSndPlayer *p, s16 v) { (void)p; (void)v; }
void alSndpSetPitch(ALSndPlayer *p, f32 pitch) { (void)p; (void)pitch; }
void alSndpSetPan(ALSndPlayer *p, ALPan pan) { (void)p; (void)pan; }
void alSndpSetFXMix(ALSndPlayer *p, u8 m) { (void)p; (void)m; }
void alSndpSetPriority(ALSndPlayer *p, ALSndId id, u8 pri) { (void)p; (void)id; (void)pri; }

/* alBnkfNew: parse N64 bank file and decode audio samples via audio_pc.c */
extern void portAudioParseBankFile(u8 *ctlData, u32 ctlSize, u32 tblRomOffset);

/* Set by music.c before calling alBnkfNew so the parser gets the real .ctl size */
static u32 s_port_ctl_size = 0;
void portSetBankCtlSize(u32 size) { s_port_ctl_size = size; }

void alBnkfNew(ALBankFile *bf, u8 *t) {
    u32 tblRomOffset = (u32)(uintptr_t)t;
    u32 ctlSize = s_port_ctl_size ? s_port_ctl_size : 0x10000;
    s_port_ctl_size = 0; /* consume — one-shot */
    portAudioParseBankFile((u8 *)bf, ctlSize, tblRomOffset);
}

void alHeapInit(ALHeap *heap, u8 *base, s32 len) {
    if (heap) { heap->base = base; heap->cur = base; heap->len = len; heap->count = 0; }
}
void *alHeapDBAlloc(u8 *file, s32 line, ALHeap *heap, s32 count, s32 size) {
    (void)file; (void)line;
    if (heap && heap->cur) {
        void *ptr = heap->cur;
        s32 total = count * size;
        heap->cur += total;
        heap->count++;
        return ptr;
    }
    return NULL;
}

void alEvtqNew(ALEventQueue *eq, ALEvent *events, s32 count) {
    (void)eq; (void)events; (void)count;
}
void alEvtqNextEvent(ALEventQueue *eq, ALEvent *event) { (void)eq; (void)event; }
void alEvtqPostEvent(ALEventQueue *eq, ALEvent *event, ALMicroTime delta) {
    (void)eq; (void)event; (void)delta;
}

void alCopy(void *src, void *dst, s32 size) { memcpy(dst, src, size); }
f32 alCents2Ratio(s32 cents) { return powf(2.0f, (f32)cents / 1200.0f); }
#endif

/* ===== Additional OS stubs ===== */

void osCreateViManager(OSPri pri) { (void)pri; }

s32 osPiRawStartDma(s32 direction, u32 devAddr, void *dramAddr, u32 size) {
    extern u32  g_romSize;
    extern int platformRomRead(u32 offset, void *destination, u32 bytes);
    if (direction == OS_READ && size <= g_romSize && devAddr <= g_romSize - size && dramAddr) {
        (void)platformRomRead(devAddr, dramAddr, size);
    }
    return 0;
}

u32 __osGetFpcCsr(void) { return 0; }
void __osSetFpcCsr(u32 value) { (void)value; }

/* ===== _Printf stub ===== */
/* Some N64 code (rmon.c) calls the internal _Printf.
 * Redirect to vsnprintf. */
#include <stdarg.h>

int _Printf(char *(*output_fn)(char *, const char *, size_t), char *dest, const char *fmt, va_list ap) {
    (void)output_fn;
    if (dest && fmt) {
        return vsnprintf(dest, 256, fmt, ap);
    }
    return 0;
}

/* proutSprintf — used by rmon.c */
char *proutSprintf(char *dst, const char *src, size_t count) {
    memcpy(dst, src, count);
    return dst + count;
}

/* ===== Motor stubs (src/motor.c not compiled) ===== */
void motorInitialize(void) {}
s32 motorSetActivePlayer(s32 playerNum) { (void)playerNum; return 0; }

/* romCopy is now provided by ramrom.c */

/* ===== libm float used by guint.h ===== */
float __libm_qnan_f = 0.0f / 0.0f;

/* ===== ALIGN64_V2 — macro from macro.h, but some files don't include it ===== */
/* Provide as function fallback for implicit-function-declaration cases */
u32 ALIGN64_V2(u32 val) {
    return (((val) + 0x3f | 0x3f) ^ 0x3f);
}

/* ===== osPiReadIo ===== */
s32 osPiReadIo(u32 devAddr, u32 *data) {
    (void)devAddr; if (data) *data = 0; return 0;
}

/* ===== __osGetTLBHi ===== */
u32 __osGetTLBHi(s32 index) { (void)index; return 0; }

/* ===== Audio manager stubs (src/audi.c functions referenced but body may have issues) ===== */
/* amCreateAudioManager and amStartAudioThread should come from audi.c;
 * if they're missing at link time, they'll be added here. */

/* ===== resolve_TLBaddress_for_InvalidHit — from tlb_resolve.s ===== */
void resolve_TLBaddress_for_InvalidHit(void) {}

/* ===== D_80051D30-D_80051D54 — late_rodata float constants from chr.c GLOBAL_ASM ===== */
/* These are referenced from NONMATCHING C code that uses the float values directly. */
f32 D_80051D30 = 1.0471976f;
f32 D_80051D34 = -0.87266463f;
f32 D_80051D38 = 0.87266463f;
f32 D_80051D3C = 1.0471976f;
f32 D_80051D40 = -0.87266463f;
f32 D_80051D44 = 6.2831855f;
f32 D_80051D48 = 6.2831855f;
f32 D_80051D4C = 6.2831855f;
f32 D_80051D50 = 6.2831855f;
f32 D_80051D54 = 6.2831855f;

/* ===== unknown2 — binary data from romfiles2.s ===== */
/* ROM offset from N64 ELF: 0x002a4d50 (_romfiles2SegmentStart) */
u32 unknown2 = 0x002a4d50;

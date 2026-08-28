/**
 * stall_watchdog.c — BUG-1 silent-freeze capture rig. See stall_watchdog.h.
 *
 * Design constraints (docs/RENDERER_SIM_AUDIT + BUG-1 investigation):
 *  - The freeze signature is a silent wedge (sim infinite loop or GL/driver
 *    present wedge), so the dump must not depend on the wedged thread:
 *    everything runs on a dedicated watchdog thread.
 *  - Thread backtraces come from spawning `/usr/bin/sample <pid> 2 -file ...`
 *    (macOS). One approach, chosen over in-process unwinding because: (a) it
 *    needs no async-signal-unsafe work inside a process whose heap/locks may
 *    be corrupt or held by the wedged thread, (b) it captures ALL threads
 *    including GPU-driver worker threads wedged in Metal/GL (the #1 field
 *    hypothesis), and (c) a failure to sample cannot take the game down with
 *    it. posix_spawn (no fork of the possibly-corrupt address space on
 *    macOS) + waitpid on the watchdog thread.
 *  - Stall detection counts consecutive unchanged-heartbeat watchdog wakes
 *    instead of comparing wall-clock timestamps: under a debugger/instrument
 *    pause the watchdog thread is suspended too, so wakes don't accrue and
 *    resume does not fire a false stall. (A pause targeting only the main
 *    thread still false-positives; accepted.)
 *  - stderr writes in the dump path use write(2) into a private buffer, never
 *    stdio, so a sim thread wedged while holding the stdio lock can't wedge
 *    the watchdog as well. stderr is tee'd into mgb64.log by the app shell's
 *    diag log; the same block is also appended to <savedir>/stall_watchdog.log
 *    so launches without the shell keep the evidence too.
 */
#include "stall_watchdog.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h> /* va_list: do not rely on SDL_stdinc pulling it in */

/* AI-VM breadcrumb targets (written by chrai.c on the sim thread, one store
 * per interpreted AI command; read racily here). */
volatile int g_portWatchdogAiChrnum = -1;
volatile int g_portWatchdogAiOpcode = -1;
/* VTX-arena watermark of the frame just ended (stored by dynSwapBuffers). */
volatile int g_portWatchdogDynVtxUsed = 0;

#include <SDL.h>
#include <fcntl.h>
#if defined(_WIN32)
/* CRT fd IO (same shape as POSIX); the thread-sampler stays Apple-only and
 * Windows gets the breadcrumb ring + stall block, like Linux. */
#include <io.h>
#include <sys/stat.h> /* _S_IREAD/_S_IWRITE for _open() */
#else
#include <unistd.h>
#include <sys/wait.h>
#endif
#if defined(__APPLE__)
#include <spawn.h>
extern char **environ;
#endif

#include "../bondtypes.h"
#include "../game/chrai.h"
#include "port_env.h"
#include "savedir.h"

/* Sim-loop diagnostics the breadcrumb snapshots (all read-only here). */
extern int g_frame_count_diag;   /* render frame ordinal (gfx_pc.c) */
extern s32 g_StageNum;           /* current stage (boss.c) */
extern s32 g_ClockTimer;         /* capped sim tick count (lvl.c) */
extern s32 g_NumChrSlots;        /* chr slot table size (chr.c) */
extern ChrRecord *g_ChrSlots;
extern s32 g_dyn_overflow_count; /* per-frame dyn render-health (dyn.c) */

#define WATCHDOG_RING_LEN 120

typedef struct WatchdogCrumb {
    Uint32 heartbeat;   /* sim frame ordinal this crumb belongs to */
    int frame;          /* g_frame_count_diag */
    int stage;          /* g_StageNum */
    int clock_timer;    /* g_ClockTimer */
    int props;          /* bounded active prop-list count */
    int prop_walk;      /* 0 ok, 1 cap hit (cycle?), 2 invalid pointer */
    int chrs;           /* chr slots with a live prop */
    int ai_chrnum;      /* last AI command's chrnum */
    int ai_opcode;      /* last AI command's opcode */
    int dyn_vtx_used;   /* VTX arena bytes used by the finished frame */
    int dyn_overflows;  /* g_dyn_overflow_count for the finished frame */
} WatchdogCrumb;

static WatchdogCrumb s_ring[WATCHDOG_RING_LEN];

static SDL_atomic_t s_heartbeat;      /* published once per frame */
static SDL_atomic_t s_running;        /* 1 = frame loop, 0 = load/init */
static SDL_atomic_t s_paused;         /* 1 = legit solo-pause (sim frozen on purpose) */

static int  s_armed = 0;              /* watchdog enabled + thread started */
static int  s_stallSecs = 5;
static int  s_testStall = 0;
static char s_stallLogPath[1024];
static char s_samplePath[2][1024];

/* Bounded, validity-checked active prop-list walk (read-only, racy by
 * design). The list is the #2 freeze hypothesis (cyclic `prop->prev`), so
 * the walk caps at pool size and validates every pointer: a count pinned at
 * POS_DATA_ENTRY_LEN with walk=1 in the crumbs IS the cycle diagnosis. */
static int watchdogPropPtrValid(const PropRecord *prop) {
    uintptr_t ptr = (uintptr_t)prop;
    uintptr_t first = (uintptr_t)&pos_data_entry[0];
    uintptr_t last = (uintptr_t)&pos_data_entry[POS_DATA_ENTRY_LEN];

    return ptr >= first &&
           ptr < last &&
           ((ptr - first) % sizeof(pos_data_entry[0])) == 0;
}

static int watchdogCountActiveProps(int *walk_flag) {
    const PropRecord *prop = ptr_obj_pos_list_current_entry;
    int count = 0;

    *walk_flag = 0;
    while (prop != NULL) {
        if (!watchdogPropPtrValid(prop)) {
            *walk_flag = 2;
            break;
        }
        count++;
        if (count >= POS_DATA_ENTRY_LEN) {
            *walk_flag = 1;
            break;
        }
        prop = prop->prev;
    }
    return count;
}

static int watchdogCountLiveChrs(void) {
    int count = 0;
    s32 i;
    s32 n = g_NumChrSlots;

    if (g_ChrSlots == NULL || n <= 0) {
        return 0;
    }
    if (n > POS_DATA_ENTRY_LEN) {
        n = POS_DATA_ENTRY_LEN;
    }
    for (i = 0; i < n; i++) {
        if (g_ChrSlots[i].prop != NULL) {
            count++;
        }
    }
    return count;
}

/* ---- dump path (watchdog thread only) ---------------------------------- */

/* Private append buffer: the whole dump is formatted here and flushed with
 * write(2) so the dump path never takes a stdio lock. Watchdog-thread-only,
 * so a single static buffer is safe. */
static char s_dumpBuf[48 * 1024];
static int  s_dumpLen = 0;

static void dumpReset(void) {
    s_dumpLen = 0;
}

static void dumpf(const char *fmt, ...) {
    va_list ap;
    int room = (int)sizeof(s_dumpBuf) - s_dumpLen;
    int n;

    if (room <= 1) {
        return;
    }
    va_start(ap, fmt);
    n = vsnprintf(s_dumpBuf + s_dumpLen, (size_t)room, fmt, ap);
    va_end(ap);
    if (n > 0) {
        s_dumpLen += (n < room) ? n : (room - 1);
    }
}

static void dumpFlush(void) {
    int fd;

    if (s_dumpLen <= 0) {
        return;
    }
#if defined(_WIN32)
    {
        int w = _write(2, s_dumpBuf, (unsigned)s_dumpLen);
        (void)w;
    }
    fd = _open(s_stallLogPath, _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY,
               _S_IREAD | _S_IWRITE);
    if (fd >= 0) {
        int w = _write(fd, s_dumpBuf, (unsigned)s_dumpLen);
        (void)w;
        _close(fd);
    }
#else
    {
        ssize_t w = write(STDERR_FILENO, s_dumpBuf, (size_t)s_dumpLen);
        (void)w;

        fd = open(s_stallLogPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            w = write(fd, s_dumpBuf, (size_t)s_dumpLen);
            (void)w;
            close(fd);
        }
    }
#endif
}

/* One post-mortem integrity pass over the active prop list with a visited
 * bitmap: directly confirms/denies the cyclic-list hypothesis and names the
 * first revisited pool slot. Bounded, read-only, validity-checked. */
static void dumpActiveListIntegrity(void) {
    unsigned char visited[POS_DATA_ENTRY_LEN];
    const PropRecord *prop = ptr_obj_pos_list_current_entry;
    int steps = 0;

    memset(visited, 0, sizeof(visited));
    while (prop != NULL && steps <= POS_DATA_ENTRY_LEN) {
        size_t slot;

        if (!watchdogPropPtrValid(prop)) {
            dumpf("[WATCHDOG] active-list integrity: INVALID pointer %p after %d node(s)\n",
                  (const void *)prop, steps);
            return;
        }
        slot = (size_t)(prop - &pos_data_entry[0]);
        if (visited[slot]) {
            dumpf("[WATCHDOG] active-list integrity: CYCLE — slot %d revisited after %d node(s)\n",
                  (int)slot, steps);
            return;
        }
        visited[slot] = 1;
        steps++;
        prop = prop->prev;
    }
    if (prop != NULL) {
        dumpf("[WATCHDOG] active-list integrity: walk exceeded pool size (%d) without NULL — corrupt\n",
              POS_DATA_ENTRY_LEN);
    } else {
        dumpf("[WATCHDOG] active-list integrity: OK (%d node(s), no cycle)\n", steps);
    }
}

static void dumpBreadcrumbRing(Uint32 hb_now) {
    Uint32 first = (hb_now > WATCHDOG_RING_LEN) ? (hb_now - WATCHDOG_RING_LEN) : 0;
    Uint32 hb;

    dumpf("[WATCHDOG] breadcrumb ring (oldest -> newest, %u..%u):\n",
          (unsigned)first, (unsigned)(hb_now ? hb_now - 1 : 0));
    for (hb = first; hb < hb_now; hb++) {
        const WatchdogCrumb *c = &s_ring[hb % WATCHDOG_RING_LEN];

        if (c->heartbeat != hb) {
            continue; /* slot not yet written / already overwritten (racy read) */
        }
        dumpf("[WATCHDOG]   hb=%u frame=%d stage=%d clk=%d props=%d walk=%d chrs=%d "
              "ai_chr=%d ai_op=0x%02X dynvtx=%d dynovf=%d\n",
              (unsigned)c->heartbeat, c->frame, c->stage, c->clock_timer,
              c->props, c->prop_walk, c->chrs,
              c->ai_chrnum, (unsigned)(c->ai_opcode & 0xFF),
              c->dyn_vtx_used, c->dyn_overflows);
    }
}

static void dumpSpawnSample(int dump_no) {
#if defined(__APPLE__)
    char pidbuf[16];
    const char *path = s_samplePath[dump_no - 1];
    char *argv[] = { "/usr/bin/sample", pidbuf, "2", "-file", (char *)path, NULL };
    pid_t child = -1;
    int rc;

    snprintf(pidbuf, sizeof(pidbuf), "%d", (int)getpid());
    rc = posix_spawn(&child, "/usr/bin/sample", NULL, NULL, argv, environ);
    if (rc == 0) {
        int status = 0;
        dumpf("[WATCHDOG] sampling all threads -> %s (2s)...\n", path);
        dumpFlush();
        dumpReset();
        waitpid(child, &status, 0);
        dumpf("[WATCHDOG] sample done (status %d)\n", status);
    } else {
        dumpf("[WATCHDOG] posix_spawn(/usr/bin/sample) failed: %d\n", rc);
    }
#else
    (void)dump_no;
    dumpf("[WATCHDOG] thread sampling unavailable on this platform "
          "(breadcrumbs + stall block only)\n");
#endif
}

static void watchdogDumpStall(Uint32 hb_now, int stalled_secs, int dump_no) {
    dumpReset();
    dumpf("\n[WATCHDOG] SIM STALL: heartbeat frozen for ~%ds "
          "(hb=%u frame=%d stage=%d, dump %d/2)\n",
          stalled_secs, (unsigned)hb_now, g_frame_count_diag, g_StageNum, dump_no);
    dumpActiveListIntegrity();
    dumpBreadcrumbRing(hb_now);
    dumpSpawnSample(dump_no);
    dumpf("[WATCHDOG] end of stall dump %d/2 (process left running for capture)\n\n",
          dump_no);
    dumpFlush();
    dumpReset();
}

/* ---- watchdog thread ---------------------------------------------------- */

static int watchdogThreadMain(void *arg) {
    Uint32 last_hb = (Uint32)SDL_AtomicGet(&s_heartbeat);
    int stalled = 0;
    int dumps = 0;

    (void)arg;
    for (;;) {
        Uint32 hb;

        SDL_Delay(1000);

        if (!SDL_AtomicGet(&s_running)) {
            /* load/init phase: loads legitimately block the main loop */
            last_hb = (Uint32)SDL_AtomicGet(&s_heartbeat);
            stalled = 0;
            dumps = 0;
            continue;
        }

        if (SDL_AtomicGet(&s_paused)) {
            /* MC.7: single-player settings-overlay pause deliberately freezes
             * the sim. Rendering keeps publishing the heartbeat, so this is a
             * belt-and-suspenders guard (and covers the case where a paused,
             * unfocused window also stops presenting): treat it exactly like the
             * load phase — reset the stall accounting and never dump. */
            last_hb = (Uint32)SDL_AtomicGet(&s_heartbeat);
            stalled = 0;
            dumps = 0;
            continue;
        }

        hb = (Uint32)SDL_AtomicGet(&s_heartbeat);
        if (hb != last_hb) {
            last_hb = hb;
            stalled = 0;
            dumps = 0; /* stall cleared: re-arm for the next one */
            continue;
        }

        stalled++;
        if (dumps == 0 && stalled >= s_stallSecs) {
            dumps = 1;
            watchdogDumpStall(hb, stalled, 1);
        } else if (dumps == 1 && stalled >= 2 * s_stallSecs) {
            dumps = 2;
            watchdogDumpStall(hb, stalled, 2);
        }
        /* dumps == 2: stay silent until the heartbeat advances again */
    }
    return 0;
}

/* ---- hooks (main/sim thread) -------------------------------------------- */

void portWatchdogInit(void) {
    SDL_Thread *thread;

#ifdef __EMSCRIPTEN__
    /* The stall watchdog is a native diagnostic: a background thread
     * (SDL_CreateThread, no -pthread under single-threaded wasm) that on a
     * stall shells out via posix_spawn(/usr/bin/sample). Neither has a wasm
     * runtime backing — both fail at runtime. In the browser the tab's own
     * devtools/hang detection is the equivalent. */
    (void)thread;
    return;
#else
    if (port_env_bool("GE007_NO_WATCHDOG", 0,
            "disable the sim stall watchdog (heartbeat monitor + stall dump + breadcrumb ring)")) {
        return;
    }
    s_stallSecs = port_env_int("GE007_WATCHDOG_STALL_SECS", 5,
        "seconds without a sim heartbeat before the stall watchdog dumps (default 5)");
    if (s_stallSecs < 2) {
        s_stallSecs = 2; /* below the 1s wake granularity the count is noise */
    }
    s_testStall = port_env_bool("GE007_WATCHDOG_TEST", 0,
        "negative control: synthetically stall the sim thread at frame ~600 to prove the watchdog capture path");

    /* savedirPath returns a static buffer and is not thread-safe: resolve
     * every path the watchdog thread will ever need here, on the main
     * thread, before the thread exists. */
    snprintf(s_stallLogPath, sizeof(s_stallLogPath), "%s", savedirPath("stall_watchdog.log"));
    snprintf(s_samplePath[0], sizeof(s_samplePath[0]), "%s", savedirPath("stall_sample_1.txt"));
    snprintf(s_samplePath[1], sizeof(s_samplePath[1]), "%s", savedirPath("stall_sample_2.txt"));

    SDL_AtomicSet(&s_heartbeat, 0);
    SDL_AtomicSet(&s_running, 0);
    SDL_AtomicSet(&s_paused, 0);

    thread = SDL_CreateThread(watchdogThreadMain, "ge007-stall-watchdog", NULL);
    if (thread == NULL) {
        fprintf(stderr, "[WATCHDOG] SDL_CreateThread failed: %s (watchdog disabled)\n",
                SDL_GetError());
        return;
    }
    SDL_DetachThread(thread);
    s_armed = 1;
#endif /* __EMSCRIPTEN__ */
}

void portWatchdogLoadBegin(void) {
    if (!s_armed) {
        return;
    }
    SDL_AtomicSet(&s_running, 0);
}

void portWatchdogLoadEnd(void) {
    if (!s_armed) {
        return;
    }
    SDL_AtomicSet(&s_running, 1);
}

void portWatchdogSetPaused(int paused) {
    if (!s_armed) {
        return;
    }
    SDL_AtomicSet(&s_paused, paused ? 1 : 0);
}

void portWatchdogFrameTick(void) {
    Uint32 hb;
    WatchdogCrumb *c;

    if (!s_armed) {
        return;
    }

    hb = (Uint32)SDL_AtomicGet(&s_heartbeat);
    c = &s_ring[hb % WATCHDOG_RING_LEN];
    c->frame = g_frame_count_diag;
    c->stage = (int)g_StageNum;
    c->clock_timer = (int)g_ClockTimer;
    c->props = watchdogCountActiveProps(&c->prop_walk);
    c->chrs = watchdogCountLiveChrs();
    c->ai_chrnum = g_portWatchdogAiChrnum;
    c->ai_opcode = g_portWatchdogAiOpcode;
    c->dyn_vtx_used = g_portWatchdogDynVtxUsed;
    c->dyn_overflows = (int)g_dyn_overflow_count;
    c->heartbeat = hb; /* marks the slot valid; written last before publish */

    SDL_AtomicSet(&s_heartbeat, (int)(hb + 1));

    if (s_testStall && hb == 600) {
        int sleep_ms = (3 * s_stallSecs + 2) * 1000;
        fprintf(stderr, "[WATCHDOG-TEST] synthetic sim stall: sleeping %d ms at heartbeat %u\n",
                sleep_ms, (unsigned)hb);
        SDL_Delay((Uint32)sleep_ms);
        fprintf(stderr, "[WATCHDOG-TEST] synthetic stall over; sim resumes\n");
    }
}

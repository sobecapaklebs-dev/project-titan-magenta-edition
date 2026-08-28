/*
 * PC input tape — disk record/replay (FID-0034, S-Tier Task 0.6).
 *
 * Two halves (see input_tape.h):
 *   1. The pure .ge7tape reader/writer (always compiled; ROM-free). All
 *      multi-byte fields are serialised little-endian by hand so a committed
 *      tape is portable across platforms and free of struct-padding surprises.
 *   2. The engine glue under NATIVE_PORT: record/playback hook functions that
 *      slot into the retail g_ContRecordFunc / g_ContPlaybackFunc seam exactly
 *      as the RAMROM demo path does (record_player_input_as_packet /
 *      ramrom_replay_handler are the references, ramromreplay.c).
 */
#include "input_tape.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>   /* MoveFileExA: rename() won't replace an existing file */
#endif

/* Atomically publish tmp over path. POSIX rename() replaces an existing target;
 * the Windows CRT rename() fails with EEXIST if the target exists, so re-recording
 * a tape (the destination already present) would lose the write -- use MoveFileExA
 * with REPLACE_EXISTING there, matching config_pc.c replaceConfigFile / the EEPROM
 * writer in stubs.c. Returns 0 on success. */
static int tape_publish(const char *tmp_path, const char *path) {
#ifdef _WIN32
    return MoveFileExA(tmp_path, path,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
    return rename(tmp_path, path);
#endif
}

/* =====================================================================
 * Pure .ge7tape serialisation (ROM-free)
 * ===================================================================== */

/* On-disk size of the fixed header: 8 magic + 8*u32 + 1*u64 (version, level_id,
 * difficulty, tick_hz, flags, num_players, controller_count, tick_count as u32;
 * rng_seed as u64). */
#define GE7TAPE_HEADER_BYTES  (GE7TAPE_MAGIC_LEN + 4u*8u + 8u)
/* On-disk size of one per-tick record: u32 tick + num_players * (u16+s8+s8). */
#define GE7TAPE_PAD_BYTES     4u

#define GE7TAPE_MAX_PLAYERS   4u
#define GE7TAPE_MAX_TICKS     (16u * 1024u * 1024u)  /* sanity clamp on read */

/* ---- little-endian put/get into a byte cursor ---- */
static void put_u16(unsigned char **p, uint16_t v) {
    (*p)[0] = (unsigned char)(v & 0xFF);
    (*p)[1] = (unsigned char)((v >> 8) & 0xFF);
    *p += 2;
}
static void put_u32(unsigned char **p, uint32_t v) {
    (*p)[0] = (unsigned char)(v & 0xFF);
    (*p)[1] = (unsigned char)((v >> 8) & 0xFF);
    (*p)[2] = (unsigned char)((v >> 16) & 0xFF);
    (*p)[3] = (unsigned char)((v >> 24) & 0xFF);
    *p += 4;
}
static void put_u64(unsigned char **p, uint64_t v) {
    put_u32(p, (uint32_t)(v & 0xFFFFFFFFu));
    put_u32(p, (uint32_t)((v >> 32) & 0xFFFFFFFFu));
}
static uint16_t get_u16(const unsigned char **p) {
    uint16_t v = (uint16_t)((*p)[0] | ((uint16_t)(*p)[1] << 8));
    *p += 2;
    return v;
}
static uint32_t get_u32(const unsigned char **p) {
    uint32_t v = (uint32_t)(*p)[0] | ((uint32_t)(*p)[1] << 8) |
                 ((uint32_t)(*p)[2] << 16) | ((uint32_t)(*p)[3] << 24);
    *p += 4;
    return v;
}
static uint64_t get_u64(const unsigned char **p) {
    uint64_t lo = get_u32(p);
    uint64_t hi = get_u32(p);
    return lo | (hi << 32);
}

/* ---- Writer ---- */

struct InputTapeWriter {
    InputTapeHeader header;
    char           *path;
    char           *tmp_path; /* "<path>.tmp"; renamed over path at close */
    FILE           *fp;       /* held open from Open (validates the dest early) */
    uint32_t       *ticks;    /* [cap]                    */
    InputTapePad   *pads;     /* [cap * num_players]      */
    uint32_t        count;    /* records appended         */
    uint32_t        cap;      /* records reserved         */
};

static int writer_grow(InputTapeWriter *w) {
    uint32_t newcap = (w->cap == 0) ? 1024u : (w->cap * 2u);
    uint32_t *nt = (uint32_t *)realloc(w->ticks, (size_t)newcap * sizeof(uint32_t));
    InputTapePad *np;
    if (!nt) {
        return -1;
    }
    w->ticks = nt;
    np = (InputTapePad *)realloc(
        w->pads, (size_t)newcap * (size_t)w->header.num_players * sizeof(InputTapePad));
    if (!np) {
        return -1;
    }
    w->pads = np;
    w->cap = newcap;
    return 0;
}

InputTapeWriter *inputTapeWriterOpen(const char *path, const InputTapeHeader *hdr) {
    InputTapeWriter *w;
    size_t plen;

    if (!path || !hdr) {
        return NULL;
    }
    if (memcmp(hdr->magic, GE7TAPE_MAGIC, GE7TAPE_MAGIC_LEN) != 0) {
        return NULL;
    }
    if (hdr->num_players == 0 || hdr->num_players > GE7TAPE_MAX_PLAYERS) {
        return NULL;
    }

    w = (InputTapeWriter *)calloc(1, sizeof(*w));
    if (!w) {
        return NULL;
    }
    w->header = *hdr;
    w->header.tick_count = 0;   /* writer owns this */
    plen = strlen(path) + 1;
    w->path = (char *)malloc(plen);
    w->tmp_path = (char *)malloc(plen + 4);   /* ".tmp" + NUL */
    if (!w->path || !w->tmp_path) {
        free(w->path);
        free(w->tmp_path);
        free(w);
        return NULL;
    }
    memcpy(w->path, path, plen);
    snprintf(w->tmp_path, plen + 4, "%s.tmp", path);

    /* AUDIT-0061: reserve+validate the destination NOW, before gameplay, by
     * opening the temp file up front. A bad --record-tape path (unwritable dir,
     * read-only fs) fails here so the caller can abort instead of discovering it
     * only at process exit. The live destination is never touched until the
     * atomic rename in inputTapeWriterClose (AUDIT-0062), so a crash mid-run
     * leaves any existing tape intact. */
    w->fp = fopen(w->tmp_path, "wb");
    if (!w->fp) {
        free(w->path);
        free(w->tmp_path);
        free(w);
        return NULL;
    }
    return w;
}

int inputTapeWriterAppendTick(InputTapeWriter *w, uint32_t tick,
                              const InputTapePad *pads, uint32_t num_players) {
    uint32_t p;

    if (!w || !pads) {
        return -1;
    }
    if (num_players != w->header.num_players) {
        return -1;
    }
    if (w->count == w->cap && writer_grow(w) != 0) {
        return -1;
    }
    w->ticks[w->count] = tick;
    for (p = 0; p < num_players; p++) {
        w->pads[(size_t)w->count * num_players + p] = pads[p];
    }
    w->count++;
    return 0;
}

int inputTapeWriterClose(InputTapeWriter *w) {
    FILE *f;
    unsigned char hdrbuf[GE7TAPE_HEADER_BYTES];
    unsigned char *cur;
    uint32_t i, p;
    int rc = 0;
    int published = 0;

    if (!w) {
        return 0;
    }

    /* The temp file was opened+validated in inputTapeWriterOpen; write into it and
     * only rename it over the live destination once it is complete and durable
     * (AUDIT-0062). Any failure leaves the live tape untouched and the temp
     * removed -- never a truncated/partial fixture. */
    f = w->fp;
    if (!f) {
        rc = -1;
        goto done;
    }

    /* Header (tick_count now known). */
    cur = hdrbuf;
    memcpy(cur, w->header.magic, GE7TAPE_MAGIC_LEN);
    cur += GE7TAPE_MAGIC_LEN;
    put_u32(&cur, w->header.version);
    put_u32(&cur, w->header.level_id);
    put_u32(&cur, w->header.difficulty);
    put_u64(&cur, w->header.rng_seed);
    put_u32(&cur, w->header.tick_hz);
    put_u32(&cur, w->header.flags);
    put_u32(&cur, w->header.num_players);
    put_u32(&cur, w->header.controller_count);
    put_u32(&cur, w->count);
    if (fwrite(hdrbuf, 1, sizeof hdrbuf, f) != sizeof hdrbuf) {
        rc = -1;
        goto close_done;
    }

    /* Records. */
    for (i = 0; i < w->count; i++) {
        unsigned char rec[4 + GE7TAPE_MAX_PLAYERS * GE7TAPE_PAD_BYTES];
        size_t reclen;
        unsigned char *rc_cur = rec;
        put_u32(&rc_cur, w->ticks[i]);
        for (p = 0; p < w->header.num_players; p++) {
            const InputTapePad *tp = &w->pads[(size_t)i * w->header.num_players + p];
            put_u16(&rc_cur, tp->button);
            *rc_cur++ = (unsigned char)(uint8_t)tp->stick_x;
            *rc_cur++ = (unsigned char)(uint8_t)tp->stick_y;
        }
        reclen = (size_t)(rc_cur - rec);
        if (fwrite(rec, 1, reclen, f) != reclen) {
            rc = -1;
            goto close_done;
        }
    }

    if (ferror(f) || fflush(f) != 0) {
        rc = -1;
    }

close_done:
    if (fclose(f) != 0) {
        rc = -1;
    }
    w->fp = NULL;
    if (rc == 0) {
        if (tape_publish(w->tmp_path, w->path) == 0) {
            published = 1;
        } else {
            rc = -1;
        }
    }
    if (!published) {
        remove(w->tmp_path);   /* leave the live destination untouched */
    }
done:
    free(w->ticks);
    free(w->pads);
    free(w->path);
    free(w->tmp_path);
    free(w);
    return rc;
}

/* ---- Reader ---- */

InputTape *inputTapeRead(const char *path) {
    FILE *f;
    unsigned char hdrbuf[GE7TAPE_HEADER_BYTES];
    const unsigned char *cur;
    InputTape *t;
    uint32_t i, p;
    size_t reclen;
    unsigned char *recbuf = NULL;

    if (!path) {
        return NULL;
    }
    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    if (fread(hdrbuf, 1, sizeof hdrbuf, f) != sizeof hdrbuf) {
        fclose(f);
        return NULL;
    }

    t = (InputTape *)calloc(1, sizeof(*t));
    if (!t) {
        fclose(f);
        return NULL;
    }

    cur = hdrbuf;
    memcpy(t->header.magic, cur, GE7TAPE_MAGIC_LEN);
    cur += GE7TAPE_MAGIC_LEN;
    t->header.version          = get_u32(&cur);
    t->header.level_id         = get_u32(&cur);
    t->header.difficulty       = get_u32(&cur);
    t->header.rng_seed         = get_u64(&cur);
    t->header.tick_hz          = get_u32(&cur);
    t->header.flags            = get_u32(&cur);
    t->header.num_players      = get_u32(&cur);
    t->header.controller_count = get_u32(&cur);
    t->header.tick_count       = get_u32(&cur);

    if (memcmp(t->header.magic, GE7TAPE_MAGIC, GE7TAPE_MAGIC_LEN) != 0 ||
        t->header.version != GE7TAPE_VERSION ||
        t->header.num_players == 0 || t->header.num_players > GE7TAPE_MAX_PLAYERS ||
        t->header.tick_count > GE7TAPE_MAX_TICKS) {
        free(t);
        fclose(f);
        return NULL;
    }

    /* AUDIT-0065: reject a file whose actual size does not match the extent its
     * header claims BEFORE allocating from the (untrusted) tick_count -- otherwise
     * a header claiming tick_count=16M forces a ~336 MB allocation for a truncated
     * file. A well-formed tape is exactly header + tick_count*reclen bytes; a
     * short body or trailing garbage is corrupt. Overflow-safe: num_players<=4 and
     * tick_count<=GE7TAPE_MAX_TICKS (16M), so reclen<=20 and the product < 2^29. */
    {
        long fsz;
        size_t reclen_e = 4u + (size_t)t->header.num_players * GE7TAPE_PAD_BYTES;
        size_t expected = (size_t)GE7TAPE_HEADER_BYTES +
                          (size_t)t->header.tick_count * reclen_e;
        if (fseek(f, 0, SEEK_END) != 0) { free(t); fclose(f); return NULL; }
        fsz = ftell(f);
        /* Restore the cursor to just past the header for the record read loop. */
        if (fsz < 0 || (size_t)fsz != expected ||
            fseek(f, (long)GE7TAPE_HEADER_BYTES, SEEK_SET) != 0) {
            free(t);
            fclose(f);
            return NULL;
        }
    }

    if (t->header.tick_count > 0) {
        t->ticks = (uint32_t *)malloc((size_t)t->header.tick_count * sizeof(uint32_t));
        t->pads = (InputTapePad *)malloc((size_t)t->header.tick_count *
                                         (size_t)t->header.num_players * sizeof(InputTapePad));
        if (!t->ticks || !t->pads) {
            inputTapeFree(t);
            fclose(f);
            return NULL;
        }
    }

    reclen = 4u + (size_t)t->header.num_players * GE7TAPE_PAD_BYTES;
    recbuf = (unsigned char *)malloc(reclen);
    if (!recbuf) {
        inputTapeFree(t);
        fclose(f);
        return NULL;
    }

    for (i = 0; i < t->header.tick_count; i++) {
        const unsigned char *rc_cur = recbuf;
        if (fread(recbuf, 1, reclen, f) != reclen) {
            free(recbuf);
            inputTapeFree(t);
            fclose(f);
            return NULL;
        }
        t->ticks[i] = get_u32(&rc_cur);
        for (p = 0; p < t->header.num_players; p++) {
            InputTapePad *tp = &t->pads[(size_t)i * t->header.num_players + p];
            tp->button  = get_u16(&rc_cur);
            tp->stick_x = (int8_t)(uint8_t)*rc_cur++;
            tp->stick_y = (int8_t)(uint8_t)*rc_cur++;
        }
    }

    free(recbuf);
    fclose(f);
    return t;
}

void inputTapeFree(InputTape *t) {
    if (!t) {
        return;
    }
    free(t->ticks);
    free(t->pads);
    free(t);
}

/* =====================================================================
 * Engine glue (NATIVE_PORT only)
 *
 * add_definitions(-DNATIVE_PORT) in CMakeLists.txt is directory-wide and so also
 * reaches the ROM-free unit-test target; INPUT_TAPE_UNIT_TEST (set only on that
 * target) suppresses the game-header pull-in, mirroring GFX_ROOM_NORMALS_UNIT_TEST.
 * ===================================================================== */
#if defined(NATIVE_PORT) && !defined(INPUT_TAPE_UNIT_TEST)
#include "joy.h"                 /* struct contsample, CONTSAMPLE_LEN, setters   */
#include "game/lvl.h"            /* g_GlobalTimer                                */

extern contplaybackfunc g_ContPlaybackFunc;
extern contrecordfunc   g_ContRecordFunc;
extern int  g_deterministic;
extern void simStateHashEmitIfRequested(int frame, const char *replay);

/* Requested configuration (set from arg parse). */
static const char *s_recordPath = NULL;
static const char *s_playPath   = NULL;
static int         s_sessionLevel      = -1;
static int         s_sessionDifficulty = 0;

/* Live state. */
static InputTapeWriter *s_writer      = NULL;
static InputTape       *s_tape        = NULL;
static int              s_writerFailed = 0;
static int              s_tapeFailed   = 0;
static uint32_t         s_playCursor   = 0;
static int              s_alignInit    = 0;
static uint32_t         s_baseGlobal   = 0;
static uint32_t         s_baseTick     = 0;
static int              s_diverged     = 0;
static int              s_finished     = 0;

static void tapeRecordFunc(struct contsample *samples, s32 curstart, s32 curlast);
static s32  tapePlaybackFunc(struct contsample *samples, s32 curlast);

void inputTapeConfigureRecord(const char *path)   { s_recordPath = path; }
void inputTapeConfigurePlayback(const char *path) { s_playPath   = path; }
int  inputTapeIsRecordingRequested(void) { return s_recordPath != NULL; }
int  inputTapeIsPlaybackRequested(void)  { return s_playPath   != NULL; }

void inputTapeSetSessionParams(int level_id, int difficulty) {
    s_sessionLevel      = level_id;
    s_sessionDifficulty = difficulty;
}

/* Close the recording writer at process exit (flushes the file). */
static void inputTapeFinishRecording(void) {
    if (s_writer) {
        InputTapeWriter *w = s_writer;
        s_writer = NULL;
        if (inputTapeWriterClose(w) != 0) {
            fprintf(stderr, "[INPUT-TAPE] ERROR: failed to flush recording to %s\n",
                    s_recordPath ? s_recordPath : "(null)");
        } else {
            fprintf(stderr, "[INPUT-TAPE] recording flushed to %s\n",
                    s_recordPath ? s_recordPath : "(null)");
        }
    }
}

/* End of tape reached during playback: emit the sim hash and exit cleanly. */
static void inputTapeFinishPlayback(void) {
    if (s_finished) {
        return;
    }
    s_finished = 1;
    fprintf(stderr, "[INPUT-TAPE] playback complete: %u ticks consumed%s\n",
            s_playCursor, s_diverged ? " (TICK MISALIGNMENT DETECTED)" : "");
    fflush(stdout);
    simStateHashEmitIfRequested((int)(uint32_t)g_GlobalTimer, "tape");
    fflush(stderr);
    /* Non-zero exit on divergence so tape_regression.sh treats it as a failure. */
    exit(s_diverged ? 4 : 0);
}

void inputTapeInstallHooks(void) {
    /* Recording. */
    if (s_recordPath && !s_writerFailed) {
        if (!s_writer) {
            InputTapeHeader hdr;
            memset(&hdr, 0, sizeof hdr);
            memcpy(hdr.magic, GE7TAPE_MAGIC, GE7TAPE_MAGIC_LEN);
            hdr.version          = GE7TAPE_VERSION;
            hdr.level_id         = (uint32_t)s_sessionLevel;
            hdr.difficulty       = (uint32_t)s_sessionDifficulty;
            hdr.rng_seed         = GE7TAPE_DETERMINISTIC_SEED;
            hdr.tick_hz          = GE7TAPE_TICK_HZ;
            hdr.flags            = 0;
            hdr.num_players      = (uint32_t)MAXCONTROLLERS;
            {
                s8 cc = joyGetControllerCount();
                hdr.controller_count = (cc > 0) ? (uint32_t)cc : 1u;
            }
            s_writer = inputTapeWriterOpen(s_recordPath, &hdr);
            if (!s_writer) {
                /* AUDIT-0061: fail closed. An explicit --record-tape whose
                 * destination cannot be created (unwritable dir, read-only fs)
                 * must abort now -- before any gameplay -- rather than run to
                 * completion and silently produce no tape. inputTapeWriterOpen
                 * already probed the destination, so this catches it up front,
                 * symmetric with the --play-tape failures below. */
                s_writerFailed = 1;
                fprintf(stderr, "[INPUT-TAPE] ERROR: cannot open %s for recording\n",
                        s_recordPath);
                fflush(stderr);
                exit(2);
            }
            atexit(inputTapeFinishRecording);
            fprintf(stderr,
                    "[INPUT-TAPE] recording -> %s (level=%u diff=%u seed=0x%08llX controllers=%u)\n",
                    s_recordPath, hdr.level_id, hdr.difficulty,
                    (unsigned long long)hdr.rng_seed, hdr.controller_count);
        }
        /* Re-arm if joyInit cleared the pointer. */
        if (s_writer && g_ContRecordFunc != tapeRecordFunc) {
            joySetRecordFunc(tapeRecordFunc);
        }
    }

    /* Playback. */
    if (s_playPath && !s_tapeFailed) {
        if (!s_tape) {
            s_tape = inputTapeRead(s_playPath);
            if (!s_tape) {
                /* AUDIT-0064: fail closed. An explicit --play-tape that is
                 * missing/unreadable/short/malformed must exit nonzero, NOT fall
                 * back to live/ordinary input (which would run a different,
                 * silently-wrong session). Symmetric with the validation
                 * failures below. */
                s_tapeFailed = 1;
                fprintf(stderr, "[INPUT-TAPE] ERROR: cannot read tape %s\n", s_playPath);
                fflush(stderr);
                exit(2);
            }
            /* Determinism-seed assertion: a tape must be recorded under the
             * fixed-seed envelope and replayed under it too. */
            if (!g_deterministic) {
                fprintf(stderr,
                        "[INPUT-TAPE] ERROR: --play-tape requires --deterministic\n");
                exit(2);
            }
            if (s_tape->header.rng_seed != GE7TAPE_DETERMINISTIC_SEED) {
                fprintf(stderr,
                        "[INPUT-TAPE] ERROR: tape seed 0x%016llX != deterministic seed 0x%016llX\n",
                        (unsigned long long)s_tape->header.rng_seed,
                        (unsigned long long)GE7TAPE_DETERMINISTIC_SEED);
                exit(2);
            }
            if (s_sessionLevel >= 0 &&
                s_tape->header.level_id != (uint32_t)s_sessionLevel) {
                fprintf(stderr,
                        "[INPUT-TAPE] ERROR: tape level %u != requested --level %d\n",
                        s_tape->header.level_id, s_sessionLevel);
                exit(2);
            }
            /* AUDIT-0063: reject a tape whose session metadata is incompatible
             * with this run instead of replaying it against a mismatched sim.
             * Difficulty changes AI/spawns; a foreign tick rate or unknown flag
             * bit means the stream was authored under semantics this build does
             * not implement; controller_count is handed straight to the sim. */
            if (s_sessionDifficulty >= 0 &&
                s_tape->header.difficulty != (uint32_t)s_sessionDifficulty) {
                fprintf(stderr,
                        "[INPUT-TAPE] ERROR: tape difficulty %u != requested difficulty %d\n",
                        s_tape->header.difficulty, s_sessionDifficulty);
                exit(2);
            }
            if (s_tape->header.tick_hz != GE7TAPE_TICK_HZ) {
                fprintf(stderr,
                        "[INPUT-TAPE] ERROR: tape tick_hz %u != engine tick_hz %u\n",
                        s_tape->header.tick_hz, (unsigned)GE7TAPE_TICK_HZ);
                exit(2);
            }
            if (s_tape->header.flags != 0u) {
                fprintf(stderr,
                        "[INPUT-TAPE] ERROR: tape declares unsupported flags 0x%08X\n",
                        s_tape->header.flags);
                exit(2);
            }
            if (s_tape->header.controller_count == 0u ||
                s_tape->header.controller_count > s_tape->header.num_players) {
                fprintf(stderr,
                        "[INPUT-TAPE] ERROR: tape controller_count %u out of range (1..%u)\n",
                        s_tape->header.controller_count, s_tape->header.num_players);
                exit(2);
            }
            fprintf(stderr,
                    "[INPUT-TAPE] playback <- %s (level=%u diff=%u ticks=%u controllers=%u)\n",
                    s_playPath, s_tape->header.level_id, s_tape->header.difficulty,
                    s_tape->header.tick_count, s_tape->header.controller_count);
        }
        if (s_tape && g_ContPlaybackFunc != tapePlaybackFunc) {
            joySetPlaybackFunc(tapePlaybackFunc, (s32)s_tape->header.controller_count);
        }
    }
}

/*
 * Record hook — mirrors record_player_input_as_packet's ring-buffer traversal:
 * capture samples (curstart+1 .. curlast) inclusive, one tape record per sample,
 * tick = g_GlobalTimer (the same value the playback side aligns against).
 */
static void tapeRecordFunc(struct contsample *samples, s32 curstart, s32 curlast) {
    s32 samplenum;

    if (!s_writer || samples == NULL) {
        return;
    }
    if (curstart == curlast) {
        return;  /* no new samples this frame */
    }

    samplenum = (s32)(((u32)curstart + 1u) % (u32)CONTSAMPLE_LEN);
    while (1) {
        InputTapePad pads[MAXCONTROLLERS];
        s32 p;
        for (p = 0; p < MAXCONTROLLERS; p++) {
            pads[p].button  = samples[samplenum].pads[p].button;
            pads[p].stick_x = samples[samplenum].pads[p].stick_x;
            pads[p].stick_y = samples[samplenum].pads[p].stick_y;
        }
        if (inputTapeWriterAppendTick(s_writer, (uint32_t)g_GlobalTimer,
                                      pads, (uint32_t)MAXCONTROLLERS) != 0) {
            fprintf(stderr, "[INPUT-TAPE] ERROR: append failed; stopping recording\n");
            inputTapeFinishRecording();
            s_writerFailed = 1;
            joySetRecordFunc(NULL);
            return;
        }
        if (samplenum == curlast) {
            break;
        }
        samplenum = (s32)(((u32)samplenum + 1u) % (u32)CONTSAMPLE_LEN);
    }
}

/*
 * Playback hook — mirrors ramrom_replay_handler: fill one new sample into the
 * PLAYBACK ring at (curlast+1)%LEN, switch the active contdata index to PLAYBACK
 * so the sim reads the tape'd input, and return the advanced index. Emits the
 * sim hash and exits when the tape is exhausted.
 */
static s32 tapePlaybackFunc(struct contsample *samples, s32 curlast) {
    s32 index;
    uint32_t cur;
    s32 p;

    if (!s_tape || samples == NULL) {
        return curlast;
    }
    if (s_playCursor >= s_tape->header.tick_count) {
        inputTapeFinishPlayback();
        return curlast;
    }

    cur = s_playCursor;

    /* Tick alignment: relative to the first record, robust to a constant phase
     * offset in g_GlobalTimer. Under determinism the sim advances one tick per
     * frame in both record and playback runs, so this must match exactly. */
    if (!s_alignInit) {
        s_baseGlobal = (uint32_t)g_GlobalTimer;
        s_baseTick   = s_tape->ticks[0];
        s_alignInit  = 1;
    }
    {
        uint32_t expect_rel = s_tape->ticks[cur] - s_baseTick;
        uint32_t actual_rel = (uint32_t)g_GlobalTimer - s_baseGlobal;
        if (expect_rel != actual_rel && !s_diverged) {
            s_diverged = 1;
            fprintf(stderr,
                    "[INPUT-TAPE] WARNING: tick misalignment at record %u "
                    "(expected rel %u, got %u; g_GlobalTimer=%d)\n",
                    cur, expect_rel, actual_rel, (int)g_GlobalTimer);
        }
    }

    index = (s32)(((u32)curlast + 1u) % (u32)CONTSAMPLE_LEN);
    for (p = 0; p < MAXCONTROLLERS; p++) {
        uint16_t btn = 0;
        int8_t   sx  = 0;
        int8_t   sy  = 0;
        if ((uint32_t)p < s_tape->header.num_players) {
            const InputTapePad *tp =
                &s_tape->pads[(size_t)cur * s_tape->header.num_players + p];
            btn = tp->button;
            sx  = tp->stick_x;
            sy  = tp->stick_y;
        }
        samples[index].pads[p].button  = btn;
        samples[index].pads[p].stick_x = sx;
        samples[index].pads[p].stick_y = sy;
        samples[index].pads[p].errnum  = 0;
    }
    s_playCursor++;

    /* Make the sim read the tape'd PLAYBACK contdata (ramrom does the same). */
    joySetContDataIndex(1);
    return index;
}

#endif /* NATIVE_PORT && !INPUT_TAPE_UNIT_TEST */

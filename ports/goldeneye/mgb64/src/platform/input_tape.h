/*
 * PC input tape — disk record/replay of controller sample streams (FID-0034).
 *
 * A .ge7tape is a byte-exact capture of the OSContPad samples the game consumed
 * during a deterministic session, so an arbitrary play session can be replayed
 * as a regression fixture. It builds on the retail hook points
 * (g_ContRecordFunc / g_ContPlaybackFunc, joy.c:111-112) exactly as the RAMROM
 * demo path does (ramromreplay.c:1494-1496 is the substitution-point reference)
 * — it is NOT a parallel input path.
 *
 * This header splits into two halves:
 *   1. The pure .ge7tape reader/writer (ROM-free, stdio/stdint only). This is
 *      what tests/test_input_tape.c exercises — it never pulls in game headers.
 *   2. The game-side glue (record/playback hook install), compiled only into the
 *      engine under NATIVE_PORT.
 */
#ifndef INPUT_TAPE_H
#define INPUT_TAPE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* On-disk magic (8 bytes, NOT NUL-terminated on disk). */
#define GE7TAPE_MAGIC       "GE7TAPE1"
#define GE7TAPE_MAGIC_LEN   8
#define GE7TAPE_VERSION     1u
#define GE7TAPE_TICK_HZ     60u

/*
 * Fixed deterministic RNG seed. The engine reseeds to this under --deterministic
 * (boss.c: randomSetSeed(0x12345678)); a tape recorded under that envelope pins
 * the same value in its header, and playback refuses to run unless it matches.
 */
#define GE7TAPE_DETERMINISTIC_SEED  0x12345678ULL

/* One controller's post-binding sample (the captured OSContPad, minus errnum). */
typedef struct {
    uint16_t button;
    int8_t   stick_x;
    int8_t   stick_y;
} InputTapePad;

/*
 * File header. `num_players` is the pad-stride of each per-tick record;
 * `controller_count` is the value joyGetControllerCount() returned during
 * recording (fed back to joySetPlaybackFunc so playback reports the identical
 * controller count to the sim). `tick_count` is patched at writer close.
 */
typedef struct {
    char     magic[GE7TAPE_MAGIC_LEN];
    uint32_t version;
    uint32_t level_id;
    uint32_t difficulty;
    uint64_t rng_seed;
    uint32_t tick_hz;
    uint32_t flags;
    uint32_t num_players;
    uint32_t controller_count;
    uint32_t tick_count;
} InputTapeHeader;

/* -------- Writer (buffers records in memory, serialises on close) -------- */

typedef struct InputTapeWriter InputTapeWriter;

/*
 * Open a writer bound to `path`. `hdr` supplies every header field except
 * tick_count, which the writer tracks itself. Returns NULL on allocation
 * failure or invalid header (bad magic / num_players out of range).
 */
InputTapeWriter *inputTapeWriterOpen(const char *path, const InputTapeHeader *hdr);

/* Append one tick record. `pads` must hold at least `num_players` entries and
 * `num_players` must match the header. Returns 0 on success, non-zero on error. */
int inputTapeWriterAppendTick(InputTapeWriter *w, uint32_t tick,
                              const InputTapePad *pads, uint32_t num_players);

/* Flush the buffered header + records to disk and free the writer.
 * Returns 0 on success, non-zero on I/O error. Safe to call on NULL (no-op). */
int inputTapeWriterClose(InputTapeWriter *w);

/* -------- Reader (loads the whole tape into memory) -------- */

typedef struct {
    InputTapeHeader header;
    uint32_t       *ticks;  /* [header.tick_count]                          */
    InputTapePad   *pads;   /* [header.tick_count * header.num_players]      */
} InputTape;

/* Load and validate a .ge7tape. Returns NULL on open/parse/validation error. */
InputTape *inputTapeRead(const char *path);
void inputTapeFree(InputTape *t);

#ifdef NATIVE_PORT
/* -------- Game-side glue (engine only) -------- */

/* Stash a request from arg parse; the actual open/install is deferred to the
 * first joyConsumeSamplesWrapper call (after joyInit). */
void inputTapeConfigureRecord(const char *path);
void inputTapeConfigurePlayback(const char *path);

/* Called from main_pc.c once the boot level/difficulty are resolved, so a
 * recorded tape carries the true header fields. */
void inputTapeSetSessionParams(int level_id, int difficulty);

int  inputTapeIsRecordingRequested(void);
int  inputTapeIsPlaybackRequested(void);

/* Idempotent hook (re)install; called from joy.c under NATIVE_PORT every frame.
 * Re-arms the g_Cont* function pointers if joyInit cleared them. */
void inputTapeInstallHooks(void);
#endif /* NATIVE_PORT */

#ifdef __cplusplus
}
#endif

#endif /* INPUT_TAPE_H */

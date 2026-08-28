/*
 * ROM-free unit test for the .ge7tape reader/writer (FID-0034, Task 0.6 Step 1).
 *
 * Writes a synthetic 600-tick tape through the writer API, reads it back, and
 * asserts a byte-exact roundtrip of both the header and every per-tick record.
 * Then (AUDIT-0065) crafts size-mismatched files and asserts the reader rejects
 * them rather than allocating from the unverified header count.
 *
 * Uses an explicit CHECK counter, NOT assert(): the ctest build is -DNDEBUG
 * (Release), where assert() is compiled out — a test whose logic lives inside
 * assert() would silently run nothing and pass. No game headers, no ROM.
 */
#include "input_tape.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_TICKS       600u
#define TEST_PLAYERS     4u
#define TEST_FIRST_TICK  17u   /* start g_GlobalTimer-style tick at a nonzero base */

static int g_fails = 0;
#define CHECK(name, cond) do { \
    if (!(cond)) { printf("FAIL: %s\n", (name)); ++g_fails; } \
} while (0)

/* Deterministic synthetic pad for (tick, player) — spans the full u16/s8 range. */
static InputTapePad synth_pad(uint32_t t, uint32_t p) {
    InputTapePad pad;
    pad.button  = (uint16_t)((t * 131u + p * 7919u) & 0xFFFFu);
    pad.stick_x = (int8_t)((int)((t * 3u + p * 11u) & 0xFF) - 128);
    pad.stick_y = (int8_t)((int)((t * 5u + p * 13u) & 0xFF) - 128);
    return pad;
}

/* Read an entire file into a fresh buffer (or NULL); *out_len gets the length. */
static unsigned char *slurp(const char *path, long *out_len) {
    long n;
    unsigned char *buf;
    FILE *f = fopen(path, "rb");
    *out_len = 0;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
        *out_len = n;
    } else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

static void write_bytes(const char *path, const unsigned char *buf, size_t len) {
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(buf, 1, len, f); fclose(f); }
}

int main(void) {
    char path[64];
    snprintf(path, sizeof path, "test_input_tape_%d.ge7tape", (int)0);

    InputTapeHeader hdr;
    memset(&hdr, 0, sizeof hdr);
    memcpy(hdr.magic, GE7TAPE_MAGIC, GE7TAPE_MAGIC_LEN);
    hdr.version          = GE7TAPE_VERSION;
    hdr.level_id         = 0x22;        /* Dam LEVELID */
    hdr.difficulty       = 1;
    hdr.rng_seed         = GE7TAPE_DETERMINISTIC_SEED;
    hdr.tick_hz          = GE7TAPE_TICK_HZ;
    hdr.flags            = 0;
    hdr.num_players      = TEST_PLAYERS;
    hdr.controller_count = 1;
    /* tick_count intentionally left 0; the writer owns it. */

    /* ---- Write ---- */
    InputTapeWriter *w = inputTapeWriterOpen(path, &hdr);
    CHECK("writer open", w != NULL);
    if (w) {
        for (uint32_t t = 0; t < TEST_TICKS; t++) {
            InputTapePad pads[TEST_PLAYERS];
            for (uint32_t p = 0; p < TEST_PLAYERS; p++) pads[p] = synth_pad(t, p);
            CHECK("append tick", inputTapeWriterAppendTick(w, TEST_FIRST_TICK + t, pads, TEST_PLAYERS) == 0);
        }
        CHECK("writer close", inputTapeWriterClose(w) == 0);
    }

    /* Atomic writer must not leave a temp file behind (AUDIT-0062). */
    {
        char tmp[80];
        FILE *leftover;
        snprintf(tmp, sizeof tmp, "%s.tmp", path);
        leftover = fopen(tmp, "rb");
        CHECK("no leftover .tmp after close", leftover == NULL);
        if (leftover) { fclose(leftover); remove(tmp); }
    }

    /* ---- Read back ---- */
    InputTape *tape = inputTapeRead(path);
    CHECK("read back", tape != NULL);
    if (tape) {
        CHECK("magic", memcmp(tape->header.magic, GE7TAPE_MAGIC, GE7TAPE_MAGIC_LEN) == 0);
        CHECK("version", tape->header.version == GE7TAPE_VERSION);
        CHECK("level_id", tape->header.level_id == 0x22u);
        CHECK("difficulty", tape->header.difficulty == 1u);
        CHECK("rng_seed", tape->header.rng_seed == GE7TAPE_DETERMINISTIC_SEED);
        CHECK("tick_hz", tape->header.tick_hz == GE7TAPE_TICK_HZ);
        CHECK("flags", tape->header.flags == 0u);
        CHECK("num_players", tape->header.num_players == TEST_PLAYERS);
        CHECK("controller_count", tape->header.controller_count == 1u);
        CHECK("tick_count", tape->header.tick_count == TEST_TICKS);

        int records_ok = 1;
        for (uint32_t t = 0; t < TEST_TICKS && records_ok; t++) {
            if (tape->ticks[t] != TEST_FIRST_TICK + t) records_ok = 0;
            for (uint32_t p = 0; p < TEST_PLAYERS && records_ok; p++) {
                InputTapePad expect = synth_pad(t, p);
                InputTapePad got    = tape->pads[t * TEST_PLAYERS + p];
                if (got.button != expect.button || got.stick_x != expect.stick_x ||
                    got.stick_y != expect.stick_y) records_ok = 0;
            }
        }
        CHECK("record roundtrip byte-exact", records_ok);
        inputTapeFree(tape);
    }

    /* ---- AUDIT-0065: reject a file whose size does not match its header count.
     * A well-formed tape is exactly header + tick_count*reclen bytes; a truncated
     * body or trailing garbage is corrupt and must be rejected before allocating
     * from the unverified count. ---- */
    {
        long full = 0;
        unsigned char *buf = slurp(path, &full);
        CHECK("slurp valid tape", buf != NULL && full > 0);
        if (buf) {
            size_t reclen = 4u + (size_t)TEST_PLAYERS * 4u;
            unsigned char *big = (unsigned char *)malloc((size_t)full + 32);
            /* (a) over-sized: valid tape + trailing garbage -> reject. */
            memcpy(big, buf, (size_t)full);
            memset(big + full, 0xAB, 32);
            write_bytes("test_input_tape_oversize.ge7tape", big, (size_t)full + 32);
            InputTape *over = inputTapeRead("test_input_tape_oversize.ge7tape");
            CHECK("reject over-sized tape", over == NULL);
            inputTapeFree(over);
            remove("test_input_tape_oversize.ge7tape");
            /* (b) under-sized: body truncated by one record -> reject. */
            write_bytes("test_input_tape_undersize.ge7tape", buf, (size_t)full - reclen);
            InputTape *under = inputTapeRead("test_input_tape_undersize.ge7tape");
            CHECK("reject under-sized tape", under == NULL);
            inputTapeFree(under);
            remove("test_input_tape_undersize.ge7tape");
            free(big);
            free(buf);
        }
    }

    remove(path);

    if (g_fails == 0) {
        printf("test_input_tape: OK (%u ticks, %u players, roundtrip byte-exact; "
               "size-mismatch rejection)\n", TEST_TICKS, TEST_PLAYERS);
        return 0;
    }
    printf("test_input_tape: %d failure(s)\n", g_fails);
    return 1;
}

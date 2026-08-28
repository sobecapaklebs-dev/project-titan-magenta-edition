/*
 * test_screenshot_series.c — ROM-free / GPU-free cadence + write-contract lane
 * for the screenshot-series diagnostic (AUDIT-0003).
 *
 * The diagnostic's decision logic (AFTER_FRAME / EVERY / LIMIT / room-filter
 * accounting) and its P6 write (filename pattern, bottom-left->top-left row flip,
 * write-only-on-durable-file contract, written-count) were extracted from a
 * static function in the 25k-line gfx_pc.c into src/platform/fast3d/screenshot_series.c
 * so they can be exercised against a mocked readback. This test drives that TU
 * directly with deterministic synthetic pixels and a controllable success/failure
 * mock, and asserts:
 *
 *   (a) AFTER_FRAME=10 EVERY=3 LIMIT=4 captures at frames 10,13,16,19 and none
 *       after (cadence + limit exact).
 *   (b) the room filter (GE007_SCREENSHOT_SERIES_ROOM) is honored.
 *   (c) written-count increments only on a durable full file, and the file is a
 *       valid P6 of the expected byte size with the vertical flip applied (the
 *       mock encodes the source row index into pixel red, so the file's TOP row
 *       must carry the source BOTTOM row's marker).
 *   (d) a failed readback writes no file, does not increment the counter, and
 *       does not block later (successful) frames.
 *   (e) once the limit is reached the readback is not even invoked.
 *
 * CHECK-counter (no assert(): ctest builds are Release -DNDEBUG, which strips
 * assert()).
 */
/* mkdtemp / setenv require the POSIX/BSD surface on some libcs. */
#define _DARWIN_C_SOURCE 1
#define _DEFAULT_SOURCE 1
#define _XOPEN_SOURCE 700

#include "screenshot_series.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

/* ---- Mock readback ------------------------------------------------------- */

typedef struct {
    int calls;       /* how many times the readback was invoked */
    int fail;        /* if nonzero, return false (simulate readback failure) */
} mock_ctx;

/*
 * Deterministic synthetic pixels: red channel = source row index (mod 256),
 * green = source column index (mod 256), blue = 0x40. This makes both the row
 * flip AND row/col orientation verifiable from the written file.
 */
static bool mock_readback(int x, int y, int width, int height,
                          uint8_t *rgb_out, void *ctx)
{
    mock_ctx *m = (mock_ctx *)ctx;
    (void)x; (void)y;
    m->calls++;
    if (m->fail) {
        return false;
    }
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            uint8_t *px = rgb_out + ((size_t)row * (size_t)width + (size_t)col) * 3;
            px[0] = (uint8_t)(row & 0xFF);
            px[1] = (uint8_t)(col & 0xFF);
            px[2] = 0x40;
        }
    }
    return true;
}

/* ---- Helpers ------------------------------------------------------------- */

static char g_dir[512];

static void make_tmpdir(void)
{
    char tmpl[512];
    const char *base = getenv("TMPDIR");
    if (base == NULL || base[0] == '\0') {
        base = "/tmp";
    }
    snprintf(tmpl, sizeof(tmpl), "%s/ss_series_XXXXXX", base);
    char *p = mkdtemp(tmpl);
    if (p == NULL) {
        fprintf(stderr, "FATAL: mkdtemp failed\n");
        exit(2);
    }
    snprintf(g_dir, sizeof(g_dir), "%s", p);
}

static void set_env(const char *after, const char *every, const char *limit,
                    const char *room, const char *prefix)
{
    setenv("GE007_SCREENSHOT_SERIES_DIR", g_dir, 1);
    if (prefix) setenv("GE007_SCREENSHOT_SERIES_PREFIX", prefix, 1);
    else        unsetenv("GE007_SCREENSHOT_SERIES_PREFIX");
    if (room)   setenv("GE007_SCREENSHOT_SERIES_ROOM", room, 1);
    else        unsetenv("GE007_SCREENSHOT_SERIES_ROOM");
    if (after)  setenv("GE007_SCREENSHOT_SERIES_AFTER_FRAME", after, 1);
    else        unsetenv("GE007_SCREENSHOT_SERIES_AFTER_FRAME");
    if (every)  setenv("GE007_SCREENSHOT_SERIES_EVERY", every, 1);
    else        unsetenv("GE007_SCREENSHOT_SERIES_EVERY");
    if (limit)  setenv("GE007_SCREENSHOT_SERIES_LIMIT", limit, 1);
    else        unsetenv("GE007_SCREENSHOT_SERIES_LIMIT");
}

/* Build the expected file path for a given prefix+frame. */
static void series_path(char *out, size_t cap, const char *prefix, int frame)
{
    snprintf(out, cap, "%s/%s_f%06d.ppm", g_dir, prefix ? prefix : "capture", frame);
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* ---- Tests --------------------------------------------------------------- */

/* (a) cadence + (c) durable-file/flip + (e) limit-stops-readback */
static void test_cadence_and_file(void)
{
    make_tmpdir();
    set_env("10", "3", "4", NULL, "cap");

    screenshot_series_state st;
    memset(&st, 0, sizeof(st));
    mock_ctx m = {0, 0};

    const int W = 8, H = 6;
    int captured_frames[64];
    int n_captured = 0;

    for (int f = 0; f < 40; f++) {
        bool wrote = screenshot_series_capture_if_due(&st, f, 0, W, H,
                                                      mock_readback, &m);
        if (wrote) {
            captured_frames[n_captured++] = f;
        }
    }

    /* after=10 every=3 limit=4 -> 10,13,16,19 */
    CHECK(n_captured == 4, "cadence: exactly 4 captures");
    if (n_captured == 4) {
        CHECK(captured_frames[0] == 10, "cadence: first at frame 10");
        CHECK(captured_frames[1] == 13, "cadence: second at frame 13");
        CHECK(captured_frames[2] == 16, "cadence: third at frame 16");
        CHECK(captured_frames[3] == 19, "cadence: fourth at frame 19");
    }
    CHECK(st.written == 4, "cadence: written counter == 4");

    /* (e) once limit hit, the readback is not invoked for later due frames.
     * Due frames in [0,40): 10,13,16,19,22,25,28,31,34,37 = 10 total; only the
     * first 4 should have called the mock. */
    CHECK(m.calls == 4, "limit: readback invoked exactly 4 times (not on frames >= limit)");

    /* (c) verify one captured file is a valid P6 of the expected size with the
     * vertical flip applied. File top row must carry source row (H-1) marker. */
    char path[600];
    series_path(path, sizeof(path), "cap", 13);
    FILE *fp = fopen(path, "rb");
    CHECK(fp != NULL, "file: frame-13 PPM exists and opens");
    if (fp) {
        int hw = 0, hh = 0, maxv = 0;
        int matched = fscanf(fp, "P6 %d %d %d", &hw, &hh, &maxv);
        CHECK(matched == 3, "file: P6 header parses");
        CHECK(hw == W && hh == H, "file: header dimensions match");
        CHECK(maxv == 255, "file: maxval is 255");
        /* Consume the single whitespace byte after maxval before the raster. */
        fgetc(fp);
        uint8_t first_px[3];
        size_t got = fread(first_px, 1, 3, fp);
        CHECK(got == 3, "file: first pixel readable");
        /* Rows are written bottom-to-top, so the file's first (top) row is the
         * source's bottom row H-1. Red channel encodes source row index. */
        CHECK(first_px[0] == (uint8_t)((H - 1) & 0xFF), "flip: top file row == source bottom row");
        CHECK(first_px[1] == 0, "flip: first column marker == 0");
        CHECK(first_px[2] == 0x40, "file: blue marker preserved");
        fclose(fp);

        /* Expected total byte size = header length + W*H*3 raster. */
        char hdr[64];
        int hdr_len = snprintf(hdr, sizeof(hdr), "P6\n%d %d\n255\n", W, H);
        struct stat sb;
        if (stat(path, &sb) == 0) {
            CHECK((size_t)sb.st_size == (size_t)hdr_len + (size_t)W * H * 3,
                  "file: total byte size == header + raster");
        } else {
            CHECK(0, "file: stat succeeds");
        }
    }

    /* Frames past the limit must not have produced files. */
    series_path(path, sizeof(path), "cap", 22);
    CHECK(!file_exists(path), "limit: frame-22 (past limit) produced no file");
}

/* (b) room filter honored */
static void test_room_filter(void)
{
    make_tmpdir();
    set_env("0", "1", "0", "5", "rm");  /* limit=0 -> unbounded; only room 5 */

    screenshot_series_state st;
    memset(&st, 0, sizeof(st));
    mock_ctx m = {0, 0};

    const int W = 4, H = 4;

    /* room 5 -> due every frame; room 7 -> filtered out. */
    bool a = screenshot_series_capture_if_due(&st, 0, 5, W, H, mock_readback, &m);
    bool b = screenshot_series_capture_if_due(&st, 1, 7, W, H, mock_readback, &m);
    bool c = screenshot_series_capture_if_due(&st, 2, 5, W, H, mock_readback, &m);

    CHECK(a, "room: room 5 frame 0 captured");
    CHECK(!b, "room: room 7 frame 1 filtered out");
    CHECK(c, "room: room 5 frame 2 captured");
    CHECK(st.written == 2, "room: exactly 2 writes");
    CHECK(m.calls == 2, "room: filtered frame never invoked readback");

    char path[600];
    series_path(path, sizeof(path), "rm", 1);
    CHECK(!file_exists(path), "room: filtered frame produced no file");
}

/* (d) failed readback writes no file, no counter, later frames still try */
static void test_readback_failure(void)
{
    make_tmpdir();
    set_env("0", "1", "0", NULL, "fail");

    screenshot_series_state st;
    memset(&st, 0, sizeof(st));
    mock_ctx m = {0, 1};  /* fail = 1 */

    const int W = 4, H = 4;

    bool r0 = screenshot_series_capture_if_due(&st, 0, 0, W, H, mock_readback, &m);
    CHECK(!r0, "fail: failed readback returns false");
    CHECK(st.written == 0, "fail: no counter increment on failed readback");
    CHECK(m.calls == 1, "fail: readback was invoked (frame was due)");

    char path[600];
    series_path(path, sizeof(path), "fail", 0);
    CHECK(!file_exists(path), "fail: no partial file written");

    /* Now let readback succeed on a later frame; it must still be attempted. */
    m.fail = 0;
    bool r1 = screenshot_series_capture_if_due(&st, 1, 0, W, H, mock_readback, &m);
    CHECK(r1, "fail: later successful frame still captured");
    CHECK(st.written == 1, "fail: later success increments counter");
    CHECK(m.calls == 2, "fail: readback attempted again after earlier failure");
}

int main(void)
{
    test_cadence_and_file();
    test_room_filter();
    test_readback_failure();

    if (g_failures == 0) {
        printf("test_screenshot_series: PASS\n");
        return 0;
    }
    fprintf(stderr, "test_screenshot_series: %d FAILURE(S)\n", g_failures);
    return 1;
}

/**
 * test_rom_validation.c -- Standalone test harness for game_validate_rom().
 *
 * Generates synthetic ROM files in /tmp, exercises every code path in
 * game_validate_rom(), and reports results in TAP (Test Anything Protocol)
 * format.
 *
 * Build:  see run_tests.sh
 * Output: TAP format -- "1..N" header, "ok/not ok" per test, exit 0/1.
 */

#include "GameBridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void gameBridgeConsumeInput(GameInputState *out);

/* ========================================================================
 * TAP harness
 * ======================================================================== */

static int g_test_num   = 0;
static int g_test_fail  = 0;

#define TAP_TOTAL 22

static void tap_ok(int pass, const char *desc) {
    g_test_num++;
    if (pass) {
        printf("ok %d - %s\n", g_test_num, desc);
    } else {
        printf("not ok %d - %s\n", g_test_num, desc);
        g_test_fail++;
    }
}

/* ========================================================================
 * Temporary file management
 * ======================================================================== */

#define MAX_TMP_FILES 16
static char g_tmp_paths[MAX_TMP_FILES][256];
static int  g_tmp_count = 0;

static void cleanup_tmp_files(void) {
    for (int i = 0; i < g_tmp_count; i++) {
        unlink(g_tmp_paths[i]);
    }
}

static const char *tmp_path(const char *name) {
    if (g_tmp_count >= MAX_TMP_FILES) {
        fprintf(stderr, "FATAL: too many tmp files\n");
        exit(2);
    }
    snprintf(g_tmp_paths[g_tmp_count], sizeof(g_tmp_paths[0]),
             "/tmp/ge007_test_%s", name);
    return g_tmp_paths[g_tmp_count++];
}

/* ========================================================================
 * Synthetic ROM generation
 *
 * We create a 12 MB buffer with a recognizable header and deterministic
 * fill data, then write it in three byte orders (.z64, .v64, .n64).
 * ======================================================================== */

#define ROM_SIZE (12 * 1024 * 1024)  /* 12 MB -- comfortably valid */

/** Big-endian (.z64) header bytes: 80 37 12 40 */
static const uint8_t Z64_MAGIC[4] = { 0x80, 0x37, 0x12, 0x40 };

static uint8_t *make_z64_data(void) {
    uint8_t *buf = calloc(1, ROM_SIZE);
    if (!buf) { perror("calloc"); exit(2); }
    /* Write .z64 magic header */
    memcpy(buf, Z64_MAGIC, 4);
    /* Fill the rest with a simple pattern so SHA1 is deterministic */
    for (size_t i = 4; i < ROM_SIZE; i++) {
        buf[i] = (uint8_t)(i * 7 + 13);
    }
    return buf;
}

static void write_file(const char *path, const uint8_t *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    if (fwrite(data, 1, len, f) != len) { perror("fwrite"); exit(2); }
    fclose(f);
}

/** Byte-swap pairs in-place: .z64 -> .v64 */
static void byteswap_v64(uint8_t *buf, size_t len) {
    for (size_t i = 0; i + 1 < len; i += 2) {
        uint8_t tmp = buf[i];
        buf[i]     = buf[i + 1];
        buf[i + 1] = tmp;
    }
}

/** Word-swap in-place: .z64 -> .n64 (little-endian 32-bit words) */
static void byteswap_n64(uint8_t *buf, size_t len) {
    for (size_t i = 0; i + 3 < len; i += 4) {
        uint8_t t0 = buf[i], t1 = buf[i + 1];
        buf[i]     = buf[i + 3];
        buf[i + 1] = buf[i + 2];
        buf[i + 2] = t1;
        buf[i + 3] = t0;
    }
}

/* ========================================================================
 * Test cases
 * ======================================================================== */

static void test_z64_byte_order(const char *z64_path) {
    GameROMInfo info = game_validate_rom(z64_path);
    tap_ok(strcmp(info.byte_order, "z64") == 0,
           ".z64 file detected as byte_order=\"z64\"");
}

static void test_v64_byte_order(const char *v64_path) {
    GameROMInfo info = game_validate_rom(v64_path);
    tap_ok(strcmp(info.byte_order, "v64") == 0,
           ".v64 file detected as byte_order=\"v64\"");
}

static void test_n64_byte_order(const char *n64_path) {
    GameROMInfo info = game_validate_rom(n64_path);
    tap_ok(strcmp(info.byte_order, "n64") == 0,
           ".n64 file detected as byte_order=\"n64\"");
}

static void test_sha1_consistency(const char *z64_path,
                                  const char *v64_path,
                                  const char *n64_path) {
    GameROMInfo z = game_validate_rom(z64_path);
    GameROMInfo v = game_validate_rom(v64_path);
    GameROMInfo n = game_validate_rom(n64_path);

    int z_v_match = (strcmp(z.sha1_hex, v.sha1_hex) == 0);
    int z_n_match = (strcmp(z.sha1_hex, n.sha1_hex) == 0);
    int all_match = z_v_match && z_n_match;

    if (!all_match) {
        fprintf(stderr, "  # SHA1 mismatch:\n");
        fprintf(stderr, "  #   z64: %s\n", z.sha1_hex);
        fprintf(stderr, "  #   v64: %s\n", v.sha1_hex);
        fprintf(stderr, "  #   n64: %s\n", n.sha1_hex);
    }
    tap_ok(all_match,
           "z64/v64/n64 of same data produce identical SHA1");
}

static void test_valid_size(const char *z64_path) {
    GameROMInfo info = game_validate_rom(z64_path);
    tap_ok(info.size_bytes == ROM_SIZE,
           "valid file reports correct size_bytes");
}

static void test_unrecognized_sha1(const char *z64_path) {
    GameROMInfo info = game_validate_rom(z64_path);
    /* Synthetic data will never match a known ROM hash */
    tap_ok(info.valid == false,
           "synthetic ROM returns valid=false (unknown SHA1)");
    tap_ok(strstr(info.message, "Unrecognized") != NULL,
           "unknown SHA1 message contains \"Unrecognized\"");
}

static void test_undersized(const char *path) {
    GameROMInfo info = game_validate_rom(path);
    tap_ok(info.valid == false,
           "undersized file (100 bytes) returns valid=false");
    tap_ok(strstr(info.message, "size") != NULL || strstr(info.message, "Invalid") != NULL,
           "undersized file message mentions size/invalid");
}

static void test_oversized(const char *path) {
    GameROMInfo info = game_validate_rom(path);
    tap_ok(info.valid == false,
           "oversized file (65 MB) returns valid=false");
    tap_ok(strstr(info.message, "size") != NULL || strstr(info.message, "Invalid") != NULL,
           "oversized file message mentions size/invalid");
}

static void test_empty_file(const char *path) {
    GameROMInfo info = game_validate_rom(path);
    tap_ok(info.valid == false,
           "zero-byte file returns valid=false");
}

static void test_bad_magic(const char *path) {
    GameROMInfo info = game_validate_rom(path);
    tap_ok(strcmp(info.byte_order, "???") == 0,
           "unrecognized magic -> byte_order=\"???\"");
}

static void test_null_path(void) {
    GameROMInfo info = game_validate_rom(NULL);
    tap_ok(info.valid == false,
           "NULL path returns valid=false");
}

static void test_empty_string_path(void) {
    GameROMInfo info = game_validate_rom("");
    tap_ok(info.valid == false,
           "empty string path returns valid=false");
}

static void test_nonexistent_path(void) {
    GameROMInfo info = game_validate_rom("/tmp/ge007_test_DOES_NOT_EXIST.z64");
    tap_ok(info.valid == false,
           "non-existent path returns valid=false");
}

static void test_bridge_input_state(void) {
    GameInputState state;
    GameInputState out;

    memset(&state, 0, sizeof(state));
    state.buttons = GAME_BTN_A | GAME_BTN_Z;
    state.stick_x = 80;
    state.stick_y = -80;
    state.mouse_dx = 2.5f;
    state.mouse_dy = -1.0f;
    state.mouse_wheel = 1;
    state.right_stick_x = 0.5f;
    state.right_stick_y = -0.25f;
    game_set_input(&state);

    memset(&out, 0, sizeof(out));
    gameBridgeConsumeInput(&out);
    tap_ok(out.buttons == state.buttons && out.stick_x == 80 && out.stick_y == -80,
           "bridge first consume returns current button/stick state");

    memset(&out, 0, sizeof(out));
    gameBridgeConsumeInput(&out);
    tap_ok(out.buttons == state.buttons && out.stick_x == 80 && out.stick_y == -80,
           "bridge preserves held button/stick state without a new event");
    tap_ok(out.mouse_dx == 0.0f && out.mouse_dy == 0.0f && out.mouse_wheel == 0,
           "bridge consumes mouse and wheel deltas for one frame only");

    state.mouse_dx = 1.0f;
    state.mouse_dy = 2.0f;
    state.mouse_wheel = 1;
    game_set_input(&state);
    state.mouse_dx = 3.0f;
    state.mouse_dy = 4.0f;
    state.mouse_wheel = -2;
    game_set_input(&state);

    memset(&out, 0, sizeof(out));
    gameBridgeConsumeInput(&out);
    tap_ok(out.mouse_dx == 4.0f && out.mouse_dy == 6.0f && out.mouse_wheel == -1,
           "bridge accumulates multiple mouse/wheel events before a frame");

    memset(&state, 0, sizeof(state));
    game_set_input(&state);
    memset(&out, 0xff, sizeof(out));
    gameBridgeConsumeInput(&out);
    tap_ok(out.buttons == 0 && out.stick_x == 0 && out.stick_y == 0,
           "bridge release event clears held button/stick state");

    memset(&out, 0xff, sizeof(out));
    gameBridgeConsumeInput(&out);
    tap_ok(out.buttons == 0 && out.stick_x == 0 && out.stick_y == 0,
           "bridge cleared button/stick state persists after release");
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void) {
    atexit(cleanup_tmp_files);

    printf("1..%d\n", TAP_TOTAL);

    /* ------ Generate synthetic ROM files ------ */

    /* 1. Valid .z64 (big-endian, 12 MB) */
    uint8_t *z64_data = make_z64_data();
    const char *z64_path = tmp_path("valid.z64");
    write_file(z64_path, z64_data, ROM_SIZE);

    /* 2. .v64 (byte-swapped copy) */
    uint8_t *v64_data = malloc(ROM_SIZE);
    memcpy(v64_data, z64_data, ROM_SIZE);
    byteswap_v64(v64_data, ROM_SIZE);
    const char *v64_path = tmp_path("valid.v64");
    write_file(v64_path, v64_data, ROM_SIZE);

    /* 3. .n64 (little-endian word-swapped copy) */
    uint8_t *n64_data = malloc(ROM_SIZE);
    memcpy(n64_data, z64_data, ROM_SIZE);
    byteswap_n64(n64_data, ROM_SIZE);
    const char *n64_path = tmp_path("valid.n64");
    write_file(n64_path, n64_data, ROM_SIZE);

    /* 4. Undersized (100 bytes) */
    const char *small_path = tmp_path("undersized.z64");
    write_file(small_path, z64_data, 100);

    /* 5. Oversized (65 MB -- just over the 64 MB limit) */
    const char *big_path = tmp_path("oversized.z64");
    {
        size_t big_size = 65UL * 1024 * 1024;
        uint8_t *big = calloc(1, big_size);
        if (!big) { perror("calloc oversized"); exit(2); }
        memcpy(big, Z64_MAGIC, 4);
        write_file(big_path, big, big_size);
        free(big);
    }

    /* 6. Zero-byte file */
    const char *empty_path = tmp_path("empty.z64");
    write_file(empty_path, z64_data, 0);

    /* 7. Unrecognized magic (0xDE, 0xAD, 0xBE, 0xEF) */
    const char *badmagic_path = tmp_path("badmagic.bin");
    {
        uint8_t *bad = malloc(ROM_SIZE);
        memcpy(bad, z64_data, ROM_SIZE);
        bad[0] = 0xDE; bad[1] = 0xAD; bad[2] = 0xBE; bad[3] = 0xEF;
        write_file(badmagic_path, bad, ROM_SIZE);
        free(bad);
    }

    /* Done generating -- free source buffers */
    free(z64_data);
    free(v64_data);
    free(n64_data);

    /* ------ Run tests ------ */

    /* Byte order detection (tests 1-3) */
    test_z64_byte_order(z64_path);
    test_v64_byte_order(v64_path);
    test_n64_byte_order(n64_path);

    /* SHA1 consistency across byte orders (test 4) */
    test_sha1_consistency(z64_path, v64_path, n64_path);

    /* Size field (test 5) */
    test_valid_size(z64_path);

    /* Unknown SHA1 handling (tests 6-7) */
    test_unrecognized_sha1(z64_path);

    /* Size rejection (tests 8-9 undersized, 10-11 oversized) */
    test_undersized(small_path);
    test_oversized(big_path);

    /* Empty file (test 12) */
    test_empty_file(empty_path);

    /* Bad magic (test 13) */
    test_bad_magic(badmagic_path);

    /* NULL/empty/nonexistent paths (tests 14-16) */
    test_null_path();
    test_empty_string_path();
    test_nonexistent_path();

    /* Bridge input state persistence and delta consumption (tests 17-22) */
    test_bridge_input_state();

    /* ------ Summary ------ */

    if (g_test_fail == 0) {
        printf("# All %d tests passed\n", g_test_num);
    } else {
        printf("# %d of %d tests FAILED\n", g_test_fail, g_test_num);
    }

    return g_test_fail > 0 ? 1 : 0;
}

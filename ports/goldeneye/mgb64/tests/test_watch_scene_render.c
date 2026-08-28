/*
 * test_watch_scene_render.c — ROM-free regression lane for FID-0068.
 *
 * Guards the paused-watch scene renderer's two retail constants
 * (watch_scene_render.c): the guPerspective aspect and the envcolour room-tint
 * word. Retail passes aspect 0x3FBA2E8C (16/11) and packs the tint big-endian
 * as (r<<24)|(g<<16)|(b<<8)|a (or 205 for states 5/12); the port passed 1.456f
 * (0x3FBA5E35) and stored an LE-byte-reversed *(u32*)&tileColor into the wrong
 * field. Pins the exact bit patterns so a revert of either reddens.
 */
#include "watch_scene_render.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

static uint32_t bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

int main(void)
{
    /* aspect: fixed retail 16/11, legacy port defect 1.456f */
    CHECK(bits(watchScenePerspAspect(0)) == 0x3FBA2E8Cu, "fixed aspect == 0x3FBA2E8C (16/11)");
    CHECK(bits(watchScenePerspAspect(1)) == 0x3FBA5E35u, "legacy aspect == 0x3FBA5E35 (1.456f)");

    /* tint word: big-endian pack (r<<24)|(g<<16)|(b<<8)|a. */
    CHECK(watchSceneTintWord(0, 0x11, 0x22, 0x33, 0x44) == 0x11223344u,
          "tint word packs r<<24|g<<16|b<<8|a");
    CHECK(watchSceneTintWord(3, 0xFF, 0x00, 0x80, 0xCD) == 0xFF0080CDu,
          "tint word high/low bytes are r and a");

    /* the LE reinterpret the port used would have yielded the byte-reversed
     * word; assert we are NOT that. */
    CHECK(watchSceneTintWord(0, 0x11, 0x22, 0x33, 0x44) != 0x44332211u,
          "tint word is NOT the LE-reversed *(u32*)&tileColor");

    /* raise/lower states 5 and 12 => constant 205 (0,0,0,205 darkening). */
    CHECK(watchSceneTintWord(5, 0x11, 0x22, 0x33, 0x44) == 205u, "state 5 -> 205");
    CHECK(watchSceneTintWord(12, 0x11, 0x22, 0x33, 0x44) == 205u, "state 12 -> 205");
    CHECK((205u & 0xFFu) == 205u && (205u >> 24) == 0u,
          "205 decodes to fog (0,0,0,205) under _SHIFTR");

    if (g_failures == 0) {
        printf("PASS: watch_scene_render\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}

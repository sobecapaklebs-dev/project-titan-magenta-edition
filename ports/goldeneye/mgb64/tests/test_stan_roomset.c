/*
 * test_stan_roomset.c — ROM-free regression lane for FID-0079.
 *
 * Guards sub_GAME_7F0AF20C's roomset filter (retail ASM src/game/stan.c:1063-1086,
 * .L7F0AF3CC-.L7F0AF410). Retail reads the roomset as u8 room bytes, 0xFF-
 * terminated, capped at 4 entries; the port defect cast to s32* and read 4-byte
 * words with a -1 terminator, packing 4 room bytes into one word so the filter
 * matched nothing and flag-8 projectile tile reacquire (chrobjhandler.c:5328)
 * always failed.
 *
 * The fix routes the reader's decision through stanRoomsetFilterAdmitsRoom()
 * (src/platform/stan_roomset.c), which sub_GAME_7F0AF20C calls with
 * legacy = !stanRoomsetByteFixEnabled(). This test drives BOTH readers and the
 * dispatcher directly against the exact u8 roomset[8] shape the live retail-path
 * caller builds (src/game/chrobjhandler.c:5048 `u8 roomset[8]`), so:
 *
 *   Fix-ON  (legacy=0): the byte reader ADMITS the listed rooms -> reacquire
 *                       succeeds (the tile is found in the filtered room set).
 *   Fix-OFF (legacy=1): the legacy s32 reader misreads the same u8 array and
 *                       ADMITS NOTHING -> reacquire always fails (the pre-fix
 *                       defect, reproduced byte-identically).
 *
 * Fails on revert:
 *   - byte reader reverted to s32 semantics -> it stops admitting listed rooms.
 *   - legacy reader reverted to byte semantics -> the negative control starts
 *     admitting rooms, breaking byte-identity of GE007_NO_STAN_ROOMSET_BYTE_FIX.
 *   - dispatcher wiring reverted -> the legacy!=0 vs legacy==0 assertions diverge.
 */
#include "stan_roomset.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

int main(void)
{
    /* Exact shape sub_GAME_7F0B4AB4 fills into the live u8 roomset[8]
     * (chrobjhandler.c:5048): the reachable rooms as bytes, 0xFF-terminated. */
    const unsigned char roomset_u8[8] = { 5, 9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    /* The port-added bond frozen-camera hint (bondview.c:2646) list, faithful
     * form: {room, 0xFF}. */
    const unsigned char hint_u8[2] = { 12, 0xFF };

    /* --- Byte reader (faithful, fix path) --- */
    CHECK(stanRoomsetContainsRoom(roomset_u8, 5) == 1, "byte: room 5 listed");
    CHECK(stanRoomsetContainsRoom(roomset_u8, 9) == 1, "byte: room 9 listed");
    CHECK(stanRoomsetContainsRoom(roomset_u8, 3) == 0, "byte: room 3 not listed");
    CHECK(stanRoomsetContainsRoom(roomset_u8, 0) == 0, "byte: room 0 not listed");
    CHECK(stanRoomsetContainsRoom(hint_u8, 12) == 1, "byte: hint room 12 listed");
    CHECK(stanRoomsetContainsRoom(hint_u8, 5) == 0, "byte: hint rejects non-hint room");

    /* 0xFF terminator halts the scan (empty list rejects everything). */
    {
        const unsigned char empty[4] = { 0xFF, 5, 9, 3 };
        CHECK(stanRoomsetContainsRoom(empty, 5) == 0, "byte: 0xFF at [0] terminates before 5");
        CHECK(stanRoomsetContainsRoom(empty, 9) == 0, "byte: 0xFF terminator, room 9 unreached");
    }

    /* 4-entry cap: a fifth listed room is never scanned (matches `li $a0,4`). */
    {
        const unsigned char full[6] = { 1, 2, 3, 4, 7, 0xFF };
        CHECK(stanRoomsetContainsRoom(full, 4) == 1, "byte: 4th entry (index 3) still scanned");
        CHECK(stanRoomsetContainsRoom(full, 7) == 0, "byte: 5th entry (index 4) past the cap");
    }

    /* --- Legacy s32 reader (opt-out negative control) --- */
    {
        const int legacy_list[3] = { 5, 9, -1 };
        CHECK(stanRoomsetContainsRoomLegacyS32(legacy_list, 5) == 1, "legacy: s32 5 listed");
        CHECK(stanRoomsetContainsRoomLegacyS32(legacy_list, 9) == 1, "legacy: s32 9 listed");
        CHECK(stanRoomsetContainsRoomLegacyS32(legacy_list, 3) == 0, "legacy: s32 3 not listed");
        CHECK(stanRoomsetContainsRoomLegacyS32(legacy_list, -1) == 0, "legacy: terminator not a member");
    }

    /* --- The defect, at the real reader's dispatch (both flag sides) ---
     * Same u8 roomset the live caller passes; stanRoomsetFilterAdmitsRoom is
     * exactly what sub_GAME_7F0AF20C calls.
     *
     * Fix-ON: reacquire succeeds for the listed rooms. */
    CHECK(stanRoomsetFilterAdmitsRoom(roomset_u8, 5, 0) == 1,
          "fix-ON: reacquire admits listed room 5");
    CHECK(stanRoomsetFilterAdmitsRoom(roomset_u8, 9, 0) == 1,
          "fix-ON: reacquire admits listed room 9");
    CHECK(stanRoomsetFilterAdmitsRoom(roomset_u8, 3, 0) == 0,
          "fix-ON: reacquire rejects unlisted room 3");

    /* Fix-OFF (legacy): the s32 misread of the u8 array admits nothing -> the
     * pre-fix always-fail. The packed word (5 | 9<<8 | 0xFF<<16 | 0xFF<<24 on LE,
     * or 0x0509FFFF on BE) equals neither room index nor -1, and the second word
     * is the -1 terminator, so no room is admitted on either endianness. */
    CHECK(stanRoomsetFilterAdmitsRoom(roomset_u8, 5, 1) == 0,
          "fix-OFF: legacy s32 misread rejects room 5 (reproduces defect)");
    CHECK(stanRoomsetFilterAdmitsRoom(roomset_u8, 9, 1) == 0,
          "fix-OFF: legacy s32 misread rejects room 9 (reproduces defect)");

    /* The two flag sides MUST diverge on the live caller's data — that divergence
     * is the whole of FID-0079. */
    CHECK(stanRoomsetFilterAdmitsRoom(roomset_u8, 5, 0)
              != stanRoomsetFilterAdmitsRoom(roomset_u8, 5, 1),
          "fix-ON and fix-OFF disagree on the live u8 roomset (the defect)");

    if (g_failures == 0) {
        printf("test_stan_roomset: OK\n");
        return 0;
    }
    fprintf(stderr, "test_stan_roomset: %d failure(s)\n", g_failures);
    return 1;
}

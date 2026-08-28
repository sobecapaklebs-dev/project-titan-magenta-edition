/*
 * stan_roomset.h — FID-0079 roomset-filter membership test for sub_GAME_7F0AF20C.
 *
 * Retail sub_GAME_7F0AF20C reads its roomset filter as a run of u8 room bytes,
 * 0xFF-terminated, capped at 4 entries, comparing each byte to the room counter
 * (ASM src/game/stan.c:1063-1086, .L7F0AF3CC-.L7F0AF410: `li $s6,255` sentinel,
 * `li $a0,4` cap, `lbu` byte loads, `addiu $v0,$v0,1` byte stride). The live
 * retail-path caller (src/game/chrobjhandler.c:5328) passes the u8 roomset[8]
 * that sub_GAME_7F0B4AB4 fills as bytes (src/game/bg.c:6746 `u8 *outputRoomSet`),
 * so the byte read is the faithful one.
 *
 * The prior port cast the roomset to s32* and read 4-byte words with a -1
 * terminator, which packs 4 room bytes into a single word: it neither equals the
 * room index nor -1, so the filter rejected every room and flag-8 projectile tile
 * reacquire always failed. These pure helpers isolate the filter decision so it
 * is unit-testable and so the negative control (legacy s32 read) stays
 * byte-identical to the pre-fix port under GE007_NO_STAN_ROOMSET_BYTE_FIX.
 */
#ifndef STAN_ROOMSET_H
#define STAN_ROOMSET_H

/* Faithful u8 read: up to 4 bytes, 0xFF terminator, byte compared to `room`.
 * Returns 1 if `room` is in the set, else 0. */
int stanRoomsetContainsRoom(const unsigned char *roomset, int room);

/* Legacy pre-FID-0079 read: s32 words, -1 terminator, up to 4 entries. Kept so
 * the opt-out negative control reproduces the old behavior exactly. */
int stanRoomsetContainsRoomLegacyS32(const int *roomset, int room);

/* The filter decision sub_GAME_7F0AF20C makes for one room. `legacy` selects the
 * reader (0 = faithful bytes, 1 = legacy words) so the env read stays at the call
 * site (mirrors projectileEndpointPullback's `legacy` parameter). Returns 1 if
 * the room is admitted by the filter. */
int stanRoomsetFilterAdmitsRoom(const void *roomset, int room, int legacy);

#endif /* STAN_ROOMSET_H */

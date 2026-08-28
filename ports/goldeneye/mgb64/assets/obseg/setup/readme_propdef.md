# Object / Propdef Readme

This file desribes how items in the setup objects list are used in the game.

Items are described by the `enum PROPDEF_TYPE`. There are various kinds of objects that can be listed:

- guards (and hats)
- props (doors, vehicle, tank, monitor, etc)
- collectibles (body armor, ammo, weapons, items in safe, etc)
- objectives
- autogun / drones
- CCTV / surveilance cameras
- keys, locked doors, and locks
- glass
- watch menu text
- poison gas (facility)
- meta items (rename, link, switch)
- meta programming (tag, `PROPDEF_END`)

Level objects are just blank structs ready to be used by the game. For instance, the structs have position fields that are zero in the setup file but are set (and updated) when the level is loaded and active. The only exception to this is guards (characters) which are copied to a much larger character struct on level load.

# Item Location

If an item is to be placed at a specific coordinate, it will be associated with a "preset location". This is the second half of the second word in the item `propdef`. This preset id is an index into the `padlist`. The pad contains the position (and `up` and `orientiation` vectors) to describe where the item will be placed. The pad also contains an associated stan tile name. A brief example:

UsetuprunZ.c

    s32 objlist[] = {
        /* Type = AmmoBox; index = 99 */
        _mkword(128, _mkshort(0, 20)), _mkword(4, 24) ...

    struct pad padlist[] = {
        /* index = 24 */
        { {683.0f, -38.0f, -467.0f}, {0.0f, 1.0f, 0.0f}, {0.002546f, 0.0f, 0.999997f}, "p3830a", 0 },

Tbg_run_all_p_stanZ.c

    StandTile tile_429 = {
        0x0ef600, 0x04,
        0x0,
        0xf, 0xf, 0xf,
        3,
        0x0, 0x1, 0x2,
        {
            {701, -41, -446, 0x06d1},
            {710, -41, -472, 0x0000},
            {665, -41, -472, 0x06c9}
        }
    };

Here the Runway ammobox is associated to preset 24, so it will use the coordinates from pad with index 24. The pad has link "p3830a", which refers to tile name 3830, or in hex, 0x0ef6.

# Multi-Ammo Crate Contents (`PROPDEF_AMMO`, type 20)

After the standard object header, an ammo crate carries a 52-byte tail of **13
`{u16 modelnum, u16 quantity}` pairs** (offset `0x80`, 4 bytes each). `modelnum`
`0xFFFF` means "no model" (nothing spawns if the crate is shot); `quantity` is
the ammo dispensed on pickup. Slot `i` is authored for **ammotype `i + 1`**, and
slot 1 is folded into the shared 9mm pool (so slots 0 and 1 both feed 9mm and
ammotype 2 / `AMMO_9MM_2` is never targeted):

| slot | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|------|---|---|---|---|---|---|---|---|---|---|----|----|----|
| ammotype | 9mm | 9mm | rifle | shotgun | grenade | rockets | remote | prox | timed | knife | gren-round | magnum | ggun |

For example, the Runway crates author `quantity = 5` at slot 4 → the player
receives 5 grenades (times the solo-play ammo multiplier), and nothing else.

The collect loop (`collect_or_interact_object`, `PROPDEF_AMMO` case in
`chrobjhandler.c`) reads `slots[i].quantity` with this 4-byte pair stride,
matching retail `interact_ammobox_object` (asm `7F050338`). It must NOT read the
smaller `unk80/quantities[]` overlay view of the same union: that 2-byte stride
lands on `modelnum` lanes for odd `i`, where `0xFFFF` reads as a quantity of
65535 and maxes out ammo on pickup. See the comment on `MultiAmmoCrateRecord`
in `src/bondtypes.h` and the regression gate `tools/ammo_crate_collect_smoke.sh`.
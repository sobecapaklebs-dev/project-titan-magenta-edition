# Titan Game Patches

Project Titan keeps gameplay cheats and graphics enhancements deliberately
separate on disk and in the SELECT menu while using the same PS2RD-compatible
runtime transport underneath.

## USB layout

```text
mass:/CHT/<STARTUP>.cht
mass:/ENH/<STARTUP>.enh
mass:/TITAN/gamepatches/<STARTUP>.cht.state
mass:/TITAN/gamepatches/<STARTUP>.enh.state
mass:/TITAN/gamepatches/<STARTUP>.tpch
```

`<STARTUP>` is the PS2 executable ID used by the catalog, for example
`SLUS_209.46`. `CHT` is intentionally compatible with the normal OPL naming
convention. `ENH` is Titan-only so visual patches never get mixed with gameplay
cheats.

The files in `TITAN/gamepatches` are private Titan state/runtime files. Do not
ship patch databases there.

## Gameplay cheats (`CHT`)

Example `mass:/CHT/SLUS_209.46.cht`:

```text
Master Code
90100000 0C040000

Infinite Health
20123456 00000063

Infinite Ammo
20123460 00000063
```

A heading starts an entry. The following PS2RD address/value pairs belong to
that entry until the next heading. `#` and `//` comments are accepted.

Gameplay cheats are always opt-in. Titan never enables them automatically.
Recognized master/enable-code headings are hidden from the user and are added
before selected gameplay codes when required.

## Graphics enhancements (`ENH`)

Example `mass:/ENH/SLUS_209.46.enh`:

```text
# @auto
Widescreen 16:9
20101010 3C013F40

# @auto
HUD Fix
20101020 12345678

# @experimental
60 FPS
20102020 00000001
```

Enhancements use the same PS2RD address/value representation, plus Titan
metadata comments:

- `@auto` — enabled by default for this game.
- `@experimental` — never enabled automatically, even if `@auto` is also
  present.

Only patches that have been validated on real PS2 hardware should be marked
`@auto`. Patches that depend on PCSX2 renderer behavior, internal resolution,
EE overclock, `dpatch=`, `gsaspectratio=` or other emulator-only facilities do
not belong in ENH as-is.

## SELECT menu behavior

For a supported game the SELECT menu exposes two independent entries:

```text
Cheats
Graphics Enhancements
```

The source databases are loaded by game STARTUP ID. User toggles persist in
Titan's private state directory and are not written back into the CHT/ENH
source files.

When at least one graphical enhancement is active at launch, the normal game
launch animation displays `Titan Enhanced` in white semibold text with a thin
magenta outline. Gameplay cheats do not trigger this badge.

When gameplay cheats are active, Titan can display a short localized taunt in
the launch animation. Those strings use the normal PT-BR / EN-US / ES
localization path and never appear for graphics-only enhancements.

## Runtime routing

### Neutrino

No active patch payload:

```text
Titan -> neutrino/neutrino.elf -> game
```

Active cheat/enhancement payload:

```text
Titan -> embedded low-memory handoff stage2 -> Golden neutrino.elf -> game
```

The normal Neutrino executable remains a separate build artifact and the patch
runtime is selected only when Titan has produced a valid payload.

### Titan UL

Golden Titan UL is never rebuilt. Titan stages the selected CHT/ENH codes through the normal OPL CHT/CFG interface, and Golden UL consumes them through its existing PS2RD
cheat infrastructure. When Titan controls the payload, an absent/invalid TPCH
fails closed instead of falling back to unrelated legacy cheat state.

## Capacity and failure policy

Titan validates both the transport size and the actual PS2RD internal limits
before publishing a payload:

- maximum 5 hook/master slots;
- maximum 250 ordinary code slots;
- maximum 254 address/value pairs in the Titan transport.

Oversized or malformed patch sets are rejected before game launch and any stale
payload is removed. This avoids overflowing PS2RD's separate hook and code
arrays.

## Adding a graphics enhancement database

For every imported patch:

1. Match the correct game STARTUP ID and revision/CRC during validation.
2. Remove PCSX2-only directives; keep only code sequences valid on real PS2.
3. Test on hardware with the normal video mode first.
4. Test again with every Titan resolution mode intended for that engine.
5. Mark `@auto` only after the patch is known-safe for that exact game build.
6. Keep performance-sensitive patches such as 50/60 FPS or increased LOD
   `@experimental` unless real-hardware testing proves otherwise.

The ENH directory is a patch database, not a substitute for Titan's GSM/video
output policy. Game-side viewport/FOV fixes and GS output resolution remain
separate layers.

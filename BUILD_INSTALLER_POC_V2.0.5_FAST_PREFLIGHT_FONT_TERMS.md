# Project Titan Installer POC v2.0.5

Scope: performance + text correctness only. No installer visual redesign.

## Fast TINS preflight

- Startup no longer calls `update_preflight()` before the first interactive page.
- System Verification validates only the TINS header, entry table, table CRC,
  safe paths and bounds through `load_manifest()`.
- It does **not** read every payload byte during preflight.
- Full payload integrity remains enforced during installation:
  - source CRC is accumulated while streaming each payload into `.TITAN.NEW`;
  - staged file is reread and CRC/size checked before publication;
  - rollback/backup behavior is unchanged.
- TINS remains seekless/forward-only on PS2 hardware.

## Terms layout

- Canonical VlombisCore notice remains byte-for-byte present in UTF-8 source.
- Fixed 28 px per-item placement was removed.
- Wrapped blocks now advance by their actual rendered line count (22 px line
  height + 6 px item spacing), clipped inside the same existing panel.
- Terms scrolling remains item-based and now permits reaching every item.
- No panel/button/color/layout redesign.

## Unicode font additions

The existing ASCII atlas is unchanged. The font renderer now draws these
symbols procedurally so they no longer fall back to `?`:

- U+26A0 WARNING SIGN: ⚠
- U+2122 TRADE MARK SIGN: ™
- U+00AE REGISTERED SIGN: ®
- U+2022 BULLET: •
- U+2191 UPWARDS ARROW: ↑
- U+2193 DOWNWARDS ARROW: ↓
- U+2192 RIGHTWARDS ARROW: →

`TitanFont_MeasureText()` uses matching advances for these symbols, so wrapping
and button/text measurement remain consistent with rendering.

## Build

```bash
make -C kernel clean
make -C kernel installer
```

Only `titan-installer.elf` needs to be replaced on the installer USB. The
existing `TITAN_SYSTEM.TINS` is unchanged.

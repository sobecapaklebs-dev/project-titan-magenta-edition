# Project Titan Magenta Edition RC14 — Build 36

## Complex Theme API v3

Build 36 extends the declarative `.tthm` system for community-authored animated
shell themes. It keeps all Theme API v1 Flow themes and Theme API v2 `scene-v2`
themes compatible; no executable theme payload is introduced.

### New in API v3

`scene-v2` themes using `shell_api=3` may define independent idle animation loops
for the stable HOME and TOP MENU states. The base `scene_home` / `scene_top` frame
is always frame zero of the corresponding loop; additional frames are declared as:

```ini
scene_home_idle_count=2
scene_home_idle_0=scenes/home-idle-01.rgb565
scene_home_idle_1=scenes/home-idle-02.rgb565
scene_home_idle_ms=140
scene_home_idle_mode=pingpong

scene_top_idle_count=5
scene_top_idle_0=scenes/top-idle-01.rgb565
scene_top_idle_1=scenes/top-idle-02.rgb565
scene_top_idle_2=scenes/top-idle-03.rgb565
scene_top_idle_3=scenes/top-idle-04.rgb565
scene_top_idle_4=scenes/top-idle-05.rgb565
scene_top_idle_ms=90
scene_top_idle_mode=loop
```

Supported playback modes:

- `loop`: base -> idle0 -> ... -> idleN -> base.
- `pingpong`: base -> idle0 -> ... -> idleN -> ... -> idle0 -> base.

Frame time is monotonic-millisecond based, not refresh-frame based, so the same
manifest keeps the same visual speed in PAL, NTSC and 720p output modes.

### State semantics

- stable HOME (`top_menu_transition <= 0.001`): HOME idle loop may run;
- HOME -> TOP / TOP -> HOME: idle loops are paused and reset; the existing
  reversible transition keyframes remain driven by the shell's own transition;
- stable TOP (`top_menu_transition >= 0.999`): TOP idle loop may run;
- Settings/Profile/VMC/Library/folder functional views: scene fades out and only
  the theme Flow/ribbon is exposed, exactly as in API v2.

Entering HOME or TOP always restarts that state's idle loop at its base frame.
This makes authored transformations deterministic: for example, the TOP state
can always start on a clean Super Saiyan frame before the aura begins looping.

### Resource budget

Every scene frame remains raw 512x256 RGB565 LE = 262,144 bytes.

Build 36 enforces:

- transition frames: 0..6;
- additional HOME idle frames: 0..8;
- additional TOP idle frames: 0..8;
- HOME + TOP additional idle frames combined: **maximum 8**;
- idle frame time: **40..2000 ms**.

The strict shared idle budget means the absolute maximum resident scene cache is:

```text
2 mandatory frames + 6 transition frames + 8 idle frames = 16 frames
16 * 262,144 bytes = 4,194,304 bytes (4 MiB EE RAM)
```

No additional persistent GS VRAM is reserved. The selected full-screen frame is
still drawn through `TitanVideo_DrawMediaImage16`, using the existing 512x256
media path and the 720p scratch-tile strategy.

### Why full-scene idle frames

API v3 intentionally animates complete precomposed RGB565 scene frames rather
than introducing arbitrary shader/microprogram code or transparent executable
layers. Theme authors can therefore animate aura, fire, smoke, rain, lightning,
characters breathing/blinking, cloth/hair, signage, screens, parallax prepared
offline, or any combination of these, while the Titan kernel retains complete
control of memory, rendering and input behavior.


### Community authoring helper

Build 36 also ships `kernel/tools/titan_theme_asset.py` for host-side conversion
and validation of 512x256 scene frames and 256x128 previews. The converter uses
Pillow only on the author's PC; the PS2 runtime remains dependency-free.

### Package hardening

The TPKG builder/verifier now validates common theme metadata for all `.tthm`
packages and rejects a package when `theme.cfg id=` does not exactly match the
TPKG `--title-id`. This prevents packages that install to one theme ID while the
manifest claims another ID.

Theme packages remain declarative. `.elf`, `.tmod`, `.irx`, `.so` and `.vsm`
remain forbidden.

### Compatibility

- `shell_api=1` + `flow3d-v1`: unchanged.
- `shell_api=2` + `scene-v2`: unchanged; no idle-loop keys required.
- `shell_api=3` + `scene-v2`: adds idle loops.
- an API v2 theme that declares API v3 idle keys is rejected rather than silently
  ignoring them.

### Build/update metadata

- system update generation: `36`;
- PUP version: `0.3.90-build36`;
- PUP target: `Project-Titan-Magenta-Edition-0.3.90-build36-complex-theme-api-v3-compat7.pup`;
- updater self-test target: `Project-Titan-PUP-SelfTest-build37.pup`.

The seven-entry TPUP envelope proven on hardware is preserved for the Build 35 ->
Build 36 update path.

See `docs/THEME_AUTHORING.md` for the complete community authoring specification.

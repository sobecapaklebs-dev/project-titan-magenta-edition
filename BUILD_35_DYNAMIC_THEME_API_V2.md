# Project Titan Magenta Edition RC14 — Build 35

## Dynamic Theme API v2

Build 35 extends the existing declarative `.tthm`/TPKG v2 system without allowing executable theme code. Existing API v1 Flow themes remain valid.

### scene-v2 contract

`background_engine=scene-v2` requires `shell_api=2` and two raw RGB565 512x256 frames:

- `scene_home`: full HOME composition.
- `scene_top`: stable top-menu composition.
- `scene_transition_count=0..6` plus `scene_transition_0..5`: optional intermediate keyframes.

The frames are selected from Titan's existing `top_menu_transition`, so forward and reverse animation remain synchronized with the shell. The cache is EE RAM only; the GS 720p double-buffer VRAM layout is not expanded.

### Functional-view rule

For scene themes the full scene is visible on HOME/top menu with **no ribbon**. During entry into Settings, Profile, Saved Data/VMC, Library or fullscreen folder-option screens, the scene fades away and the theme's Flow palette/ribbon is revealed.

### Audio

Theme-relative WAV assets can override `audio_music`, `audio_notification`, `audio_profile_entry`, `audio_home_nav`, `audio_top_nav`, `audio_interface_nav`, `audio_shutdown`, and `audio_top_enter`. Missing overrides fall back to Titan assets; `audio_top_enter` is optional and has no default duplicate effect.

### Icons

`icon_tint_enabled=1` plus RGB values tints Titan system glyphs. `icon_decorator=none|ring|corners` adds a lightweight declarative decoration.

### Security and package behavior

`.tthm` remains TPKG v2 with `@theme`; `.elf`, `.tmod`, `.irx`, `.so`, and `.vsm` payloads remain rejected. Scene paths are theme-relative, reject traversal/absolute/device paths, and scene frames are validated as exactly 512x256 RGB565 before a theme is accepted.

### Build

- updater generation: `35`
- PUP version: `0.3.90-build35`
- PUP target: `Project-Titan-Magenta-Edition-0.3.90-build35-dynamic-theme-api-v2-bootstrap-compat7.pup`
- bootstrap envelope: **7 entries / payload offset 1072 bytes**, matching BuildFix34

### BuildFix34 -> Build 35 bootstrap

Real-PS2 testing showed that both the original ten-entry Build 35 update and the
subsequent kernel-only one-entry bootstrap could fail during the **read-only first
payload CRC pass** on a running BuildFix34. The kernel-only attempt reported, for
example, `arq 1 exp 3076A639 obt 1B2AF221` before staging began. This proves the
problem is not a later publish/staging CRC boundary.

BuildFix34 itself uses a seven-entry TPUP envelope. Build 35 therefore preserves
that already-deployed geometry exactly: 64-byte header + 7 x 144-byte entries =
**1072-byte payload offset**. `kernel.elf` remains payload 1; entries 2..7 are the
existing wLaunchELF/Scarlet Flow assets already shipped by BuildFix34. They are
repacked only to preserve the proven envelope while bootstrapping the new kernel.

`make -C kernel pup` builds and verifies exactly:

```text
kernel.elf
TITAN/packages/WLAUNCHELF.pkg
TITAN/apps/TITNWLE01/wlaunchelf.tmod
TITAN/apps/TITNWLE01/icon.rgb565
TITAN/apps/TITNWLE01/titan_app.ini
TITAN/apps/TITNWLE01/UPSTREAM_NOTICE.txt
TITAN/themes/SCARLETFLOW/theme.cfg
```

The three Flow color variants are intentionally *not* in this bootstrap, because
adding them changes the table back to ten entries. No updater security policy is
weakened: build 35 is still forward-only and table/payload CRC validation remains
mandatory.

### Manifest example

```ini
format=1
id=MYSCENE
name=My Dynamic Theme
author=Theme Author
version=1.0.0
shell_api=2
background_engine=scene-v2

scene_home=scenes/home.rgb565
scene_top=scenes/top.rgb565
scene_transition_count=3
scene_transition_0=scenes/up-01.rgb565
scene_transition_1=scenes/up-02.rgb565
scene_transition_2=scenes/up-03.rgb565
scene_system_flow=1

# Flow is only exposed on the supported functional views for scene-v2 themes.
wave_r=244
wave_g=190
wave_b=52
highlight_r=255
highlight_g=232
highlight_b=120

# Optional theme-owned audio; each path is relative to the installed theme root.
audio_music=audio/bgm.wav
audio_home_nav=audio/home-nav.wav
audio_top_nav=audio/top-nav.wav
audio_interface_nav=audio/interface-nav.wav
audio_top_enter=audio/top-enter.wav

# Optional light-weight system-glyph styling.
icon_tint_enabled=1
icon_tint_r=255
icon_tint_g=226
icon_tint_b=120
icon_decorator=corners
icon_decorator_r=255
icon_decorator_g=196
icon_decorator_b=48
icon_decorator_alpha=180
```

`scene_transition_*` frames are reused in reverse order automatically when the shell returns from the top menu, because selection is derived from the same reversible `top_menu_transition` value. With the maximum six intermediate frames, scene storage is at most 2 MiB in EE RAM (HOME + TOP + six intermediates) while active; only one frame is uploaded/drawn per shell frame.

Theme packages are built with `kernel/tools/titan_pkg.py build --type theme` and must use the `.tthm` extension. The builder rejects wrong-sized scene frames, missing declared assets, transition counts above six, and executable/microprogram payloads before packaging.
- forward-only updater self-test target: `Project-Titan-PUP-SelfTest-build36.pup` (numeric build 36, so it is accepted by a running Build 35).

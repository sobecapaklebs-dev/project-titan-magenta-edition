# Project Titan `.tthm` Theme Authoring Guide

This document is the author-facing contract for Project Titan themes as of
**Build 36 / Theme Shell API v3**.

## 1. Design goals

Titan themes are data, not programs. A theme can change shell presentation,
animated scene content, audio, icon treatment and the functional-view Flow
palette, but it cannot replace input mappings, launch code, storage policy,
package policy or shell state machines.

This separation lets community themes become visually complex without giving a
`.tthm` arbitrary execution privileges on the PS2.

## 2. Package and installation

A theme is a TPKG v2 package with:

```text
extension:  .tthm
app type:   theme
entrypoint: @theme
```

Copy packages to:

```text
mass:/TITAN/packages/
```

Install from:

```text
★Debug Settings > Package Installer
```

Installed themes live under:

```text
mass:/TITAN/themes/<THEME-ID>/
```

The TPKG `title-id` and `theme.cfg` `id=` **must be identical**. Both use 3–15
uppercase ASCII letters/digits plus `_` or `-`.

## 3. Recommended source tree

```text
MYTHEME/
├── theme.cfg
├── preview.rgb565
├── scenes/
│   ├── home.rgb565
│   ├── top.rgb565
│   ├── transition-01.rgb565
│   ├── transition-02.rgb565
│   ├── home-idle-01.rgb565
│   ├── top-idle-01.rgb565
│   └── top-idle-02.rgb565
└── audio/
    ├── bgm.wav
    ├── home-nav.wav
    ├── top-nav.wav
    ├── interface-nav.wav
    └── top-enter.wav
```

## 4. Image formats

### Scene frames

All `scene_*` images are raw little-endian RGB565:

```text
width:   512
height:  256
stride:  512 pixels
size:    262144 bytes exactly
```

The frame is scaled by Titan to the active logical shell canvas. Design for the
full 16:9 composition and keep important UI-negative space clear.

### Theme preview

Optional selector preview:

```text
preview.rgb565
256x128 RGB565 LE
65536 bytes exactly
```

## 5. Converting artwork on a PC

Build 36 ships `kernel/tools/titan_theme_asset.py` as a host-side authoring
helper. It is **not** part of the PS2 runtime. Image conversion uses Pillow.

Exact-size source artwork can be encoded with:

```sh
python3 kernel/tools/titan_theme_asset.py encode \
  --kind scene --input home.png --output scenes/home.rgb565

python3 kernel/tools/titan_theme_asset.py encode \
  --kind preview --input preview.png --output preview.rgb565
```

The tool refuses the wrong source dimensions by default. Use `--resize` only
when you intentionally want the tool to resample the artwork. Raw assets can be
checked independently:

```sh
python3 kernel/tools/titan_theme_asset.py verify \
  --kind scene scenes/home.rgb565
```

On Arch/Garuda, Pillow is normally provided by the `python-pillow` package. The
Titan kernel and `.tthm` runtime do not acquire a Pillow dependency.

## 6. Theme API generations

### API v1 — Flow themes

```ini
shell_api=1
background_engine=flow3d-v1
```

Uses Titan's procedural/VU1 Flow engine.

### API v2 — state/keyframe scene themes

```ini
shell_api=2
background_engine=scene-v2
```

Adds full HOME/TOP images and reversible transition keyframes.

### API v3 — complex scene themes

```ini
shell_api=3
background_engine=scene-v2
```

Adds stable-state HOME/TOP idle loops while keeping API v2 transition behavior.

## 7. Required scene-v2 keys

```ini
format=1
id=MYTHEME
name=My Theme
author=Author Name
version=1.0.0
shell_api=3
background_engine=scene-v2

scene_home=scenes/home.rgb565
scene_top=scenes/top.rgb565
```

`scene_home` is used on HOME. `scene_top` is the stable top-menu base frame.

## 8. HOME <-> TOP transition animation

Optional transition frames:

```ini
scene_transition_count=4
scene_transition_0=scenes/transition-01.rgb565
scene_transition_1=scenes/transition-02.rgb565
scene_transition_2=scenes/transition-03.rgb565
scene_transition_3=scenes/transition-04.rgb565
```

Limits: 0..6 intermediate frames.

Titan does not run a separate theme timer for this transition. The frames are
selected from the shell's real reversible `top_menu_transition` value. Therefore:

- moving up advances the frames;
- moving back down uses the same frames in reverse;
- theme animation cannot drift out of sync with the UI transition.

Use this for transformations, pose changes, logo disappearance, camera motion,
large environment changes or other state changes tied to entering the top menu.

## 9. API v3 idle loops

Idle loops run **only after a state is fully stable**. During HOME/TOP movement,
they pause and reset automatically.

### HOME loop

```ini
scene_home_idle_count=2
scene_home_idle_0=scenes/home-idle-01.rgb565
scene_home_idle_1=scenes/home-idle-02.rgb565
scene_home_idle_ms=160
scene_home_idle_mode=pingpong
```

### TOP loop

```ini
scene_top_idle_count=5
scene_top_idle_0=scenes/top-idle-01.rgb565
scene_top_idle_1=scenes/top-idle-02.rgb565
scene_top_idle_2=scenes/top-idle-03.rgb565
scene_top_idle_3=scenes/top-idle-04.rgb565
scene_top_idle_4=scenes/top-idle-05.rgb565
scene_top_idle_ms=90
scene_top_idle_mode=loop
```

The base frame (`scene_home` or `scene_top`) is implicitly frame 0. Do not count
it in `*_idle_count`.

Allowed modes:

```text
loop
pingpong
```

Allowed timing:

```text
40..2000 ms per frame
```

The timer uses monotonic milliseconds, so the authored speed is independent of
50/60 Hz output timing.

### Memory budget

The two states share **eight additional idle frames total**:

```text
scene_home_idle_count + scene_top_idle_count <= 8
```

With six transition frames this caps the complete active scene cache at 4 MiB EE
RAM. The builder, verifier and runtime all enforce the same limit.

### Examples of what idle loops can animate

Because an idle frame is a complete precomposed scene, authors can combine many
effects in one state:

- character breathing, blinking and pose micro-movement;
- Super Saiyan aura pulse/flame motion;
- sparks, lightning and energy particles;
- fire, smoke, dust, snow, rain and fog;
- cloth/hair motion;
- animated screens/signs;
- subtle camera/parallax prepared offline;
- environmental lighting changes.

For subtle loops, 4–6 total frames at roughly 80–180 ms/frame generally gives a
convincing result without consuming the entire idle-frame budget.

## 10. Functional-view Flow rule

For `scene-v2` themes, HOME and TOP display the full authored scene and **do not
render the ribbon**.

When entering the following functional surfaces, Titan fades the scene out and
reveals only the theme's Flow background/ribbon palette:

- Settings;
- Profile;
- Saved Data / VMC;
- Library;
- fullscreen folder-option screens.

Enable this behavior with:

```ini
scene_system_flow=1
```

Then configure the existing Flow keys (`bg_top_*`, `bg_bottom_*`, `wave_*`,
`highlight_*`, particles and optional Flow motion parameters).

## 11. Audio

Optional theme-relative WAV overrides:

```ini
audio_music=audio/bgm.wav
audio_notification=audio/notification.wav
audio_profile_entry=audio/profile-entry.wav
audio_home_nav=audio/home-nav.wav
audio_top_nav=audio/top-nav.wav
audio_interface_nav=audio/interface-nav.wav
audio_top_enter=audio/top-enter.wav
audio_shutdown=audio/shutdown.wav
```

Missing files are not required; Titan falls back to the system assets. The
special `audio_top_enter` event has no duplicate default effect and is intended
for authored events such as a transformation/energy cue.

Runtime audio expects 32 kHz stereo WAV. PCM must be 16-bit; Titan also supports
its validated stereo IMA-ADPCM WAV path.

## 12. Icon styling

Global system glyph tint:

```ini
icon_tint_enabled=1
icon_tint_r=255
icon_tint_g=220
icon_tint_b=120
```

Decorators:

```ini
icon_decorator=none
# or ring / corners

icon_decorator_r=255
icon_decorator_g=196
icon_decorator_b=48
icon_decorator_alpha=180
```

Decorators are presentation-only and do not change icon hitboxes/navigation.

## 13. Shell styling

Supported v1 styling keys remain available:

```ini
shell_accent_r=238
shell_accent_g=180
shell_accent_b=62
shell_selected_scale_milli=1000
shell_tile_spacing_milli=1000
shell_title_offset_y_milli=0
shell_header_alpha_milli=1000
shell_help_alpha_milli=1000
shell_chrome_alpha_milli=1000
shell_motion_x_milli=1020
shell_motion_y_milli=980
```

Theme settings cannot remap buttons or replace the Titan navigation state
machine.

## 14. Packaging

Build a theme:

```sh
python3 kernel/tools/titan_pkg.py build \
  --source ./MYTHEME \
  --output MYTHEME.tthm \
  --title-id MYTHEME \
  --title "My Theme" \
  --version 1.0.0 \
  --type theme
```

Verify before copying it to USB:

```sh
python3 kernel/tools/titan_pkg.py verify MYTHEME.tthm
```

Both build and verify reject:

- `theme.cfg id` / TPKG title-ID mismatch;
- unsupported shell API/background engine;
- missing or wrong-sized scene frames;
- API v3 idle keys under shell_api 1/2;
- >8 total additional idle frames;
- idle frame times outside 40..2000 ms;
- invalid idle modes;
- unsafe/absolute/traversal paths;
- executable or microprogram files inside `.tthm`.

## 15. Complete API v3 example

```ini
format=1
id=ENERGY01
name=Energy Theme
author=Community Author
version=1.0.0
shell_api=3
background_engine=scene-v2

scene_home=scenes/home.rgb565
scene_top=scenes/top.rgb565
scene_transition_count=3
scene_transition_0=scenes/up-01.rgb565
scene_transition_1=scenes/up-02.rgb565
scene_transition_2=scenes/up-03.rgb565

scene_home_idle_count=1
scene_home_idle_0=scenes/home-idle-01.rgb565
scene_home_idle_ms=180
scene_home_idle_mode=pingpong

scene_top_idle_count=5
scene_top_idle_0=scenes/top-aura-01.rgb565
scene_top_idle_1=scenes/top-aura-02.rgb565
scene_top_idle_2=scenes/top-aura-03.rgb565
scene_top_idle_3=scenes/top-aura-04.rgb565
scene_top_idle_4=scenes/top-aura-05.rgb565
scene_top_idle_ms=90
scene_top_idle_mode=loop

scene_system_flow=1

bg_top_r=8
bg_top_g=5
bg_top_b=22
bg_bottom_r=20
bg_bottom_g=8
bg_bottom_b=38
wave_r=238
wave_g=176
wave_b=54
wave_alpha=50
highlight_r=198
highlight_g=142
highlight_b=255
highlight_alpha=82

shell_accent_r=238
shell_accent_g=180
shell_accent_b=62

icon_tint_enabled=1
icon_tint_r=244
icon_tint_g=220
icon_tint_b=150
icon_decorator=corners
icon_decorator_r=192
icon_decorator_g=122
icon_decorator_b=255
icon_decorator_alpha=170

audio_music=audio/bgm.wav
audio_home_nav=audio/home-nav.wav
audio_top_nav=audio/top-nav.wav
audio_interface_nav=audio/interface-nav.wav
audio_top_enter=audio/top-enter.wav
```

## 16. Current intentional limits

API v3 does **not** load theme-supplied ELF/IRX/VU microprograms, does not allow
arbitrary shader code, and does not expose shell/input/storage internals. Idle
animation is full-scene RGB565 rather than independent alpha layers. These limits
are deliberate: they keep third-party themes bounded, deterministic and safe on
PS2 hardware while still allowing complex precomposed animation.

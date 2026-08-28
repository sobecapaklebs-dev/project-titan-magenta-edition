# Theme Authoring v4 — BF28 CP12

A v4 theme uses:

```
shell_api=4
background_engine=scene-v4
scene_v4_manifest=scene.v4
```

Create a JSON source with `layers`, `vram_budget`, `ee_ram_budget`, and `reserved_headroom`, then run:

```
python3 kernel/tools/titan_theme_asset.py compile-v4 --input scene.json --output scene.v4
```

Each layer currently declares a RGB565 asset (1..256 x 1..128), `z`, `x`, `y`, `scale_x`, `scale_y`, `rotation`, `alpha`, `parallax_x`, `parallax_y`, `priority` (`ESSENTIAL`, `ENHANCED`, `DECORATIVE`), `context` (`HOME`, `TOP`, `BOTH`) and `mode` (`STATIC`, `LOOP`, `PINGPONG`, `ONCE`). Paths are relative and executable extensions are rejected.

Use many small scene elements rather than fullscreen frame sequences. v4 is designed for composition; fullscreen video is not the v4 mechanism.

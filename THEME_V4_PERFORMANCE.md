# Theme Engine v4 Performance — BuildFix28 CP13

Material fast paths:
- STATIC layers skip timeline/transform animation work.
- Invisible/context-suppressed layers skip draw.
- Safe offscreen layers skip draw.
- Alpha-zero layers skip draw.
- v1-v3 never enter scene-v4 evaluation.
- No per-frame allocation or FileXio.

Composition is layer-based; there is no fullscreen-video backend and no requirement for fullscreen frame sequences. Transform animation is bounded declarative translation/scale/rotation/alpha. Independent layer periods use per-layer duration/phase and LOOP/PINGPONG/ONCE modes. HOME/TOP deltas are driven by the shell's real `top_menu_transition`, avoiding an independent transition clock.

Rotation uses a fixed EE staging surface only when rotation is non-zero; rotation=0 takes the direct asset path. Layer transfer uses the existing TitanVideo transient texture scratch so 720p does not reserve additional GS VRAM.

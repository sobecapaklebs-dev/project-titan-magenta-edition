# Titan UL In-Game Overlay — BF28 CP07

Titan UL now contains a resident Overlay Lite backend in `ee_core`. The existing IGR chord is split into a non-destructive overlay path and the original destructive return path. A title may enter MINIMAL overlay only when the launcher supplies a proven `CONTEXT2_SAFE` compatibility profile. UNKNOWN titles remain RETURN_ONLY until title-specific safety is established.

The renderer uses the active DISPFB framebuffer and GS context 2 only. It does not load HOME, Theme Engine, textures, filesystem content, or allocate per frame. The game is paused by tracking successfully suspended EE threads. Resume restores those threads without ResetEE, GS reset, IOP reboot, or OPL teardown. Return resumes into the existing VBLANK destructive prelude so OPL's proven cleanup remains authoritative.

Modern controller protocol parsing does not exist in the EE core. PADEMU/Titan input normalization presents controller state through the same DS2-compatible pad buffer consumed by the resident hook.

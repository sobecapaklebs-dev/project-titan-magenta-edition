# Metal backend — Phase 1 spike artifacts

Reproducible GO/NO-GO de-risking spikes for the native Metal backend
(see `docs/design/METAL_BACKEND_PLAN.md`, Phase 1).

## Spike A — cross-backend CPU-pipeline invariance ✅ GO
The CPU display-list interpreter + T&L run identically regardless of backend.
Verified by comparing the sim-state hash of the SAME deterministic run on GL
vs Metal (faithful, RenderScale=1):

```
env GE007_DETERMINISTIC_STABLE_COUNT=1 SDL_AUDIODRIVER=dummy GE007_MUTE=1 \
    GE007_BACKGROUND=1 GE007_NO_VSYNC=1 GE007_NO_INPUT_GRAB=1 GE007_DISABLE_LEVEL_INTRO=1 \
    [GE007_RENDERER=metal] ./build/ge007 --rom baserom.u.z64 --level 33 \
    --deterministic --screenshot-frame 2 --screenshot-exit \
    --config-override Video.RemasterFX=0 --config-override Video.RenderScale=1 \
    --sim-state-hash-out hash.json
```
Result: GL and Metal both → `5c2983a3f0b7345f` (identical) at frame 2. The hash
even folds render bookkeeping, and it still matches — so the `g_depth_clamp_enabled`
hoist (gfx_pc.c) keeps tri-admission identical across backends. (Metal is also
deterministic run-to-run; GL-over-Metal hangs at later frames — the motivating bug.)

## Spike B — RDP/XLU framebuffer-snapshot round-trip ✅ GO
The port's per-batch XLU snapshot + RDP-memory blend samples a color attachment
mid-frame. In Metal that means blit-copy color-attachment → sampled texture, then
sample it in a fragment. `spike_b_xlu_snapshot_roundtrip.mm` proves the round-trip:

```
clang++ -x objective-c++ -std=gnu++17 -fobjc-arc -isysroot "$(xcrun --show-sdk-path)" \
    -framework Metal -framework Foundation \
    tools/metal/spike_b_xlu_snapshot_roundtrip.mm -o /tmp/spikeB && /tmp/spikeB
```
Result: sampled (128,64,191) == expected exactly → GO.

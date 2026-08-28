<!-- Authored 2026-07-04. Purpose: record how frame timing actually works in the
native port — the two independent 60Hz gates, why the benchmark reports ~120 while
play shows 60, and why "just uncap it" is a sim rewrite, not a config flag. Written
after a trace prompted by "why is play capped at 60 when benchmarks show 120?" and
the follow-up "are there hard blockers to 120fps?". Companion docs:
docs/design/PERFORMANCE_PLAN.md (budgets + harness), docs/RENDERING_ARCHITECTURE.md. -->

# MGB64 Frame Timing & Frame-Rate Architecture (native port)

The mental model for how fast the game runs, why it's pinned to 60, and what it
would actually take to go higher. Deliberately short. If you only remember one
thing:

> **The simulation is a fixed 60 Hz integer-tick model inherited from the N64.
> Rendering more frames does not make the game run faster or smoother on its own —
> the sim cannot advance in fractions of a 1/60 s tick.**

---

## 1. The two numbers that look like a contradiction

You will see the game described as both "120 fps" and "60 fps." Both are correct;
they measure different things.

| | Benchmark "~120 fps" | In-game "60 fps" |
|---|---|---|
| Source | `tools/perf_census.sh` | `Video.FpsOverlay` HUD |
| Formula | `1000 / work_ms` | real frame-to-frame `dt` |
| Means | "a frame's *work* finishes in ~8.3 ms" | "a frame is *presented* every ~16.7 ms" |
| VSync | off (`GE007_NO_VSYNC=1`) | adaptive (default) |
| Cap | none (measuring raw work) | 60 Hz, twice over (§3) |

The census reports **render/sim cost**, not presented frames. `work_ms` is measured
*before* the pacing sleep and excludes it (`platform_sdl.c:2802-2803`, with
`g_lastFrameTime` reset *after* the wait at `:2832`), then inverted by
`fps() = 1000/work_ms` (`tools/perf_census.sh:121`). So "120" means "each frame's
work took ~8.3 ms" — i.e. **headroom**, exactly as `docs/design/PERFORMANCE_PLAN.md §6`
intends ("target = 120 fps / 8.3 ms — headroom over 60 for combat/effects/min-spec").
It was never a plan to *present* 120 frames.

The in-game overlay (`pc_fps_overlay.c`, default ON, `platform_sdl.c:1612`) reads
true wall-clock `dt` via `platformFrameStatsGet()`, so it honestly shows the paced
60. It exists partly as the visible cross-check against the census number.

---

## 2. The sim timestep: fixed 60 Hz integer ticks

The game advances the world by an **integer** count of 1/60 s ticks each frame,
held in `g_ClockTimer` (`s32`, `lvl.c:364`). It is a physics/AI step, not a
display value.

```
osGetCount() ── wall clock @46.875 MHz ──▶ waitForNextFrame()  (unk_0C0A70.c:176)
                                             busy-waits until ≥1 whole tick elapsed:
                                             nextFrameTime = (Δcount + 387937) / 775875
                                             } while (nextFrameTime < frameDelay=1)
                                                        │
                          updateFrameCounters(nextFrameTime)  → speedgraphframes
                                                        │
   lvl.c:2107   g_ClockTimer = speedgraphframes   (clamped 1..4 on PC, :2118-2122)
   lvl.c:2129   g_GlobalTimer += g_ClockTimer      ← the 60Hz master clock
```

`775875` counts @ 46.875 MHz ≈ **16.55 ms ≈ one 1/60 s tick**. `osGetCount()` in
normal play is real `CLOCK_MONOTONIC` (`stubs.c:257-259`), so `waitForNextFrame`
literally spins until 1/60 s of real time has passed before the sim advances.

`g_ClockTimer` is then consumed as an integer throughout the game logic — this is
what makes it unremovable:

- **looped** as a substep count — `for (i2=0; i2<g_ClockTimer; i2++)` (`bondview.c:6675`)
- **subtracted** from aim/AI cooldowns — `autoyaimtime60 -= g_ClockTimer` (`bondview.c:7092`)
- **treated as paused** when zero — `if (g_ClockTimer == 0)` across `explosions.c`, movement, gravity
- **accumulated** into `g_GlobalTimer`, the master clock behind objective timing, scripted guard behavior, and cutscenes

At 120 fps each render frame sees ~half a tick. The engine can only round it:
`g_ClockTimer = 0` (sim doesn't advance → duplicate frames, entities read as
paused) or `g_ClockTimer = 1` (a full 1/60 s advance twice as often → **the game
runs at 2× speed**). There is no in-between it can represent.

---

## 3. Two independent 60 Hz gates

The loop is pinned to 60 in *two* places. Removing either alone changes nothing.

1. **Game logic** — `waitForNextFrame()` busy-waits ~16.55 ms of real time
   (§2). Upstream of, and independent of, any platform setting.
2. **Platform pacer** — `platformFrameSync()` holds a sub-ms absolute deadline at
   16.667 ms (`platform_sdl.c:2790-2830`), plus VSync / `displaySyncEnabled`.

Consequence: **`Video.FrameCap=display` does not get you above 60 in gameplay.** It
removes gate #2, but gate #1 still spins the loop to 60 using the wall clock. (An
earlier verbal claim that a 120 Hz panel would yield 120 was wrong — it ignored
gate #1.)

Deterministic/headless mode is the exception that explains the benchmark: it
replaces `osGetCount` with a synthetic clock that jumps a **full** tick per call
(`SYNTHETIC_TICKS_PER_FRAME = 46875000/60`, `stubs.c:230`), so `waitForNextFrame`
returns instantly with `nextFrameTime=1`. The loop then runs as fast as rendering
allows — one sim-tick per frame, wall clock decoupled. That is why the census can
measure 8.3 ms work ("120") while the sim semantics stay exactly 60 Hz.

---

## 4. What is *not* the blocker

**Render cost.** The `perf/make-it-rip` pass is present in this tree by content —
see `gfx_opengl_compute_batch_snapshot_rect` (`src/platform/fast3d/gfx_opengl.c`)
and `g_room_cmd_cache_generation` (`src/platform/fast3d/gfx_pc.c`); the merge hash
once cited here was private-lineage and does not exist on this repo's lineage.
Post-fix, all 20 levels do ~100–189 fps of render *work* on an M3 Max (jungle
18→131, dam 50→137, see `docs/design/PERFORMANCE_PLAN.md`). There is comfortable
headroom to *draw* frames faster than 60. The wall is purely the sim timestep,
not the GPU.
(Min-spec HW and the Metal first-sight shader-compile hitch are separate, softer
concerns — not the categorical blocker.)

---

## 5. Paths to >60 fps (if ever pursued)

| Path | What it is | Effort | Risk |
|---|---|---|---|
| **A. Sim 60 + render-interpolated 120** | Keep the sim bit-exact at 60 Hz; snapshot entity/camera/viewmodel transforms each tick and **lerp** between the two latest for display only | High — GE's renderer reads live game state, so this needs a whole interpolation snapshot layer (chars, projectiles, particles, viewmodel, camera) | Low — sim stays exact, so **determinism, RAMROM, and netplay are preserved**. The faithful approach. |
| **B. Sim at 120 (fractional dt)** | Convert `g_ClockTimer`/`g_GlobalTimer` to fractional time throughout | Very high — touches movement, collision, gravity, AI cooldowns, animation, and the objective/cutscene clock | **Breaks determinism / RAMROM / netplay**; movement & collision are tuned to integer 60 Hz steps → likely physics divergences. Not worth it. |
| **C. Ship 60** | The N64-faithful rate | Zero | Zero |

Only **A** is worth considering, and it is a real project, not a tweak: the win is
smoother *presentation* (camera/viewmodel/motion at 120 Hz) while gameplay stays a
faithful, deterministic 60 Hz sim. B trades away everything netplay and RAMROM
replay depend on.

---

## 6. Config reference

Registered in `platformRegisterConfig()` (`platform_sdl.c`):

- `Video.VSync` — default `adaptive` (`:1598`); on Metal → `CAMetalLayer.displaySyncEnabled`
- `Video.FrameCap` — default `60`; values `30 | 60 | display`. `display` uncaps the
  *platform pacer* only when VSync is on (`:1531-1535`) — but gate #1 still holds
  the sim at 60, so this does **not** raise the in-game frame rate today.
- `Video.FpsOverlay` — default `1` (on); the real-`dt` HUD counter
- `GE007_NO_VSYNC=1` — env override forcing swap interval 0 (used by the census)

There is no `120` frame-cap value and no way to exceed 60 unique gameplay frames
per second through config alone — by design, see §2–§3.

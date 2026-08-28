/*
 * frame_stats.h — T11 FPS-overlay timing source (platform layer, sim-pure).
 *
 * Measures REAL wall-clock frame-to-frame time from the platform frame loop
 * (platformFrameSync in platform_sdl.c), using SDL's monotonic performance
 * counter. This is deliberately separate from the simulation clock: it never
 * reads g_GlobalTimer or any sim state, and it consumes no RNG, so it cannot
 * perturb --deterministic replays or the sim-invariance hash gate even when
 * it IS drawn (and it draws NOTHING by default in those sessions — see the
 * suppression gate in src/game/pc_fps_overlay.c).
 *
 * Two aggregation windows, both computed from one fixed-capacity ring buffer
 * (no per-frame heap allocation):
 *   - a ~0.25s window for the headline FPS + frame-time-ms average (matches
 *     the "updates ~4x/sec" HUD-legibility requirement — the ring is sampled
 *     every frame, but the PUBLISHED snapshot below only refreshes at that
 *     cadence, so a consumer polling every frame still only sees a new
 *     number ~4 times a second);
 *   - a ~2s rolling window for the 1%-low FPS (industry-standard metric:
 *     average FPS of the slowest 1% of frames observed in the window).
 *
 * `generation` increments only when the published snapshot changes, so a
 * consumer (the overlay widget) can skip reformatting text on frames where
 * nothing new was published, without doing its own float comparisons.
 */
#ifndef _PLATFORM_FRAME_STATS_H_
#define _PLATFORM_FRAME_STATS_H_

#include <ultra64.h>

typedef struct PlatformFrameStats {
    f32 fps;        /* current FPS, averaged over the ~0.25s publish window   */
    f32 frame_ms;   /* current frame time (ms), averaged over the same window */
    f32 low1_fps;   /* 1%-low FPS over the trailing ~2s rolling window        */
    u32 generation; /* increments each time a new snapshot is published       */
} PlatformFrameStats;

/* Call once per real (screen-level) frame from platformFrameSync, as early
 * as possible in the loop (before the pacing wait), so the measured interval
 * reflects the full displayed frame period. Cheap: one SDL_GetPerformanceCounter
 * read plus a ring-buffer write; the O(n log n) 1%-low sort only runs when the
 * ~0.25s publish window elapses (~4x/sec), not every frame. */
void platformFrameStatsTick(void);

/* Read-only accessor for the latest published snapshot. Safe to call every
 * frame (returns a pointer to a static struct; no allocation, no side effects). */
const PlatformFrameStats *platformFrameStatsGet(void);

#endif /* _PLATFORM_FRAME_STATS_H_ */

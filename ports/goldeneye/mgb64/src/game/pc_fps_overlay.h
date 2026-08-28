#ifndef _GAME_PC_FPS_OVERLAY_H_
#define _GAME_PC_FPS_OVERLAY_H_

/*
 * T11 — FPS overlay: draw-only DL widget, screen-level (once per real frame,
 * not once per split-screen pane), top-right corner. Mirrors the W5.E2.T2
 * settings-menu widget pattern (see pc_settings_menu.c/.h): sim-pure (reads
 * only the platform frame-stats snapshot in src/platform/frame_stats.h + the
 * Video.FpsOverlay setting's backing global), bounded snprintf, and zero
 * display-list bytes whenever suppressed or disabled.
 *
 * NATIVE_PORT-only, same as the settings menu.
 */

#ifdef NATIVE_PORT

#include <ultra64.h>

/*
 * Draws current FPS (~0.25s average), frame time in ms (same window), and
 * a 1%-low FPS over a rolling ~2s window, small and unobtrusive in the
 * screen's top-right corner. Text refreshes ~4x/sec (it does not re-format
 * on every call), matching the platform layer's publish cadence.
 *
 * CRITICAL — returns `gdl` completely untouched (zero display-list bytes,
 * no side effects) when ANY of the following hold, so every byte-identity /
 * parity / sim-invariance harness in the repo is unaffected:
 *   - g_deterministic is set (the process was launched with --deterministic)
 *   - GE007_BACKGROUND=1 is set in the environment
 *   - this process was launched with --screenshot-frame
 *   - the Video.FpsOverlay setting is 0
 * --faithful and --faithful-hd do NOT suppress it: this is an app-level HUD
 * feature (like window size), not part of the original game's LOOK.
 *
 * Call once per real frame from the per-player HUD pass in bondview.c,
 * directly AFTER pcSettingsMenuRenderForced() so it draws topmost. It is
 * safe to call this from every split-screen pane's pass: internally it
 * draws only once per real frame (a g_frame_count_diag-keyed latch), and it
 * positions itself using the full-screen viGetX()/viGetY() bounds rather
 * than the calling pane's viewport, so split-screen still shows exactly one
 * instance, in the true top-right corner of the window.
 */
Gfx *pcFpsOverlayRender(Gfx *gdl);

#endif /* NATIVE_PORT */
#endif /* _GAME_PC_FPS_OVERLAY_H_ */

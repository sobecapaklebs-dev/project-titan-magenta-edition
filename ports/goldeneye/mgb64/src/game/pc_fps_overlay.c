/*
 * T11 — Industry-standard FPS overlay: draw-only DL widget.
 *
 * Same architecture as the W5.E2.T2 settings-menu widget (pc_settings_menu.c):
 * a pure-DL renderer hooked into the per-player HUD pass in bondview.c,
 * sim-pure (no writes to simulation state, no RNG consumption), bounded
 * snprintf, zero display-list bytes emitted whenever suppressed/disabled.
 *
 * Timing source is the platform layer (src/platform/frame_stats.h): real
 * wall-clock frame-to-frame time measured off SDL's performance counter in
 * platformFrameSync, NOT the simulation clock. This module only reads the
 * published snapshot; it never touches g_GlobalTimer or anything that would
 * make it observable to a replay/hash gate.
 */

#ifdef NATIVE_PORT

#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include "pc_fps_overlay.h"
#include "textrelated.h"           /* microcode_constructor(_related_to_menus), textRender,
                                      combiner_bayer_lod_perspective, ptrFontZurich* */
#include "../platform/frame_stats.h"

/* Full-screen (not per-pane) viewport accessors — see src/fr.h. Forward-
 * declared here to avoid pulling the whole video header into this small
 * module, same idiom as pc_settings_menu.c. viGetX()/viGetY() are the FULL
 * screen dimensions (not the current split-screen pane's viGetView* rect),
 * which is exactly what a screen-level, draw-once overlay needs. */
extern s16 viGetX(void);
extern s16 viGetY(void);

/* Backing global for Video.FpsOverlay (registered in platform_sdl.c next to
 * the other Video.* settings; GE007_FPS_OVERLAY env override wins via the
 * standard settingsRegisterInt env-override idiom). Read directly here
 * rather than via settingsFind(), mirroring how gun.c reads g_pcHitMarkers
 * for Input.HitMarkers -- this widget has exactly one dedicated setting, so
 * a per-frame registry string lookup would be pure overhead. */
extern s32 g_pcFpsOverlay;

/* CRITICAL suppression gate inputs. */
extern int g_deterministic;                /* --deterministic                */
extern int g_screenshotFrameSessionActive; /* sticky: --screenshot-frame was passed */

/* Screen-level frame counter (gfx_pc.c), incremented once per real displayed
 * frame -- NOT per split-screen pane. Used purely as a "have I already drawn
 * this frame" latch key; never read for timing/formatting. */
extern int g_frame_count_diag;

/*
 * NOTE on coordinate space: viGetX()/viGetY() are NOT window pixels -- they
 * are the small fixed N64-logical HUD canvas (320x240, or 440x330 in the
 * widescreen camera-buffer mode; see getWidth320or440()/bondview.c), which
 * the renderer then scales up to the real window/render-target resolution.
 * The settings-menu widget draws in this same logical space (its viGetView*
 * accessors are the per-pane subdivision of it). Two lines are used instead
 * of one so each line's text comfortably fits within that narrow logical
 * width without running past the screen's right edge (where it would be
 * silently clipped instead of wrapping).
 */
#define FPS_PAD      6     /* inset from the logical screen edge   */
#define FPS_W        108   /* backdrop width (logical units)        */
#define FPS_LINEH    12    /* line pitch (logical units)             */
#define FPS_H        (FPS_LINEH * 2 + 6)  /* backdrop height: 2 lines + padding */
#define FPS_COL_BG   0x00000090  /* translucent dark backdrop, matches the settings-menu panel idiom but lighter/smaller (unobtrusive HUD readout, not a menu) */

/* Draw `str` at (x,y) in Zurich bold, white -- identical idiom to
 * pc_settings_menu.c's smText() (itself the credits/HUD text idiom,
 * bondview.c:20055-20062): a combiner-arming transparent box, then text. */
static Gfx *fpsText(Gfx *gdl, s32 x, s32 y, const char *str) {
    s32 tx = x;
    s32 ty = y;
    s16 vx = viGetX();
    s16 vy = viGetY();
    gdl = microcode_constructor_related_to_menus(gdl, x, y - 1, x + 1, y + 1, 0);
    gdl = textRender(gdl, &tx, &ty, (char *)str,
                     ptrFontZurichBoldChars, ptrFontZurichBold, -1, vx, vy, 0, 0);
    return gdl;
}

/* Env-flag idiom matching platformEnvFlagEnabled() in platform_sdl.c: unset
 * or empty = false; the literal string "0" = false; anything else = true. */
static int fpsOverlayEnvFlagTrue(const char *name) {
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    if (value[0] == '0' && value[1] == '\0') {
        return 0;
    }
    return 1;
}

Gfx *pcFpsOverlayRender(Gfx *gdl) {
    static s32 s_lastDrawnFrame = -1;
    static u32 s_lastGeneration = 0xFFFFFFFFu;
    static char s_line1[32] = "";  /* "999 FPS 999.9ms"  */
    static char s_line2[32] = "";  /* "1% low 999 FPS"   */
    const PlatformFrameStats *stats;
    s32 x0, y0, x1, y1;

    if (gdl == NULL) {
        return gdl;
    }

    /* --- CRITICAL: zero display-list bytes under any of these. Checked
     * first and unconditionally, before the once-per-frame latch even
     * updates, so a suppressed session's DL output (and g_frame_count_diag
     * bookkeeping below) is identical to a build that never had this file. */
    if (g_deterministic) {
        return gdl;
    }
    if (g_screenshotFrameSessionActive) {
        return gdl;
    }
    if (fpsOverlayEnvFlagTrue("GE007_BACKGROUND")) {
        return gdl;
    }
    if (!g_pcFpsOverlay) {
        return gdl;
    }

    /* Screen-level once-per-real-frame latch. Our hook site (maybe_mp_interface,
     * called from bondview.c) runs once per split-screen pane per real frame;
     * g_frame_count_diag advances once per real displayed frame regardless of
     * player count (gfx_pc.c), so this draws exactly once per frame no matter
     * which pane's pass happens to reach us first -- satisfying "draw once,
     * screen-level, not per-pane" without depending on player-index/shuffle
     * semantics. */
    if (s_lastDrawnFrame == g_frame_count_diag) {
        return gdl;
    }
    s_lastDrawnFrame = g_frame_count_diag;

    stats = platformFrameStatsGet();

    /* Reformat only when the platform layer actually published a new ~0.25s
     * snapshot (generation bump), not every frame this widget draws. */
    if (stats->generation != s_lastGeneration) {
        s_lastGeneration = stats->generation;
        snprintf(s_line1, sizeof(s_line1), "%3.0f FPS %4.1fms",
                 (double)stats->fps, (double)stats->frame_ms);
        snprintf(s_line2, sizeof(s_line2), "1%% low %3.0f",
                 (double)stats->low1_fps);
    }

    /* Reset RDP microcode/cycle state before drawing -- same prologue the
     * settings-menu widget uses, so this never inherits whatever cycle-type/
     * combiner state the last thing drawn this frame left behind. */
    gdl = microcode_constructor(gdl);

    x1 = (s32)viGetX() - FPS_PAD;
    y0 = FPS_PAD;
    x0 = x1 - FPS_W;
    y1 = y0 + FPS_H;

    gdl = microcode_constructor_related_to_menus(gdl, x0, y0, x1, y1, FPS_COL_BG);
    gdl = fpsText(gdl, x0 + 4, y0 + 3, s_line1);
    gdl = fpsText(gdl, x0 + 4, y0 + 3 + FPS_LINEH, s_line2);

    gdl = combiner_bayer_lod_perspective(gdl);
    return gdl;
}

#endif /* NATIVE_PORT */

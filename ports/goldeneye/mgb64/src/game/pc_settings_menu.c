/*
 * W5.E2.T1 — In-game Remaster Settings menu: static curation table (model).
 *
 * NATIVE_PORT-only. Data + a read accessor ONLY; no renderer, no input
 * controller, no front-end/overlay mount (those are W5.E2.T2..T6). Because
 * nothing on the shipping code path references this table yet, the module is
 * compiled but never called, so it changes NOTHING at runtime (doc 05 §8
 * overlay-closed identity is trivially preserved).
 *
 * The table curates ~55 of the registered settings keys (platform_sdl.c +
 * audio_pc.c) across 7 pages. Widget is authored per row (nominally the widget
 * Setting.type would derive; SMW_ENUM is used for a couple of ranged ints that
 * are really modes). Window-geometry keys (Video.WindowX/Y, Display, HiDPI,
 * Fullscreen*, WindowWidth/Height) stay ini-only and are intentionally absent.
 *
 * apply hooks are all NULL here: the live-apply audit (which wires
 * platformApplyWindowMode / platformApplyVSync, etc.) is W5.E2.T5, not this task.
 *
 * SM_FUTURE rows (the W6 audio bus + the today-dead Audio.MasterVolume) name
 * keys that settingsFind() may not resolve yet; they render hidden (T2) and are
 * whitelisted from the registration check by tools/check_settings_menu_model.py.
 */

#ifdef NATIVE_PORT

#include <ultra64.h>
#include <stdlib.h>
#include <stdio.h>
#include "pc_settings_menu.h"
#include "textrelated.h"          /* microcode_constructor_related_to_menus, textRender,
                                     combiner_bayer_lod_perspective, ptrFontZurich* */
#include "../platform/settings.h" /* settingsFind, Setting, settingsEnumTokenForValue */

/* Per-pane viewport accessors (declared in src/fr.h; forward-declared here to
 * avoid pulling the whole video header into this small module). */
extern s16 viGetX(void);
extern s16 viGetY(void);
extern s16 viGetViewLeft(void);
extern s16 viGetViewTop(void);
extern s16 viGetViewWidth(void);
extern s16 viGetViewHeight(void);

/* Self-contained count macro (avoids pulling bondconstants.h just for ARRAYCOUNT). */
#define SM_ARRAY_COUNT(a) ((s32)(sizeof(a) / sizeof((a)[0])))

/* ---- Page 1: Display -------------------------------------------------------- */
static const SmEntry s_pageDisplay[] = {
    { "Video.WindowMode",   SMW_ENUM,   0.0f,  NULL, "Windowed / borderless / fullscreen", 0 },
    { "Video.VSync",        SMW_ENUM,   0.0f,  NULL, "Vertical sync mode",                 0 },
    { "Video.FrameCap",     SMW_ENUM,   0.0f,  NULL, "Frame-rate cap",                     0 },
    { "Video.RenderScale",  SMW_SLIDER, 0.25f, NULL, "Internal resolution multiplier",     0 },
    { "Video.MSAA",         SMW_ENUM,   0.0f,  NULL, "Multisample anti-aliasing",          0 },
    { "Video.FovY",         SMW_SLIDER, 1.0f,  NULL, "Vertical field of view",             0 },
    { "Video.ViewmodelFov", SMW_SLIDER, 1.0f,  NULL, "Weapon viewmodel field of view",     0 },
    { "Video.RetroFilter",  SMW_ENUM,   0.0f,  NULL, "Texture filtering style",            0 },
};

/* ---- Page 2: Image ---------------------------------------------------------- */
static const SmEntry s_pageImage[] = {
    { "Video.Gamma",        SMW_SLIDER, 0.05f, NULL, "Output gamma",       0 },
    { "Video.Saturation",   SMW_SLIDER, 0.05f, NULL, "Colour saturation",  0 },
    { "Video.Contrast",     SMW_SLIDER, 0.05f, NULL, "Contrast",           0 },
    { "Video.Brightness",   SMW_SLIDER, 0.02f, NULL, "Brightness offset",  0 },
    { "Video.OutputDither", SMW_TOGGLE, 0.0f,  NULL, "Output dithering",   0 },
    { "Video.GradePresets", SMW_TOGGLE, 0.0f,  NULL, "Colour-grade preset",0 },
    { "Video.Tonemap",      SMW_TOGGLE, 0.0f,  NULL, "Filmic tonemap",     0 },
};

/* ---- Page 3: Post-FX -------------------------------------------------------- */
static const SmEntry s_pagePostFx[] = {
    { "Video.RemasterFX",     SMW_TOGGLE, 0.0f,  NULL, "Master remaster post-FX toggle", 0 },
    { "Video.Bloom",          SMW_TOGGLE, 0.0f,  NULL, "Bloom",                          0 },
    { "Video.BloomThreshold", SMW_SLIDER, 0.05f, NULL, "Bloom threshold",                0 },
    { "Video.BloomIntensity", SMW_SLIDER, 0.05f, NULL, "Bloom intensity",                0 },
    { "Video.Vignette",       SMW_SLIDER, 0.05f, NULL, "Vignette strength",              0 },
    { "Video.Fxaa",           SMW_TOGGLE, 0.0f,  NULL, "FXAA anti-aliasing",             0 },
    { "Video.Sharpen",        SMW_SLIDER, 0.05f, NULL, "Sharpen amount",                 0 },
    { "Video.FogDensity",     SMW_SLIDER, 0.05f, NULL, "Fog density scale",              0 },
};

/* ---- Page 4: Lighting ------------------------------------------------------- */
static const SmEntry s_pageLighting[] = {
    { "Video.Ssao",             SMW_TOGGLE, 0.0f,  NULL, "Ambient occlusion (SSAO)",      0 },
    { "Video.SsaoRadius",       SMW_SLIDER, 0.05f, NULL, "SSAO sample radius",            0 },
    { "Video.SsaoIntensity",    SMW_SLIDER, 0.05f, NULL, "SSAO intensity",                0 },
    { "Video.SsaoBias",         SMW_SLIDER, 0.01f, NULL, "SSAO depth bias",               0 },
    { "Video.SsaoPower",        SMW_SLIDER, 0.1f,  NULL, "SSAO falloff power",            0 },
    { "Video.SsaoFarCutoff",    SMW_SLIDER, 8.0f,  NULL, "SSAO far cutoff distance",      0 },
    { "Video.SsaoHalfRes",      SMW_TOGGLE, 0.0f,  NULL, "Half-resolution SSAO",          0 },
    { "Video.SsaoBlur",         SMW_TOGGLE, 0.0f,  NULL, "SSAO blur pass",                0 },
    { "Video.EnvSmoothNormals", SMW_TOGGLE, 0.0f,  NULL, "Smoothed environment normals",  0 },
    { "Video.EnvRelightBlend",  SMW_SLIDER, 0.05f, NULL, "Relight blend amount",          0 },
};

/* ---- Page 5: Input ---------------------------------------------------------- */
static const SmEntry s_pageInput[] = {
    { "Input.MouseSensitivity",    SMW_SLIDER, 0.01f,  NULL, "Mouse look sensitivity",        0 },
    { "Input.MouseSensitivityAim", SMW_SLIDER, 0.005f, NULL, "Mouse sensitivity while aiming",0 },
    { "Input.InvertY",             SMW_TOGGLE, 0.0f,   NULL, "Invert vertical look",          0 },
    { "Input.GamepadLookSpeed",    SMW_SLIDER, 0.5f,   NULL, "Gamepad look speed",            0 },
    { "Input.GamepadLookCurve",    SMW_SLIDER, 0.1f,   NULL, "Gamepad response curve",        0 },
    { "Input.GamepadDeadzone",     SMW_SLIDER, 0.05f,  NULL, "Gamepad stick deadzone",        0 },
    { "Input.SteadyView",          SMW_TOGGLE, 0.0f,   NULL, "Steady-view stabilisation",     0 },
    { "Input.Rumble",              SMW_TOGGLE, 0.0f,   NULL, "Controller rumble",             0 },
    { "Input.RumbleIntensity",     SMW_SLIDER, 1.0f,   NULL, "Rumble intensity",              0 },
};

/* ---- Page 6: HUD & Aim ------------------------------------------------------ */
static const SmEntry s_pageHud[] = {
    { "Input.AdsEnabled",            SMW_TOGGLE, 0.0f,  NULL, "Modern aim-down-sights",        0 },
    { "Input.AdsSensitivity",        SMW_SLIDER, 0.05f, NULL, "ADS sensitivity trim",          0 },
    { "Input.AdsModernReticle",      SMW_TOGGLE, 0.0f,  NULL, "Modern ADS reticle",            0 },
    { "Input.ViewmodelSway",         SMW_SLIDER, 0.05f, NULL, "Viewmodel sway amount",         0 },
    { "Input.ModernCrosshair",       SMW_TOGGLE, 0.0f,  NULL, "Modern crosshair",              0 },
    { "Input.HitMarkers",            SMW_TOGGLE, 0.0f,  NULL, "Hit markers",                   0 },
    { "Input.ReticleTargetFeedback", SMW_TOGGLE, 0.0f,  NULL, "Reticle target feedback",       0 },
    { "Input.MinimapEnabled",        SMW_TOGGLE, 0.0f,  NULL, "Minimap",                       0 },
    { "Input.MinimapMode",           SMW_ENUM,   0.0f,  NULL, "Minimap reveal mode",           0 },
    { "Input.MinimapObjectives",     SMW_TOGGLE, 0.0f,  NULL, "Show objectives on minimap",    0 },
    { "Input.MinimapOpacity",        SMW_SLIDER, 0.05f, NULL, "Minimap opacity",               0 },
    { "Input.MinimapSize",           SMW_SLIDER, 0.05f, NULL, "Minimap size",                  0 },
};

/* ---- Page 7: Audio ---------------------------------------------------------- *
 * Audio.MasterVolume is registered today but does not affect the main synth mix
 * (doc 06 §2.4) — tagged SM_FUTURE so W5 never surfaces a dead slider; W6.E3.T1
 * lands the live wiring. The remaining volume/reverb/width/output rows are the
 * W6 audio bus (doc 06 §4.1): registered by W6.E3, hidden until then. Only
 * Audio.DeviceSamples is a live, currently-registered audio key.               */
static const SmEntry s_pageAudio[] = {
    { "Audio.MasterVolume", SMW_SLIDER, 0.05f, NULL, "Master volume",            SM_FUTURE },
    { "Audio.MusicVolume",  SMW_SLIDER, 0.05f, NULL, "Music volume",             SM_FUTURE },
    { "Audio.SfxVolume",    SMW_SLIDER, 0.05f, NULL, "Sound-effects volume",     SM_FUTURE },
    { "Audio.RoomReverb",   SMW_SLIDER, 0.05f, NULL, "Room reverb amount",       SM_FUTURE },
    { "Audio.StereoWidth",  SMW_SLIDER, 0.05f, NULL, "Stereo width",             SM_FUTURE },
    { "Audio.OutputFilter", SMW_ENUM,   0.0f,  NULL, "Output low-pass filter",   SM_FUTURE },
    { "Audio.OutputRate",   SMW_ENUM,   0.0f,  NULL, "Output sample rate",       SM_FUTURE },
    { "Audio.DeviceSamples",SMW_SLIDER, 128.0f,NULL, "Audio buffer size",        0 },
};

/* ---- Page table (7 pages) --------------------------------------------------- */
const SmPage g_smPages[] = {
    { "Display",   s_pageDisplay,  8 },
    { "Image",     s_pageImage,    7 },
    { "Post-FX",   s_pagePostFx,   8 },
    { "Lighting",  s_pageLighting, 10 },
    { "Input",     s_pageInput,    9 },
    { "HUD & Aim", s_pageHud,      12 },
    { "Audio",     s_pageAudio,    8 },
};

/* Compile-time self-consistency: each page's declared count == its array length,
 * and the model curates exactly 7 pages. A drift here is a build error, not a
 * runtime surprise. (tools/check_settings_menu_model.py verifies the same by
 * parsing, so C and tooling agree.) */
_Static_assert(SM_ARRAY_COUNT(s_pageDisplay)  == 8,  "Display page count");
_Static_assert(SM_ARRAY_COUNT(s_pageImage)    == 7,  "Image page count");
_Static_assert(SM_ARRAY_COUNT(s_pagePostFx)   == 8,  "Post-FX page count");
_Static_assert(SM_ARRAY_COUNT(s_pageLighting) == 10, "Lighting page count");
_Static_assert(SM_ARRAY_COUNT(s_pageInput)    == 9,  "Input page count");
_Static_assert(SM_ARRAY_COUNT(s_pageHud)      == 12, "HUD page count");
_Static_assert(SM_ARRAY_COUNT(s_pageAudio)    == 8,  "Audio page count");
_Static_assert(SM_ARRAY_COUNT(g_smPages)      == 7,  "settings menu curates 7 pages");

const SmPage *pcSettingsMenuPages(s32 *outCount)
{
    if (outCount != NULL) {
        *outCount = SM_ARRAY_COUNT(g_smPages);
    }
    return g_smPages;
}

/* ==========================================================================
 * W5.E2.T2 — pure-DL widget renderer (doc 05 §4.2 "Renderer")
 *
 * All rectangles and text go through the two proven in-mission HUD primitives:
 *   - microcode_constructor_related_to_menus(gdl, x0,y0,x1,y1, rgba8888): a
 *     translucent G_RM_XLU_SURF quad drawn from PRIM colour (RGBA packed as
 *     R<<24|G<<16|B<<8|A). It leaves the combiner armed for text (TEXEL0*PRIM),
 *     exactly as the credits path uses it (bondview.c:20055).
 *   - textRender(...) with ptrFontZurichBold*: the same call the credits/HUD
 *     path makes (bondview.c:20062); arg6 = -1 => white PRIM colour.
 * A trailing combiner_bayer_lod_perspective() restores standard render state,
 * mirroring the credits epilogue (bondview.c:20128). No settex / no texture
 * state is touched, so the overlay cannot corrupt the scene it draws over.
 * ========================================================================== */

/* RGBA8888 palette (alpha < 0xFF => translucent over the scene). */
#define SM_COL_PANEL   0x0E1018D8  /* dark blue-grey panel body            */
#define SM_COL_ACCENT  0xF2B84AFF  /* amber title/header accent underline  */
#define SM_COL_SEL     0x3A5AA070  /* selected-row wash                    */
#define SM_COL_TRACK   0x2A2E3AC0  /* slider track                         */
#define SM_COL_FILL    0x4FA6FFE8  /* slider filled portion (blue)         */
#define SM_COL_TOG_ON  0x38D06AE0  /* toggle ON box (green)                */
#define SM_COL_TOG_OFF 0x4A4E5AC0  /* toggle OFF box (grey)                */
#define SM_COL_ENUM    0x232742C0  /* enum value box                       */
#define SM_COL_ACTION  0x30407AD0  /* action button box                    */

#define SM_PAD    10   /* panel inner padding (px)      */
#define SM_TITLEH 26   /* title band height (px)        */
#define SM_ROWH   16   /* per-row pitch (px)            */

/* One log line per unique unregistered key (doc 05 §4.2 "hidden, one log line,
 * no crash"). Deduped by literal pointer so a per-frame render never spams. */
static void smLogHiddenOnce(const char *key)
{
    static const char *seen[32];
    static s32 count = 0;
    s32 i;
    for (i = 0; i < count; i++) {
        if (seen[i] == key) {
            return;
        }
    }
    /* Only log (and only once, ever) if this key was actually recorded in
     * seen[] -- once the 32-slot dedup table fills up, a distinct 33rd+ key
     * can never be found by the loop above, so logging unconditionally here
     * would print it again on every single frame the row is rendered. */
    if (count < (s32)(sizeof(seen) / sizeof(seen[0]))) {
        seen[count++] = key;
        fprintf(stderr, "[settings-menu] hiding row (unregistered key): %s\n",
                key ? key : "(null)");
    }
}

/* Draw `str` at (x,y) in Zurich bold, white. Mirrors the credits idiom: a
 * combiner-arming pass (transparent PRIM box) then textRender. */
static Gfx *smText(Gfx *gdl, s32 x, s32 y, const char *str)
{
    s32 tx = x;
    s32 ty = y;
    s16 vx = viGetX();
    s16 vy = viGetY();
    if (str == NULL) {
        return gdl;
    }
    gdl = microcode_constructor_related_to_menus(gdl, x, y - 1, x + 1, y + 1, 0);
    gdl = textRender(gdl, &tx, &ty, (char *)str,
                     ptrFontZurichBoldChars, ptrFontZurichBold, -1, vx, vy, 0, 0);
    return gdl;
}

/* Format a setting's live value into `out` (mirrors settings.c's dump idiom). */
static void smFormatValue(const Setting *st, char *out, size_t out_size)
{
    switch (st->type) {
        case SETTING_TYPE_INT:
            snprintf(out, out_size, "%d", *(s32 *)st->ptr);
            break;
        case SETTING_TYPE_UINT:
            snprintf(out, out_size, "%u", *(u32 *)st->ptr);
            break;
        case SETTING_TYPE_FLOAT:
            snprintf(out, out_size, "%.2f", (double)*(f32 *)st->ptr);
            break;
        case SETTING_TYPE_ENUM:
            snprintf(out, out_size, "%s",
                     settingsEnumTokenForValue(st, *(s32 *)st->ptr));
            break;
        case SETTING_TYPE_STRING:
            snprintf(out, out_size, "%s", st->ptr ? (char *)st->ptr : "");
            break;
        default:
            snprintf(out, out_size, "?");
            break;
    }
}

/* Normalised [0,1] position of a ranged setting for the slider fill. */
static f32 smNormalized(const Setting *st)
{
    f32 lo;
    f32 hi;
    f32 v;
    if (st->type == SETTING_TYPE_FLOAT) {
        lo = st->min.f32_value;
        hi = st->max.f32_value;
        v  = *(f32 *)st->ptr;
    } else {
        lo = (f32)st->min.s32_value;
        hi = (f32)st->max.s32_value;
        v  = (f32)(*(s32 *)st->ptr);
    }
    if (hi <= lo) {
        return 0.0f;
    }
    v = (v - lo) / (hi - lo);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

Gfx *pcSettingsMenuRender(Gfx *gdl, const SmPage *page, s32 selectedRow)
{
    s32 vl;
    s32 vt;
    s32 vw;
    s32 vh;
    s32 px0;
    s32 py0;
    s32 px1;
    s32 py1;
    s32 lx;
    s32 wx;
    s32 rowY;
    s32 i;

    if (gdl == NULL || page == NULL) {
        return gdl;
    }

    /* Reset RDP microcode/cycle state before drawing, same as the credits
     * roll (bondview.c's portCreditsRender idiom): without this the widget
     * inherits whatever cycle-type/combiner state the last thing drawn this
     * frame left behind, instead of a known-good baseline. */
    gdl = microcode_constructor(gdl);

    vl = viGetViewLeft();
    vt = viGetViewTop();
    vw = viGetViewWidth();
    vh = viGetViewHeight();
    if (vw < 160) { vw = 160; }
    if (vh < 120) { vh = 120; }

    /* Panel: inset from the pane so it is safe in any split-screen viewport. */
    px0 = vl + vw / 8;
    py0 = vt + vh / 10;
    px1 = vl + vw - vw / 8;
    py1 = vt + vh - vh / 10;

    lx = px0 + SM_PAD;                       /* label column          */
    wx = px0 + (px1 - px0) * 52 / 100;       /* widget/value column   */

    /* Panel body + title band accent. */
    gdl = microcode_constructor_related_to_menus(gdl, px0, py0, px1, py1, SM_COL_PANEL);
    gdl = smText(gdl, px0 + SM_PAD, py0 + SM_PAD, page->title);
    gdl = microcode_constructor_related_to_menus(gdl, px0 + SM_PAD, py0 + SM_TITLEH,
                                                 px1 - SM_PAD, py0 + SM_TITLEH + 1, SM_COL_ACCENT);

    rowY = py0 + SM_TITLEH + 8;
    for (i = 0; i < page->count; i++) {
        const SmEntry *e = &page->entries[i];
        const Setting *st = NULL;
        const char *label;

        /* Missing-key HIDE rule (doc 05 §4.2): rows that carry a key whose
         * settingsFind() is NULL (incl. SM_FUTURE keys not yet registered) are
         * skipped entirely. HEADER/ACTION rows carry no key and always show. */
        if (e->key != NULL) {
            st = settingsFind(e->key);
            if (st == NULL) {
                smLogHiddenOnce(e->key);
                continue;
            }
        }

        if (rowY > py1 - SM_ROWH) {
            break;   /* clamp to the panel body */
        }

        if (i == selectedRow) {
            gdl = microcode_constructor_related_to_menus(gdl, px0 + 2, rowY - 2,
                                                         px1 - 2, rowY + SM_ROWH - 4, SM_COL_SEL);
        }

        label = e->note;
        if (label == NULL) {
            label = (st != NULL && st->label != NULL) ? st->label : e->key;
        }
        gdl = smText(gdl, lx, rowY, label);

        switch (e->widget) {
            case SMW_HEADER:
                /* Section label with an accent underline; no value column. */
                gdl = microcode_constructor_related_to_menus(gdl, lx, rowY + 12,
                                                             px1 - SM_PAD, rowY + 13, SM_COL_ACCENT);
                break;

            case SMW_ACTION:
                gdl = microcode_constructor_related_to_menus(gdl, wx, rowY - 1,
                                                             wx + 84, rowY + 12, SM_COL_ACTION);
                gdl = smText(gdl, wx + 6, rowY, "SELECT");
                break;

            case SMW_TOGGLE: {
                s32 on = (st != NULL && *(s32 *)st->ptr != 0);
                gdl = microcode_constructor_related_to_menus(gdl, wx, rowY - 1,
                                                             wx + 18, rowY + 12,
                                                             on ? SM_COL_TOG_ON : SM_COL_TOG_OFF);
                gdl = smText(gdl, wx + 28, rowY, on ? "ON" : "OFF");
                break;
            }

            case SMW_ENUM: {
                const char *tok = (st != NULL)
                                ? settingsEnumTokenForValue(st, *(s32 *)st->ptr)
                                : NULL;
                gdl = microcode_constructor_related_to_menus(gdl, wx, rowY - 1,
                                                             px1 - SM_PAD, rowY + 12, SM_COL_ENUM);
                gdl = smText(gdl, wx + 4, rowY, tok ? tok : "?");
                break;
            }

            case SMW_SLIDER: {
                s32 trackX0 = wx;
                s32 trackX1 = px1 - SM_PAD - 44;
                s32 trackW  = trackX1 - trackX0;
                char buf[32];
                if (trackW < 8) { trackW = 8; trackX1 = trackX0 + trackW; }
                gdl = microcode_constructor_related_to_menus(gdl, trackX0, rowY + 2,
                                                             trackX1, rowY + 9, SM_COL_TRACK);
                if (st != NULL) {
                    s32 fillX1 = trackX0 + (s32)((f32)trackW * smNormalized(st));
                    gdl = microcode_constructor_related_to_menus(gdl, trackX0, rowY + 2,
                                                                 fillX1, rowY + 9, SM_COL_FILL);
                    smFormatValue(st, buf, sizeof(buf));
                    gdl = smText(gdl, trackX1 + 6, rowY, buf);
                }
                break;
            }

            default:
                break;
        }

        rowY += SM_ROWH;
    }

    gdl = combiner_bayer_lod_perspective(gdl);
    return gdl;
}

Gfx *pcSettingsMenuRenderForced(Gfx *gdl)
{
    static s32 forced = -1;   /* latch-once: -1 unread, 0 off, 1 on */

    /* Debug force-open preview page. Deliberately a *local* (non-file-scope)
     * array so the curation-model guard (tools/check_settings_menu_model.py,
     * which scans for `static const SmEntry NAME[]` blocks and requires each to
     * be wired into g_smPages) never mistakes this debug-only page for an
     * orphaned curated page — it is NOT part of g_smPages. It exercises every
     * SmWidget branch of the renderer for the T2 acceptance capture: a header, a
     * live enum, a live slider, a live toggle, an action button, plus one
     * SM_FUTURE row (Audio.MusicVolume, unregistered until W6.E3) to prove the
     * missing-key HIDE path. The keyed rows are real registered settings so the
     * widgets show live values. */
    const SmEntry demoRows[] = {
        { NULL,                SMW_HEADER, 0.0f,  NULL, "Settings (forced preview)",      0 },
        { "Video.VSync",       SMW_ENUM,   0.0f,  NULL, NULL,                             0 },
        { "Video.FovY",        SMW_SLIDER, 1.0f,  NULL, NULL,                             0 },
        { "Video.Fxaa",        SMW_TOGGLE, 0.0f,  NULL, NULL,                             0 },
        { "Audio.MusicVolume", SMW_SLIDER, 0.05f, NULL, "Music volume (hidden until W6)", SM_FUTURE },
        { NULL,                SMW_ACTION, 0.0f,  NULL, "Reset to defaults",              0 },
    };
    const SmPage demoPage = {
        "REMASTER SETTINGS", demoRows, SM_ARRAY_COUNT(demoRows)
    };

    if (forced < 0) {
        const char *e = getenv("GE007_SETTINGS_MENU_FORCE");
        forced = (e != NULL && *e != '\0' && atoi(e) != 0) ? 1 : 0;
        if (forced) {
            fprintf(stderr, "[settings-menu] GE007_SETTINGS_MENU_FORCE=1: "
                            "force-rendering overlay preview page\n");
        }
    }
    if (!forced) {
        return gdl;   /* identity: emits NO display-list bytes when unset */
    }
    return pcSettingsMenuRender(gdl, &demoPage, 2);
}

#endif /* NATIVE_PORT */

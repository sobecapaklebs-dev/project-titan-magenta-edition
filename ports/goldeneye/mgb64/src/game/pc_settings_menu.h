#ifndef _GAME_PC_SETTINGS_MENU_H_
#define _GAME_PC_SETTINGS_MENU_H_

/*
 * W5.E2.T1 — In-game Remaster Settings menu: model + curation table.
 *
 * NATIVE_PORT-only. This header defines the *model* only (widget enum + row/page
 * structs + the SM_FUTURE sentinel). The static curation table lives in
 * pc_settings_menu.c. No renderer, no input controller and no front-end/overlay
 * mount are declared here — those are W5.E2.T2..T6. This module adds ZERO
 * behavior: nothing in the shipping code path calls into it yet, so the
 * overlay-closed frame is byte-identical to before it existed (doc 05 §8).
 *
 * The table curates ~55 of the registered settings keys (settings.c registry)
 * across 7 pages; window-geometry keys (Video.WindowX/Y, etc.) stay ini-only.
 * Widget is authored per row (nominally derived from Setting.type). A row whose
 * `key` fails settingsFind() is *hidden* at render time (renderer's job, T2), so
 * rows for keys a later workstream registers (the W6 audio bus) are pre-listed
 * and tagged SM_FUTURE — whitelisted by tools/check_settings_menu_model.py.
 */

#ifdef NATIVE_PORT

#include <ultra64.h>

/*
 * `future` field sentinel. A row tagged SM_FUTURE names a key that a later
 * workstream registers (the W6 audio bus). It stays hidden until settingsFind()
 * resolves the key, and is exempt from the "must be currently registered"
 * check in tools/check_settings_menu_model.py. Untagged rows use 0.
 */
#define SM_FUTURE 1

typedef enum SmWidget {
    SMW_TOGGLE,   /* int 0/1                                   */
    SMW_SLIDER,   /* float / ranged int                        */
    SMW_ENUM,     /* enum options table                        */
    SMW_ACTION,   /* button row (no key)                       */
    SMW_HEADER    /* section label (no key)                    */
} SmWidget;

typedef struct SmEntry {
    const char *key;        /* settingsFind() key; NULL for HEADER/ACTION      */
    SmWidget    widget;     /* derived from Setting.type unless overridden     */
    f32         step;       /* slider step (e.g. FovY 1.0, Vignette 0.05)      */
    void      (*apply)(void); /* optional live-apply hook, NULL = write-only   */
    const char *note;       /* short caption; NULL = use Setting.help          */
    u8          future;     /* SM_FUTURE: hidden until settingsFind() resolves */
} SmEntry;

typedef struct SmPage {
    const char    *title;   /* page tab label                                  */
    const SmEntry *entries; /* row array                                       */
    s32            count;   /* number of rows in `entries`                     */
} SmPage;

/*
 * Read accessor for the curated model. Returns the static page table; when
 * `outCount` is non-NULL it receives the page count. Consumed by the renderer /
 * mounts in W5.E2.T2+. Present so the model has a stable public seam; nothing on
 * the shipping code path calls it yet.
 */
const SmPage *pcSettingsMenuPages(s32 *outCount);

/*
 * W5.E2.T2 — pure-DL widget renderer. Draws one curated `page`'s panel + its
 * visible rows (title header + per-row label + a widget by SmWidget type) into
 * the HUD display list, reading each row's LIVE value from the settings registry
 * (settingsFind). Rows whose key is unregistered (settingsFind()==NULL, incl. the
 * SM_FUTURE W6 audio rows) are hidden per doc 05 §4.2. `selectedRow` highlights a
 * row (<0 = none) for the T3/T4 controllers. Panel/widgets are gDPFillRectangle
 * translucent quads and text is textRender (Zurich bold) — the exact in-mission
 * HUD idiom (bondview.c:20055-20062 credits / gun.c drawModernAdsReticle), so it
 * cannot corrupt texture state. Coordinates are viewport-relative (viGetView*),
 * so it is safe once per split-screen pane. Draw-only: it never reads or writes
 * simulation state. Returns the advanced Gfx*.
 */
Gfx *pcSettingsMenuRender(Gfx *gdl, const SmPage *page, s32 selectedRow);

/*
 * Debug force-open seam for the T2 acceptance capture. Latches the new
 * GE007_SETTINGS_MENU_FORCE env ONCE on first call; when it is set it renders a
 * self-contained preview page that exercises every SmWidget type (header, enum,
 * slider, toggle, action + a hidden SM_FUTURE row). When the env is UNSET this is
 * a zero-cost no-op that returns `gdl` untouched (emits NO display-list bytes), so
 * the default frame is byte-identical to before this task (doc 05 §8 R3 identity).
 * The in-mission toggle + front-end mounts are W5.E2.T3/T4, not this function.
 */
Gfx *pcSettingsMenuRenderForced(Gfx *gdl);

#endif /* NATIVE_PORT */
#endif /* _GAME_PC_SETTINGS_MENU_H_ */

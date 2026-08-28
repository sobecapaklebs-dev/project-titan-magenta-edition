// ui_settings.h — auto-generated settings panels from the engine config schema.
#ifndef MGB64_UI_SETTINGS_H
#define MGB64_UI_SETTINGS_H

#include "config_schema.h"  // MgbConfigSaveResult (AUDIT-0036 save-status surfacing)

// What the user did with the staging controls this frame (only meaningful in the
// in-game overlay, which opens a staging session; the launcher never stages and
// always gets SETTINGS_NONE).
enum SettingsResult {
    SETTINGS_NONE = 0,   // no staging action (or launcher path — not staging)
    SETTINGS_APPLIED,    // user hit Apply: staged values committed to live + saved
    SETTINGS_CANCELLED   // user hit Cancel: staged values discarded, close the panel
};

// Draw the settings tabs (Video / Input / Game / Audio / UI) inside the current
// content region. When a staging session is open (configStagingActive()), edits
// go to a working copy and the panel shows Apply/Cancel + a live Preview toggle;
// otherwise (launcher) edits apply instantly and the panel shows "Save Settings".
// Shared by the launcher and the in-game overlay.
SettingsResult Settings_draw();

// Record a config-save outcome (AUDIT-0036) so the settings panel surfaces it as
// the status line under the buttons on the next Settings_draw. Used by the
// overlay-close auto-apply path (ui_overlay.cpp), which commits a staged session
// outside Settings_draw and must not silently swallow a persistence failure. The
// status is cleared on the next staged edit / Cancel so it never reads stale.
void Settings_reportSaveResult(MgbConfigSaveResult r);

#endif  // MGB64_UI_SETTINGS_H

/* save_status.h — pure (SDL/ImGui/engine-free) mapping of a config-save outcome
 * (AUDIT-0036 MgbConfigSaveResult) to the user-facing settings status line plus
 * the "is this a hard failure" predicate that drives the Retry button and keeps
 * the in-game overlay open instead of silently swallowing a persistence failure.
 * Extracted so the OK / SUPPRESSED / FAILED wording AND the failure policy are
 * unit-testable without ImGui. Consumed by src/app/ui_settings.cpp (status line +
 * Retry) and src/app/ui_overlay.cpp (overlay-close: don't resume on FAILED).
 *
 * Depends only on the engine-free MgbConfigSaveResult enum (src/app/config_schema.h),
 * so the test TU pulls in no <SDL.h> / ImGui / engine headers. */
#ifndef MGB64_SAVE_STATUS_H
#define MGB64_SAVE_STATUS_H

#include "../app/config_schema.h"  /* MgbConfigSaveResult */

#ifdef __cplusplus
extern "C" {
#endif

/* Status line shown under Apply / Save Settings for a save outcome. Never NULL
 * (callers printf("%s", ...) it directly). OK names the file, SUPPRESSED explains
 * the faithful/remaster no-persist, FAILED is actionable. */
const char *saveStatusMessage(MgbConfigSaveResult r);

/* 1 iff the outcome is a hard persistence failure (MGB_CONFIG_SAVE_FAILED): it
 * gets a Retry and must not be silently swallowed on overlay close. OK and the
 * intentional SUPPRESSED no-write both return 0. */
int saveStatusIsFailure(MgbConfigSaveResult r);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_SAVE_STATUS_H */

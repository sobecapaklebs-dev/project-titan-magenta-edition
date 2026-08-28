// update_check.h — quiet "a newer MGB64 is available" check for the launcher.
//
// At launcher startup (interactive path only) a background thread shells out to
// the system `curl` to read the latest GitHub release tag, compares it against
// the compiled-in version, and — if strictly newer and not previously dismissed
// — makes a tag available for a small dismissible banner. No library deps, no
// telemetry: a single plain GET, curl absent/offline/timeout => silently nothing.
#ifndef MGB64_UPDATE_CHECK_H
#define MGB64_UPDATE_CHECK_H

#include <stddef.h>

// Start the check on a background SDL thread. Runs at most once per process. A
// no-op (and spawns no subprocess) when: the invocation is an automation/
// deterministic one (mgb_is_automation_invocation), GE007_UPDATE_CHECK=0, or the
// Game.CheckForUpdates setting is off. argc/argv are the process args (for the
// automation gate). Safe to call before the window's event loop begins.
void UpdateCheck_start(int argc, char **argv);

// If a strictly-newer, not-yet-dismissed release tag is ready, copy it into
// out[cap] and return 1; otherwise return 0. Cheap to poll every frame.
int UpdateCheck_bannerTag(char *out, size_t cap);

// 1 once the check has finished (or was declined without spawning anything).
// Used by the headless self-test harness (MGB64_UPDATE_CHECK_SELFTEST).
int UpdateCheck_isDone(void);

// 1 only when a real network response was actually fetched and version-compared
// (distinguishes "checked, up to date" from "check declined / offline"). Lets the
// About panel show an honest status instead of over-claiming "up to date".
int UpdateCheck_didCheck(void);

// Call just before app shutdown: silences the (detached) worker's log lines so
// a check still in flight can't fprintf into DiagLog's closing stderr pipe.
void UpdateCheck_quiesce(void);

// Persist `tag` as dismissed (app config) so its banner never returns.
void UpdateCheck_dismiss(const char *tag);

#endif  // MGB64_UPDATE_CHECK_H

// arg_triage.cpp — see arg_triage.h.
#include "arg_triage.h"

#include <cstring>

// Flags that force the unchanged headless engine path. These are the batch,
// deterministic, screenshot, config-dump, and trace flags the validation
// harness relies on, plus the explicit --no-ui escape hatch. Interactive
// "direct play" flags (--rom / --level / --difficulty / --faithful / ...) are
// deliberately NOT here: they open the launcher (which can preselect them).
static const char *const kAutomationFlags[] = {
    "--no-ui",
    "--deterministic",
    "--screenshot-frame",
    "--screenshot-exit",
    "--screenshot-game-timer",
    "--screenshot-label",
    "--ramrom",
    "--background",
    "--sim-state-hash-out",
    "--trace-state",
    "--freeze-input",
    "--print-sim-hash-regions",
    "--dump-config",
    "--list-settings",
    "--list-displays",
    "--reset-config",
    "--config-override",
    "--config-set",
    "--record-tape",   // FID-0034 input tape: always headless (forces --deterministic)
    "--play-tape",
};

extern "C" int mgb_is_automation_invocation(int argc, char **argv) {
    if (!argv) return 0;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        for (const char *flag : kAutomationFlags) {
            if (std::strcmp(argv[i], flag) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

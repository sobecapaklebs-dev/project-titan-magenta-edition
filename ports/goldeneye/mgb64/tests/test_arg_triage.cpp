// test_arg_triage.cpp — unit test for the launcher/automation triage.
#include "arg_triage.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int invoke(std::vector<const char *> args) {
    // argv[0] is the program name; the function scans argv[1..].
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>("ge007"));
    for (const char *a : args) argv.push_back(const_cast<char *>(a));
    return mgb_is_automation_invocation((int)argv.size(), argv.data());
}

int main() {
    int failures = 0;
    auto check = [&](const char *name, int got, int want) {
        if (got != want) {
            std::printf("FAIL: %s -> %d (want %d)\n", name, got, want);
            ++failures;
        }
    };

    // Launcher (no automation flag present).
    check("no args", invoke({}), 0);
    check("rom only", invoke({"--rom", "baserom.u.z64"}), 0);
    check("level only", invoke({"--level", "dam"}), 0);
    check("faithful", invoke({"--faithful"}), 0);
    check("remaster+rom", invoke({"--remaster", "--rom", "x.z64"}), 0);

    // Automation / diagnostic (bypass the launcher).
    check("deterministic", invoke({"--rom", "x.z64", "--deterministic"}), 1);
    check("screenshot-frame", invoke({"--screenshot-frame", "120"}), 1);
    check("screenshot-exit", invoke({"--screenshot-exit"}), 1);
    check("ramrom", invoke({"--ramrom", "demo.rmr"}), 1);
    check("background", invoke({"--background"}), 1);
    check("no-ui", invoke({"--no-ui", "--rom", "x.z64"}), 1);
    check("dump-config", invoke({"--dump-config"}), 1);
    check("list-settings", invoke({"--list-settings"}), 1);
    check("sim-state-hash-out", invoke({"--sim-state-hash-out", "h.txt"}), 1);
    check("config-set", invoke({"--config-set", "Video.VSync=on"}), 1);
    // FID-0034 input tape: record/playback always bypass the launcher (they
    // force --deterministic, so must reach the headless engine path).
    check("record-tape", invoke({"--rom", "x.z64", "--level", "dam",
                                 "--record-tape", "t.ge7tape"}), 1);
    check("play-tape", invoke({"--rom", "x.z64", "--level", "dam",
                               "--play-tape", "t.ge7tape"}), 1);
    // Realistic harness combo.
    check("harness combo",
          invoke({"--rom", "baserom.u.z64", "--level", "dam", "--deterministic",
                  "--screenshot-frame", "120", "--screenshot-exit"}),
          1);

    if (failures == 0) {
        std::printf("PASS: all arg_triage cases\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}

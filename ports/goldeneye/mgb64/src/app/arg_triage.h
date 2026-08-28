// arg_triage.h — decide whether a CLI invocation must bypass the launcher.
//
// The app shell owns main(). Automation/diagnostic invocations (used by the
// validation harness and scripts) must run the unchanged engine path so their
// behavior stays byte-identical. Everything else opens the launcher.
#ifndef MGB64_ARG_TRIAGE_H
#define MGB64_ARG_TRIAGE_H

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 if argv contains any automation/diagnostic flag (or --no-ui) that
// must bypass the launcher and delegate to mgb64_headless_main(); 0 otherwise.
int mgb_is_automation_invocation(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif  // MGB64_ARG_TRIAGE_H

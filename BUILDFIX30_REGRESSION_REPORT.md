# BuildFix30 Regression Report

BuildFix29 FINAL remains the immutable ancestry. BF30 changes Date/Time, Theme lifecycle, Disc platform, localization, boot/system-package release metadata, PUP/power finalization and API freeze tooling without replacing the validated boot chain, Unified Input, Neutrino/UL, Project NES, pikaVR or VlombisCore.

The complete command set of the official `kernel/tests/run_host_tests.sh` has now passed by exact command-boundary segmentation. Executor wall-clock limits required multiple invocations; no timeout was counted as PASS and each continuation started only at the next unexecuted command. The BF30 relative-path tail was run from the project root, matching the working-directory expectation of the original runner.

Coverage includes storage/VMC, package installer, applications/runtime, Unified Input and modern controllers, UL/Neutrino overlays and return, renderer 480i/720p, network/browser/FTP, audio, updater/PUP, Theme APIs v1-v4, TGR, pikaVR, VlombisCore, SAS, Project NES/FCEUmm, lifecycle/recovery, Date/Time/SNTP, localization, Disc Service/Engine, boot/TINS hardening, Store/Download/Repository and API freeze.

A 970-file production-source hash comparison between the regressed tree and the BF30 FINAL source candidate reports zero differences.

Result: `GLOBAL_HISTORICAL_HOST_REGRESSION_SEGMENTED=PASS`. Target, PCSX2 and physical hardware/media remain external and are not inferred.

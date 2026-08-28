# BuildFix27 Checkpoint CP10R — Material Recovery
STATUS: HOST_VALIDATED_RECOVERY
BASELINE: BuildFix26 FINAL (unchanged)
BASELINE_SHA256: 3f74d98b062eb1d6fd4f1e99b9392973515e214854505ffc7fb1867a9863e9f1
SOURCE_PARENT: persisted BuildFix27 CP00 source archive

Reason:
- CP10 checkpoint/checksums/reports were present in Library, but the full CP10 source archive referenced by those checksums was not.
- CP10R reconstructs the documented CP01-CP10 material behavior without claiming byte identity with the missing archive.

Recovered/validated boundaries:
- CP02 Titan App lifecycle/context, exit reasons, diagnostics and shutdown quiesce.
- CP04 VFS volume discovery via append-only TitanAppApiV1 tail.
- CP05 atomic publish + shared asynchronous text input + external ELF handoff.
- CP06 wLaunchELF ownership/lifecycle semantics retained; no direct FileXio/gsKit/ExecPS2 ownership.
- CP08 ProjectNesCoreBackend + Legacy NROM fallback boundary.
- CP09 neutral NES input + explicit frontend-owned RGB565 target.
- CP10 provider PCM render boundary, Titan-owned audio queue, backpressure cadence protection and provider-rate scheduler.
- Fixed FCEUmm references preserved; no FCEUmm source/provider materialization claimed.

Validation:
- Titan App runtime harness: PASS (-Wall/-Wextra/-Werror + ASan/UBSan).
- wLaunchELF host lifecycle: PASS.
- Project NES core/audio/lifecycle: PASS.
- BuildFix27 CP10 recovery contract: PASS.
- Official host runner segment 1: PASS through build25 flow/themes/power contract.
- Official host runner segment 2: PASS from Build35/36 onward through BuildFix26 readiness and CP10 recovery gates.
- Runner wrapper emitted a terminal-status anomaly after final PASS lines; no contract failure appears in either validation log.

NEXT: CP11 Storage/Saves + lifecycle.

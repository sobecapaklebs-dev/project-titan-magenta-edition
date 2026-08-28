# BuildFix30 CP12 — Global Historical Regression Closure

Parent: CP11 Final Source Packaging / Independence Gate plus CP10 freeze-consistency documentation.

Material unit: completes the official `kernel/tests/run_host_tests.sh` command set by exact command-boundary segmentation under the executor wall-clock limit. No timeout is counted as PASS; continuations begin only at the next unexecuted command. A working-directory mismatch in the BF30 relative-path tail was identified as a harness invocation issue and rerun from the project root expected by the original runner.

The production source validated by the segmented runner is byte-identical across 970 production files to the existing BF30 FINAL source candidate. The API-freeze documentation consistency gates are also PASS.

Target R5900/PS2SDK, PCSX2 and physical hardware/media remain external. `MAGENTA_V1_READY=0`; `SOURCE_RELEASE_CANDIDATE=1`; `READY_FOR_HARDWARE=1`.

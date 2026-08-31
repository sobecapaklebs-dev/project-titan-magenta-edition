# BuildFix27 Checkpoint CP47
STATUS: HOST_VALIDATED / TARGET_EXTERNAL_BLOCKED
PREVIOUS_VALID: CP46
BLOCK: deterministic R5900/VU build + PCSX2 evidence orchestrator

Implemented:
- consumes only a CP45-valid material PS2DEV/PS2SDK bundle;
- validates official PFS target prerequisites before target compilation;
- builds/verifies wLaunchELF and Project NES with mips64r5900el-ps2-elf-gcc;
- builds the kernel with the validated target environment;
- records target artifact SHA-256 and sizes into a JSON evidence manifest;
- optionally executes only an explicit caller-supplied PCSX2 command and captures exit/log;
- never substitutes host compilers or marks absent PCSX2/toolchain as PASS.

Validation:
- incomplete bundle fail-closed fixture: PASS;
- structurally complete synthetic bundle reaches a real target step and cannot fabricate PASS: PASS;
- BuildFix27 host-readiness and scope-closure contracts: PASS;
- current executor target probe: BLOCKED as expected (material PS2DEV bundle absent).

NEXT: external material intake (PS2DEV/FCEUmm) or physical-PS2 evidence validation.

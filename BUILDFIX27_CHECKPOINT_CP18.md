# BuildFix27 Checkpoint CP18
STATUS: HOST_VALIDATED
PREVIOUS_VALID: CP17
BLOCK: wLaunchELF bounded cooperative recursive copy/move

- iterative DFS, maximum depth 8; no recursive call-stack growth
- one entry/chunk cooperative progress per update
- nested files retain atomic temporary publication
- directory destination merge refused; self/descendant destination refused
- recursive move is two-phase: copy complete first, post-order source deletion second
- ASan/UBSan fixture proves 2 nested file commits precede all source deletion
- no FileXio/gsKit/ExecPS2 ownership introduced

Contracts:
- buildfix27-wlaunchelf-recursive-contract: PASS
- wlaunchelf-recursive-host ASan/UBSan: PASS
- CP17 text editor contract: PASS
- wLaunchELF host lifecycle: PASS
- CP10R/CP14/CP15 regressions: PASS

NEXT: CP19 image/JPG viewer capability audit + Titan-owned implementation where existing decode services permit.

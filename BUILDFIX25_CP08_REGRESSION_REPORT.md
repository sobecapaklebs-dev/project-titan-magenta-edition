# BuildFix25 CP08 Regression Report

STATUS: HOST VALIDATED

Passed:
- every BuildFix25 Python contract;
- VRAM fault-injection harness with -Wall -Wextra -Werror + ASan/UBSan;
- 720p transaction fault matrix with -Wall -Wextra -Werror + ASan/UBSan;
- BuildFix23 UI transition/focus/text/collision/package/layout contracts;
- BuildFix23 dynamic Theme scene graph, 128-declared/max-3-resident timeline, streaming/pikaVR and degradation contracts;
- BuildFix24 Theme stress validation and Theme API v2/v3 compatibility;
- BuildFix24 SAS normative model, archive validator, Library and badge contracts;
- TitanEYE port contract;
- Titan Game Runtime sanitizer harness;
- TGR renderer sanitizer harness covering texture lifetime, alpha, partial viewport/split-screen and ST/Q/GS packing;
- pikaVR sanitizer harness including sampler identity, lossless CLUT fallback and non-inflating savings;
- Ribbon reference, VU1 procedural flow and no-internal-edge contracts;
- source hardening;
- 480i/720p reference resolution/VRAM restore contract.

No product regression requiring a CP08 code patch was found.
Manual command mistakes (wrong root for SAS archive test and wrong source list for pikaVR) were rerun with the official harness invocation and are not product failures.

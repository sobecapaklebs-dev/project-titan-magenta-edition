# BuildFix30 Final Compatibility Matrix

| Area | Source/host state | External gate |
|---|---|---|
| BF29 platform baseline | preserved / targeted regression PASS | target/hardware regression |
| Unified Input / Early Input | preserved / PASS | physical controller matrix |
| Theme API v1-v4 + BF30 lifecycle | PASS | physical 480i/720p visual/perf |
| Date/Time/RTC/SNTP | PASS | PS2 RTC/live network |
| Disc Service/Engine | source complete / host PASS | original PS2 CD/DVD hardware |
| Disc Smart IGR | conservative negotiation PASS | per-title hardware profiles |
| USB BDM BF6/wLaunchELF-order | preserved | BF30 target boot regression |
| TINS v1 | preserved + fail-closed hardening PASS | target install/package |
| PUP/power | host PASS | physical update/power |
| Store/Download/Repository v1 | host PASS | live target network/storage |
| SDK/API freeze | PASS | target consumer build |
| pikaVR/VlombisCore/720p | targeted PASS | target performance evidence |

`HARDWARE_PASS=0`; untested combinations remain pending rather than inferred.

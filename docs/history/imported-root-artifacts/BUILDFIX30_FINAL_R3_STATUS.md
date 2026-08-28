# BuildFix30 FINAL R3 — FMCB Integrated Installer

R3 supersedes the earlier pre-hardware R2 package for installer behavior. The only supported autonomous provider remains Free McBoot, but its installation engine is now linked into and executed by the Project Titan installer itself. No second FMCB installer executable is part of the production media or UX.

Console gating is final for this source candidate: known compatible `ROM <= 2.20` consoles expose Autonomous + Semi-autonomous; incompatible or unknown consoles expose Semi-autonomous only.

Host/source-focused CP19 validation passed. PS2 target compilation and physical MagicGate/KELF/cold-boot validation remain external gates.

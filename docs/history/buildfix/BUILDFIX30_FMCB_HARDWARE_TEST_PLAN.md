# BuildFix30 — FMCB hardware validation plan

For each available representative console inside the supported Boot ROM range record model, region, exact Boot ROM version, Memory Card type/capacity and selected install strategy. Test a clean card, card with saves, existing FMCB/Titan-managed state where safe, insufficient-space refusal and a controlled interrupted transaction/recovery case.

For a positive autonomous run: back up the card; run autonomous install; confirm FMCB and Titan entry verification/commit; fully power off; cold boot; confirm FMCB -> Titan entry -> Titan bootloader -> kernel -> Health & Safety -> HOME; perform multiple additional cold boots and warm reboots; test shutdown; test recovery with Titan system missing/corrupt; and verify restore/uninstall behavior where supported.

For Boot ROM >2.20 or unknown ROM, perform only the negative compatibility test: confirm the installer refuses autonomous installation before mutation. Do not attempt FMCB writes merely to demonstrate incompatibility.

No `HARDWARE_PASS` may be recorded without physical evidence for the tested model/ROM/card combination.

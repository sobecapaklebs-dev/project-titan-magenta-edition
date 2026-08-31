# Titan Disc Hardware Test Plan — BuildFix30

Physical evidence is required before any HARDWARE_PASS claim. Validate cold boot without media; boot with an original PS2 disc; insert/eject/reinsert/swap at HOME; original PS2 DVD and CD; unsupported media; unreadable/error media where safe; metadata fallback/cache paths; launch and gameplay; controller input; PS/Home System Button; DS2 IGR fallback; Return-only; any title explicitly profiled MINIMAL/FULL; VMC only where enabled; Return to Titan; shutdown/reboot during no-media, probe, ready, and after return.

Record console model/ROM, optical drive state, disc Title ID/media type, BF30 source identity, video mode, controller/transport, serial/DEX logs, session/IGR/overlay level, and explicit PASS/FAIL for each row. Host simulation is never hardware evidence.

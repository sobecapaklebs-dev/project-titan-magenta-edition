# Titan Input Final Architecture — BuildFix29

High-level input has one route: physical device → transport/backend → `TitanUnifiedInput` device descriptor → normalized state → system/shell/application/overlay. Device identity is separate from Player 1–4 assignment. Stable reconnect hints are preserved when safe; first meaningful Welcome input can claim Player 1 deterministically.

`TITAN_NBTN_SYSTEM` is reserved for Titan while Session/Overlay routing is active. DS3 PS, DS4 PS, DualSense PS and Xbox Guide/Home normalize to the same system event. Native/Project NES consume the normalized event; UL and Neutrino receive the PADEMU out-of-band system command and retain the BF28 chord as fallback.

Battery state is honest: UNSUPPORTED, UNKNOWN, LEVEL, CHARGING or FULL. A percentage is shown only when `battery_valid` is true. PADEMU identity/battery snapshots use the slow path; system-button edges are push/event driven.

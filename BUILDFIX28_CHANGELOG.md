# BuildFix28 Final Changelog

- Added Titan Session Service and deterministic session lifecycle.
- Added adaptive Smart IGR and real Overlay Lite paths for Native/Application Runtime, Project NES, Titan UL and Titan-Neutrino.
- Added per-engine Resume and Return-to-Titan paths with fail-closed compatibility negotiation.
- Added Titan Unified Input with device identity separate from player assignment, hotplug/reconnect and normalized capabilities.
- Preserved DS2 baseline and PADEMU DS3/DS4 support; added DualSense USB/Bluetooth source paths.
- Added Xbox One/Series USB GIP and separate Bluetooth/BLE backend, including Early Input participation where the model/transport permits.
- Added keyboard and mouse through Unified Input.
- Made Early Input asynchronous and fail-open with a time-based 10-second startup deadline.
- Added platform overhead instrumentation, near-zero-cost fast paths and no-frame-allocation contracts.
- Completed pikaVR and VlombisCore pre-freeze audits and explicit capability taxonomy.
- Added Theme Shell API v4 / `scene-v4`: declarative multilayer composition, transforms, parallax, independent timelines, HOME/TOP synchronization, rare events, budgets, degradation, pikaVR-backed resource policy and DEX profiling.
- Added a deterministic 14-layer synthetic flagship workload proving multilayer composition without fullscreen-video/frame brute force.
- Closed lifecycle/recovery ordering and reconstructed-source persistence gaps in CP17.
- Completed the segmented official global host runner with ASan/UBSan retained where configured.
- Reorganized Library artifacts into `INTERMEDIATE/` and `FINAL/` without deleting historical material.

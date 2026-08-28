# BuildFix28 Hardware Test Plan

Run on real PS2 using the exact BF28 source/build identity.

1. Cold boot and warm reboot; verify Health & Safety and HOME.
2. Health & Safety input: DS2 baseline, then previously configured DS3/DS4/DualSense/Xbox Bluetooth device without connecting DS2 first.
3. HOME/TOP navigation, profiles, VMC, Library, folders, notifications and DEX.
4. Theme v1/v2/v3 regression; Theme v4 synthetic/flagship workload, rapid switching, long idle, 480i and 720p; record DEX Theme v4 telemetry and VRAM/EE RAM pressure/degradation.
5. Project NES/FCEUmm: launch user-provided ROM, input/audio/video/save RAM, Overlay -> Resume repeated, Overlay -> Return to Titan.
6. UL compatible title: Overlay -> Resume repeated, Overlay -> Return; verify VMC/storage integrity and no stuck input.
7. Neutrino compatible title: same Resume/Return sequence and storage checks.
8. Native app/game: Overlay/Return and lifecycle cleanup.
9. Controller hotplug/reconnect: DS2, DS3, DS4, DualSense USB/BT, Xbox One USB, Xbox One BT-capable, Xbox Series USB/BT, keyboard, mouse.
10. Record VID/PID/transport/player/capabilities/battery/packet errors/latency from Input DEX where available.
11. Wi-Fi/Ethernet + Browser while modern input/theme services are enabled.
12. Shutdown/reboot during idle after theme/controller/network activity.
13. Recovery/fault scenarios: invalid v4 theme -> safe fallback; VRAM critical degradation; modern input backend failure must not block boot.

For Overlay stress, repeat GAME -> OVERLAY -> RESUME at least 20 times and compare EE RAM/IOP RAM/VRAM/handles/thread/session counters for monotonic leaks. Repeat Return to Titan and relaunch cycles.

Evidence to return: build SHA, console model, video mode, title id/engine/profile, controller model/transport, serial/debug logs, DEX exports, screenshots where useful, and explicit PASS/FAIL observations per row.

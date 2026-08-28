# Project Titan RT1 BuildFix7 — Flight Recorder + Performance Profiler

Base: RT1 BuildFix6 Wi-Fi Single-Owner Handoff.
Bootloader: unchanged.

## Runtime diagnostics
- 2048-event fixed-size EE RAM ring buffer; no printf/file I/O on the realtime path.
- Monotonic timestamps and subsystem/event/result/a/b/c/d payloads.
- Network-state and Asset-Service transition events.
- Main-loop and storage-worker heartbeats.
- Automatic frame-spike records >=25 ms; >=250 ms escalates to ERROR.
- Storage-worker stall watchdog when foreground storage is pending.
- Persistent binary snapshot: `mass:/TITAN/logs/titan-current.bin`.
- Manual readable export: `mass:/TITAN/logs/titan-last.txt`.

## Performance profiler
- exact monotonic microsecond frame timing;
- moving 120-frame average, p95 and worst-frame;
- FPS x100 and spike counter;
- per-frame phase timings: input, audio, services, UI update, render;
- EE heap usage sampled through the existing DEX memory telemetry;
- network state, asset activity hint, foreground-storage state;
- main/storage heartbeat counters.

## UI
DEX Performance Overlay now also displays FPS, current frame time, p95, worst and spike count.
Debug Settings adds `Export Full Diagnostics`.

## Storage safety
Diagnostics persistence is serviced by the serialized Titan Storage Scheduler below foreground/VMC/audio/app work and before bulk artwork. It never reserves exclusive storage ownership.

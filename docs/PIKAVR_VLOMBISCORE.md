# pikaVR / VlombisCore — BuildFix22 engineering status

This document describes implemented behavior, not marketing promises.

## pikaVR

pikaVR is an internal graphics-resource optimization layer between TGR and the
low-level TitanVideo/GS path. It is not a replacement for TGR and it is not a
new GPU.

BuildFix22 implements and host-validates:

- texture fingerprints and content deduplication;
- sampler-state caching that distinguishes POINT/LINEAR and wrap semantics;
- GS VRAM telemetry (`vram_total`, `vram_used`, `vram_peak`, `vram_saved`);
- texture-budget telemetry separated from total GS VRAM;
- resident/retired/reclaimed lifetime accounting;
- exact RGBA32 -> PSM8+CLUT palettization at creation time with safe PSM32
  fallback when more than 256 distinct GS colors are required;
- draw/primitives/material/PATH3/Ribbon PATH1 counters derived from real work;
- categorized diagnostics outside the render hot path.

`vram_saved` is a current-state value: refreshing or re-registering a resource
cannot accumulate the same saving repeatedly.

The public TGR ABI remains the developer-facing rendering contract. pikaVR is
currently an internal/experimental implementation detail; no public pikaVR ABI
is frozen by BuildFix22.

## VlombisCore

VlombisCore is the umbrella name for measurable system-performance
coordination. A capability is not active merely because the name exists.

The first BuildFix22 producer is the Titan storage scheduler. It records real
coordination events such as exclusive FileXio handoffs, accepted foreground
jobs and urgent-audio background preemption. Runtime telemetry exposes active
capability bits plus saturating event counters. Diagnostics are sampled rather
than emitted for every arbitration event.

No percentage performance claims are defined by this interface.

## Badge truthfulness

Software classification and technology participation are separate concepts.
Technology badges are eligible only when the runtime evaluator sees measured
participation in the current execution context. A capability declaration with
zero measured events is insufficient.

Because the existing pre-launch Neutrino/UL splash occurs before a target has
performed Titan-native pikaVR/VlombisCore work, BuildFix22 does not hard-code
technology badges into that splash by title. Future presentation may consume
runtime evidence where a session-scoped proof exists.

## Validation levels

The implementation described here is HOST VALIDATED. A document or badge must
not claim CROSS-BUILD, PCSX2 or PS2 HARDWARE validation until those tests have
actually been executed.

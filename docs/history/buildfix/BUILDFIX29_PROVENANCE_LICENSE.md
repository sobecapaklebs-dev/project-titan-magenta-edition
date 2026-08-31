# BuildFix29 Provenance and License Report

BuildFix29 derives from the immutable BuildFix28 FINAL source freeze. Existing third-party source, licenses, upstream notices and provenance records remain authoritative and are not rewritten by this closure.

## FCEUmm

- upstream: `libretro/libretro-fceumm`
- frozen commit: `b5e3566515c27dc66c9c20572171673126532e06`
- frozen Git tree: `6adfb382bf8a369f1a6ee224e89c08d981e22cec`
- license: GPL-2.0
- imported source: `third_party/fceumm/modern/`

BuildFix29 does not replace or downgrade the frozen Project NES provider.

## Input/backend provenance

BuildFix29 extends Titan-owned integration around the already-vendored PADEMU/modern-controller architecture. Protocol-specific paths remain behind Titan backends/transports and converge at Unified Input; legacy notices remain in their original source locations.

## BuildFix29-native platform services

Repository Protocol v1, Titan Download Service, Store Foundation, controller UX/battery/DEX integration and associated tests/docs are Project Titan source additions. No new proprietary ROM, disc image, private key or external binary payload is introduced by these units.

Retain every `LICENSE`, `COPYING`, `Copying`, `UPSTREAM.md`, third-party notice and package-specific license file present in the final archive.

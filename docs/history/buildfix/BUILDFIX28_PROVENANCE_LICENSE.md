# BuildFix28 Provenance and License Report

BuildFix28 is derived from the immutable BuildFix27 final source freeze. Existing third-party source, license and upstream provenance records remain in their original locations inside the delivered tree and are not rewritten by this closure.

## FCEUmm

- upstream: `libretro/libretro-fceumm`
- frozen commit: `b5e3566515c27dc66c9c20572171673126532e06`
- frozen Git tree: `6adfb382bf8a369f1a6ee224e89c08d981e22cec`
- license: GPL-2.0
- imported source: `third_party/fceumm/modern/`
- frozen/import verification material: `external/fceumm/` and Project NES provenance resources.

BuildFix28 did not change the selected NES provider or silently replace the upstream core.

## Controller/backend provenance

BF28 extends the already-vendored Titan/PADEMU transport architecture for modern controllers. Protocol-specific implementation is kept within Titan device backends/transports and converges at Unified Input. Existing upstream/provenance records in vendored components remain authoritative for those components.

## Project source licenses

Retain every `LICENSE`, `COPYING`, `Copying`, `UPSTREAM.md`, third-party notice and package-specific license file contained in the source archive. The final delivery does not remove those notices.

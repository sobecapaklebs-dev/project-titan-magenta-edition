# Project Titan Magenta Edition 0.3.90-rc14 — Storage Fairness / Package Cancel BuildFix5

Hardware symptom addressed:

- Package install progress stopped at 11% while the HDD LED remained active.
- Pressing Circle to cancel could freeze the complete shell.
- Library scanning became substantially slower after BuildFix4.

## Root cause

BuildFix4 correctly moved storage I/O away from the realtime main thread, but the
storage worker gave the music prefetch branch unconditional first priority and
`continue`d whenever a music read made progress. The music ring target was 192 KiB,
so package extraction and library scanning could be starved while the ring was being
filled/refilled.

The package budget was exactly 8 KiB. PROJECTNES.pkg is 72,474 bytes; 8,192 bytes is
11.30% of that package, matching the observed 11% stop after one extraction turn.

Cancellation was also wrong for the new threaded architecture: Circle called
`TitanPackageInstaller_Cancel()` on the UI thread, and Cancel synchronously closed
file descriptors and recursively removed the staging directory. If the storage
worker was inside fileXio at that moment, the UI thread could block on the same
serialized RPC path, freezing render/input.

## Changes

- Package cancellation is request-only on the UI thread (`cancel_requested`).
- Descriptor close and staging cleanup happen only from the storage worker.
- Package preparation/header parsing/staging setup are queued and performed by the
  storage worker; clicking Install no longer performs package file I/O on the UI thread.
- Package extraction quantum reduced from 8 KiB to 4 KiB.
- Added a 64 KiB realtime audio low-water threshold.
- Audio may preempt bulk work only below low-water; after one audio refill the worker
  still grants one bounded package/scan step.
- Idle audio prefill still fills toward the existing 192 KiB high-water target when
  there is no package/scan work waiting.
- Library-scan throttle reduced from 12 ms to 1.5 ms; worker priority remains low and
  scan remains one scanner step per turn.
- Source-hardening regression checks now forbid synchronous package-cancel I/O and
  lock the fair-scheduler constants/API.
- Existing visual assets and UI geometry are unchanged.

## Validation

- Package installer transaction test updated for asynchronous cancellation and passes.
- Source-hardening contract passes.
- Boot path, ELF validation, installer transaction, curated asset verifier pass.
- Curated UI assets: 241 files, byte-for-byte identical to BuildFix4.
- Aggregate curated-asset manifest SHA-256 unchanged:
  aff882e2a240fa515a7df56a92d3de344d5826021e8f18461ad68c77bb778cfc

Hardware validation remains required for USB/fileXio scheduling behavior.

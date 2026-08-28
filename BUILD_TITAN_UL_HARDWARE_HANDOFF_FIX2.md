# Titan UL hardware handoff hotfix 2

This hotfix is based on the exact 0.3.90-rc14 source package used for the
physical-PS2 test where Home completed the handoff but USB/HDD activity stopped
and the selected `ul.cfg` title never booted.

## Corrected launch path

- The shell passes the already-scanned USBExtreme name, part count, media type
  and `ul.cfg` source path to Titan UL Engine. The engine no longer re-reads
  `ul.cfg` after resetting the IOP.
- The Project Titan headless path uses a bounded 10-second IOP reset/sync and
  cooperative `DelayThread` waits instead of unbounded reset spins.
- USB BDM modules are loaded once, after the I/O worker and BDM semaphore are
  initialized. The headless path does not start iLink, MX4SIO or HDD backends.
- After USB re-enumeration the engine waits up to 15 seconds and probes the
  actual `ul.<crc>.<startup>.00` split file on `mass0:/` through `mass4:/`.
- VMC/UL arguments are parsed after `setDefaults()`, which otherwise clears the
  Titan VMC override state.
- A fragmented or physically unmappable VMC no longer aborts a headless UL
  launch. That launch continues without VMC, matching OPL's interactive
  "continue" semantics instead of producing a black screen.
- Headless BDM failure paths no longer call GUI message boxes before the GUI is
  initialized, and an invalid `sbPrepare` patch index is rejected explicitly.
- If the headless launcher still fails or unexpectedly returns, it shows an
  error code and returns to `rom0:OSDSYS` instead of requiring a hardware reset.

## Regression protection

Host tests require the metadata handoff, cooperative USB wait, bounded IOP
reset, VMC non-fatal fallback and existing `massN:/` root contract.

## Assets

No file under `kernel/assets/ui` is modified by this hotfix.

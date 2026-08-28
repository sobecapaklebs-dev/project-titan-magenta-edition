# Project Titan RT1 BuildFix17 — Persistent Network Credentials

BF17 fixes persistence of Network Settings on the Titan storage root.

## Hardware-facing bug fixed
- BF16 wrote `network.cfg` with `TitanStorageIo_AtomicReplace(..., NULL)`.
- Titan Storage intentionally rejects backup-less replacement when the target already exists.
- Selecting an SSID created the first file with `pmk_valid=0`; the immediately following password/PMK save then failed with `EEXIST`.
- Result: the current boot connected successfully from RAM, but the next boot required the Wi-Fi password again.

## BF17 behavior
- `mass:/TITAN/settings/network.cfg` is published transactionally through `network.tmp` with `network.bak` as the previous known-good file.
- The Titan root and settings directory are explicitly ensured before writing.
- The temporary file is flushed before publication.
- SSID, Connect-to-Internet state, Wi-Fi/Ethernet preference and the derived WPA2 PMK are persisted.
- The plaintext Wi-Fi password is never written to storage.
- Settings format advances to v3 while v2 remains readable for migration.
- A valid backup is loaded if the primary is missing/corrupt, then the primary is self-healed.
- Boot load retries up to four times before network bootstrap to tolerate a late USB/fileXio mount.
- A stored Wi-Fi SSID+PMK automatically reconnects through the existing AR9271/TT1426 path.
- Network diagnostics events `0x230` (save) and `0x231` (load) expose persistence success/failure.

## Preservation
- No Asset Service/TCP lifecycle behavior from BF16 is changed.
- No AR9271 IOP/firmware behavior is changed.
- Frozen bootloader remains unchanged.

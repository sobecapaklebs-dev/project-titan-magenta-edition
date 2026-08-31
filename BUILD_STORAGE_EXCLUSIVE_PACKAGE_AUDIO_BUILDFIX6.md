# Project Titan Magenta Edition 0.3.90-rc14 — Package Exclusive I/O / Audio Cushion BuildFix6

Hardware symptoms addressed:

- `.pkg` installation advances to about 5% and then stops while the shell itself remains responsive.
- Magenta Melody stops during the stalled package installation.
- Circle requests cancellation but the installation appears not to react while the storage worker is blocked.
- Library scanning is slower than desired and can drain the menu-music stream even though rendering remains smooth.

## Diagnosis

BuildFix5 correctly removed storage work from the realtime UI thread, which explains why the interface now remains responsive. However, package extraction, music prefetch, catalog scanning, VMC work and asset work still share one serialized storage lane.

The package quantum in BuildFix5 is 4 KiB. For the previously reported 72,474-byte PROJECTNES package, a single 4 KiB payload transfer corresponds to about 5.65%, matching the new hardware stop point closely enough to identify the boundary that must be made preemptible.

The package extractor also called `fsync()`/`TitanStorageIo_Flush()` at every staged-file boundary and used `ReadExact()` followed by `WriteExact()` in one scheduler turn. On PS2 these calls ultimately traverse the synchronous storage/RPC path, so fairness can only be enforced between calls; a worker blocked inside one operation cannot service music or observe a cancellation flag until that operation returns.

The music ring was only 256 KiB of decoded 32 kHz stereo 16-bit PCM, about two seconds. Its 64 KiB low-water mark represented only about half a second of protection, which is too small for occasional USB/FAT/ISO metadata latency spikes.

## BuildFix6 changes

### Package Installer: real storage-exclusive mode

- Starting a package installation now reserves the storage lane exclusively.
- A library scan already in progress no longer prevents installation; it pauses at the next storage-worker boundary.
- While the installer session is exclusive, scanner, VMC refresh, app-asset build, deferred audio I/O and unrelated foreground storage work stay parked.
- Exclusivity remains active after success/cancel while the result/list screen is still open.
- Background storage resumes only when the user actually leaves Package Installer.
- A successful install defers the forced Home/library refresh until Package Installer is exited.

### Package extraction: one payload syscall per quantum

- Removed per-file `TitanStorageIo_Flush()`/`fsync()` from staged payload files.
- Removed the manifest-stage `fsync()` as well; publication still uses the private staging directory followed by rename.
- Replaced payload `ReadExact() + WriteExact()` pairs with a persistent transfer state machine.
- Each package update now performs at most one payload `ReadSome()` **or** one payload `WriteSome()` and returns to the scheduler.
- Partial reads/writes are preserved across scheduler turns.
- File open/seek gets its own scheduler turn before payload transfer begins.
- Circle remains request-only on the UI thread and now immediately changes the status text to `Cancelamento solicitado...`.
- The progress screen exposes the current storage boundary (`Abrindo`, `Lendo`, `Gravando`, `Concluindo`) without writing debug logs to disk, so any remaining hardware stall identifies the blocked operation directly.
- Cleanup and descriptor ownership remain serialized on the storage worker.

### Music behavior during installation

- Magenta Melody fades toward zero when an installation starts.
- Once the fade reaches zero, buffered PCM is no longer consumed silently, so the song position is effectively paused instead of skipping several seconds during a long install.
- Music resumes only when the user exits Package Installer.
- Package installation never performs music prefetch while it owns the storage lane.

### Music robustness during normal library scan

- Decoded PCM ring increased from 256 KiB to 1 MiB (a little over eight seconds at 32 kHz stereo 16-bit).
- Realtime refill low-water increased to 768 KiB (about six seconds of cushion).
- High-water increased to 896 KiB (about seven seconds).
- Library-scan scheduler delay reduced from 1.5 ms to 0.25 ms per scan step.
- Normal scanning still cooperates with realtime audio; it is **not** given package-style exclusive ownership.

## Validation performed

- Complete `kernel/tests/run_host_tests.sh`: PASS.
- Package Installer transactional test: PASS.
- Package test additionally forces storage transfers down to 3 bytes per `read()`/`write()` to validate persistent partial-transfer state: PASS.
- TPKG builder/verifier: PASS.
- Fast boot catalog + background scanner: PASS.
- Audio runtime and Magenta audio contracts: PASS.
- Source hardening contract: PASS.
- Release contract: PASS.
- Curated UI assets: unchanged against BuildFix5.
- Runtime audio assets: unchanged against BuildFix5.

## Hardware validation required

The host tests cannot reproduce a PS2 USB/IOP driver call that blocks internally. BuildFix6 removes the package flush path that can cause a long synchronous stall, minimizes payload work to one storage syscall per scheduler turn, and prevents competing storage clients during installation, but real-hardware validation remains the deciding test.

This environment does not contain the PS2DEV/PS2SDK MIPS toolchain, so no new `kernel.elf` was produced here. Compile the source with the normal Project Titan PS2 toolchain before hardware testing.

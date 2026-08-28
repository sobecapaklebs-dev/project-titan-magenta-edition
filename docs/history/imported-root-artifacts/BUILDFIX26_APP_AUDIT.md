# Project Titan BuildFix26 — Native Application Audit

Status: HOST-AUDITED / target execution not available in this environment.

This audit classifies runtime integration independently from SAS/package classification.
`SAS compliant/compatible` and `TITAN_NATIVE/HYBRID/EXTERNAL_ELF/COMPAT_WRAPPED` are orthogonal properties.

## Inventory

The source tree contains three explicit application projects under `apps/` plus the TitanEYE native game port under `ports/goldeneye`.
Generic ELF/SAS discovery also remains supported by the shell and is not reclassified as native simply because Titan can catalog or launch it.

### Internet / Titan Browser — A: TITAN_NATIVE (kernel system service)

- Entry point: reserved `@system-browser`; no ELF or `.tmod` payload.
- Runtime ownership: kernel-resident `TitanBrowser` + `TitanBrowserNet`, rendered by the existing shell page.
- Graphics: Titan shell owns GS/framebuffer/fonts/UI.
- Input: Titan shell page/input path.
- Audio: no independent app-owned audio runtime in current scope.
- Storage: downloads use `TitanStorageIo_*` and atomic replace.
- Network: HTTP transport currently calls PS2IP `gethostbyname/socket/connect/send/recv` inside `titan_browser_net.c`.
- Exit/return: page navigation inside the running shell; no ExecPS2.
- Metadata: TPKG `type=system-browser`, Title ID `TITN00010`.
- SAS relationship: independent; system-browser runtime identity does not imply SAS compliance.
- Current blocker for BuildFix27: Titan has network configuration/status services, but `TitanAppApiV1` exposes no general DNS/socket/HTTP client service. Browser transport is native and kernel-owned, but is not yet consuming a first-class reusable Titan Network Client API.
- Required direction: factor the existing working transport behind Titan Network Core/client services and make Browser consume that service. Do NOT create a browser-private network stack.

Classification confidence: HIGH.

### wLaunchELF Titan port — A: TITAN_NATIVE (partial feature parity)

- Entry point: `.tmod` ABI v1 (`titan_app_init/update/draw/shutdown`).
- Process/lifecycle: in-process Titan App; not an externally executed wLaunchELF ELF.
- Graphics/input: Titan App draw/input APIs; Titan owns renderer and pad lifecycle.
- Storage: Titan App API already exposes list/stat/read/write/mkdir/remove/rename for absolute allowlisted PS2 paths.
- Implemented devices: `mass:/`, `mc0:/`, `mc1:/`, `cdfs:/`, `hdd0:`.
- Implemented operations: browsing, metadata, cooperative 16 KiB single-file copy, empty delete, validated ELF handoff.
- ELF launch: `launch_elf()` transfers through Titan stage2; successful handoff intentionally leaves the in-process app runtime.
- Memory: copy buffer uses Titan `alloc/release` and is released during shutdown.
- Missing upstream parity: recursive copy, rename/new-folder UI despite API availability, HDD/PFS write management, text editor, JPG viewer, FTP, VMC management, richer configuration/mount semantics.
- Upstream reference: `ps2homebrew/wLaunchELF`; source contains mature `filer.c`, `elf.c`, `hdd.c`, `editor.c`, FTP/VMC components. Upstream is open source; the tree's `UPSTREAM_NOTICE.txt` records AFL-2.0 attribution requirements.
- BuildFix27 blocker: feature adapters and a few service boundaries, not process architecture. Mature upstream algorithms should be adapted behind Titan Storage/VFS/Input/UI lifecycle rather than recreated.

Classification confidence: HIGH. “Native” describes runtime ownership, not feature completeness.

### Project NES 0.1.5 Preview — A: TITAN_NATIVE (emulation core incomplete)

- Entry point: `.tmod` ABI v1 (`titan_app_init/update/draw/shutdown`).
- Graphics: app produces an EE-RAM RGB565 media surface; Titan owns GS presentation.
- Audio: PCM S16LE/32 kHz through Titan media queue; no direct SPU2/AUDSRV ownership.
- Input: Titan App input; Select+Circle returns to shell.
- Storage: current preview uses app sandbox `file_read/file_exists`; one fixed external ROM slot remains because the current frontend does not use a real ROM-library service.
- Memory/lifecycle: ROM buffer via Titan alloc/release; frame/audio state has host lifecycle tests.
- Current emulation coverage: iNES Mapper 0/NROM, official 6502 opcodes, frame-oriented non-cycle-accurate PPU, approximate pulse1/pulse2/triangle APU, 1P controller.
- Known missing core features: broad mapper coverage, illegal opcodes relied on by some software, precise PPU timing/raster effects, noise/DMC/full envelope/sweep, expansion audio, SRAM/save states, FDS/VS/Dendy and robust region behavior.
- BuildFix27 blocker: the frontend/runtime integration is useful and should be retained; the emulation core is the weak component and should be replaced rather than incrementally recreated.

Classification confidence: HIGH.

### TitanEYE / GoldenEye — A: TITAN_NATIVE GAME

- Entry point: Titan game module/bridge under `ports/goldeneye` via Titan Game Runtime/TGR.
- Graphics: TGR/TitanVideo/pikaVR game-mode ownership; preserves audited GS state, alpha, viewport/split-screen, ST/Q and PATH semantics.
- Input/audio/storage: borrowed foreground Titan game services; storage exclusive mode during handoff.
- Return: game-end path restores shell framebuffer, pikaVR/texture pool, VU1 video resources, storage ownership and shell music.
- SAS relationship: independent from game runtime integration.

Classification confidence: HIGH from existing runtime/port contracts; hardware lifecycle remains NOT EXECUTED.

### Generic discovered ELF / SAS-compatible package — C: EXTERNAL_ELF unless explicitly native

- Discovery/classification does not confer native runtime ownership.
- BuildFix24 SAS model remains authoritative: compliant => compatible; compatible does not imply compliant; INSTALL.PBT is not a SAS requirement.
- `TitanApplications_GetExecutionModel()` keeps external handoff distinct from native/system-service models.

## Cross-app API gaps for BuildFix27

1. **Network Client API** — reusable DNS/TCP/HTTP-level service above Titan Network Core so Browser and future apps do not bind PS2IP directly.
2. **Storage capability expansion** — wLaunchELF already has basic mkdir/rename APIs, but PFS/HDD mount/write semantics, recursive operations, VMC and explicit capability/error reporting need service-level contracts.
3. **Directory/ROM library API** — Project NES frontend needs paged enumeration/filtering or a read-only content-library service instead of a fixed `game.nes` slot.
4. **Persistent app save API** — Project NES needs bounded atomic SRAM/save-state storage rooted in app data.
5. **Media timing contract** — a mature emulator core needs explicit frame/audio pacing, queue latency and region timing semantics without direct GS/SPU2 ownership.

No BuildFix26 change should implement these APIs speculatively before target build and hardware validation.

# Project Titan Magenta Edition — project state

## RC16 TitanEYE + Ribbon BuildFix22 recovery state

- Canonical parent is `Project-Titan-0.3.92-rc16-TitanEYE-Ribbon-BuildFix21-source.tar.gz`; that baseline remains read-only.
- TitanEYE/TGR and Ribbon/VU1 are inherited baseline components, not BuildFix22 features, and their host contracts remain green.
- BuildFix22 adds pikaVR resource/VRAM management, exact creation-time PSM8/CLUT selection with fallback, truthful runtime technology capability evaluation, measurable VlombisCore scheduler evidence, SAS execution-model accuracy, diagnostics categories, and UI/theme polish.
- `TITAN_SYSTEM_UPDATE_CURRENT_BUILD` remains the existing forward-only updater generation; it is intentionally not renumbered to 22.
- The historical internal kernel/UI version strings inherited from the canonical BuildFix21 source are not rewritten by this recovery pass. BuildFix22 identity is carried by the source package, release documentation and recovery audit.
- Validation status is HOST VALIDATED only. Cross-build, PCSX2 and PS2 hardware validation require external environments.

## Identity

The single official development tree is:

```text
~/Projects/Project Titan Magenta Edition/
```

The project and final product name are both **Project Titan Magenta Edition**.
Its integrated engines are **Titan Neutrino Engine** and **Titan UL Engine**.

## Confirmed RC1 hardware baseline

The following deployed files were identified on the PS2 USB drive before RC2
source hardening:

- kernel: `7dc14dfbe25f2e56ec3fa00aaf8d17a0655d196396c128f5011ce6de4cb63da2`
- Titan UL Engine: `ec7e3a274bd20edb0bcf694c2b32f826c89c6cdbbb72702c4396342cb227d9eb`
- Titan Neutrino Engine: `3f76675330490ef2137814266d5d8ab8c3938e0bf2e665e2c3237b59d8a1ad50`

The binary copies are deliberately not tracked in the active source tree. The
user's preservation archive and Git history retain the historical baseline.

## RC15 BuildFix18 state

- Kernel `0.3.91-rc15`, Titan UI `0.9.35` and boot metadata
  `0.1.18-rc15-usb-bdm-bf6` identify this checkpoint.
- Ribbon v2 replaces independent row oscillation with one transported sheet
  frame; Scarlet, Wine, Cyan and Twilight palettes are preserved.
- TGR v2, the bounded GS texture arena, the MGB64 texture adapter and the
  streaming GoldenEye ROM cache are host-validated.
- The pinned MGB64 tree contains no ROM. TitanEYE is not yet an executable
  package: the precise remaining gates are recorded in
  `ports/goldeneye/PORT_STATUS.md`.
- The complete host suite passes. PS2DEV compilation, PCSX2 and physical-PS2
  validation remain pending because those environments are unavailable here.

## RC14 state

- Hardware photos from RC13 exposed Home tiles being moved behind the footer
  while the system menu was open and long footer hints exceeding the viewport.
- The incremental library scanner treated the shared `ul.cfg` path as the game
  identity, so only the first UL record could survive deduplication.
- RC14 keys UL records by startup ID, scans the `mass0:` alias after `mass:`,
  and keeps folder membership compatible with legacy single-key data.
- APP_HOME is the only deliberately replaced visual asset; its supplied glyph
  is centered on a transparent 128x128 A8 mask.
- Host validation is required before packaging. MIPS compilation, PCSX2 and
  physical-PS2 validation remain pending in this environment.

## RC9 state

- RC8 hardware feedback confirmed native 720p page flipping but exposed a
  down/right CRTC shift, unavailable USB profile/catalog state and failed DEX
  persistence.
- RC9 preserves the native 720p framebuffer/VRAM implementation and changes
  only the physical CRTC window coordinates.
- USB module startup no longer short-circuits on a resident-module result, and
  the persistent storage boundary accepts both first-device aliases (`mass:`
  and `mass0:`).
- Generic ELF applications stay in Applications; only installed
  `TITAN/apps` entries receive direct Home tiles, preserving game visibility.
- Host regression suites pass. MIPS compilation and physical RC9 confirmation
  remain pending.

## RC7 state (historical)

- Native 720p single-buffer tearing: RC3 source correction preserved with two
  page-aligned buffers and host VRAM/presentation contracts.
- Extracted-source build failure: corrected by a Git-optional disposable source
  snapshot and deterministic archive-mode component metadata.
- Titan Neutrino Engine EE loader compile failure: corrected by including the
  PS2SDK header that declares `DelayThread()` in the FHI retry source.
- Titan UL Engine parallel language generation failure: corrected by making
  every translation YAML target directly order-dependent on `download_lng`.
- RC6 ordering attempt: superseded because prerequisites of `languages` were
  still legally scheduled in parallel by GNU Make.
- Host validation and archive/snapshot tests: passing.
- MIPS build of RC7: **not performed in this environment**.
- Physical PS2 validation of RC7: **not yet performed**.
- Release readiness: **blocked until a clean MIPS build and hardware matrix pass**.

No patch/overlay chain is required. RC7 is delivered as a complete source tree.

## RC14 BuildFix16 state

- `Internet` is now an installable kernel-resident system app (`TITN00010`) rather than an external ELF or Titan-native `.tmod`.
- Its TPKG v2 manifest uses `type=system-browser` and `entrypoint=@system-browser`; launch routes directly to the Browser page while Titan remains resident.
- The lightweight Browser POC parser/history/PS2IP transport has been carried into the current source tree.
- HTTPS retains the Titan Stream Bridge architecture. External stream-player handoff and download UI are disabled pending in-system/hardware validation.
- Host validation passes. Full PS2DEV MIPS linking and real-PS2 network behavior remain to be validated on the user's toolchain/hardware.

## RC14 BuildFix20 state

- Library is the permanent last Home tile and acts as the complete catalog beyond the Home presentation limit. Its Applications category retains the former Applications glyph.
- Folder content selection uses an Orbis-style 6x3 scrolling viewport over the complete selectable catalog instead of exposing only what fits on one screen.
- Package Installer can enumerate up to 96 `.pkg`/`.tthm` files across both first-device aliases and supports consecutive installs in the same session.
- App installation updates the application catalog incrementally instead of forcing a complete library scan.
- USB system updates are now represented by TPUP v1 `.pup` packages under Settings > System > Atualizacao do software do sistema. Build numbers at or below the running BuildFix20 generation are rejected.
- BuildFix20 must still be deployed once by replacing `kernel.elf`; subsequent builds can be exercised through `.pup` packages on hardware.

## RC14 BuildFix22 candidate state

- BuildFix20 Library/Updater remains the baseline; the parallel wLaunchELF native-port work is merged into it.
- `wLaunchELF` is present under `apps/wlaunchelf` as a Titan-native `.tmod`, not an external ELF wrapper.
- Titan App API v1 is append-only extended with storage/ELF services needed by file-manager class apps while preserving the original Project Nes ABI prefix.
- Current wLaunchELF milestone browses mass/MC/disc/HDD namespaces, copies regular files cooperatively, removes files/empty folders, and hands `.elf` execution to Titan's validated stage2.
- HDD writes/PFS mounts, VMC control, FTP, editor/JPG viewer, recursive folder copy and text-entry operations remain pending Titan-owned equivalents.


## RC14 BuildFix23 hardware-updater state

- TPUP installation now begins filesystem work only after storage-exclusive worker parking has been acknowledged.
- Source payload is CRC-verified before staging; input and output handles are never live simultaneously on USB.
- Staging is reread and CRC-verified before rollback-safe publish.
- Existing BuildFix20/21/22 kernels require one manual `kernel.elf` bootstrap because the updater bug is inside the running kernel itself.
- The hardware self-test remains available and is numbered Build25 in this source so it stays newer than BuildFix24.
## RC14 BuildFix24 Flow / errors / wLaunchELF state

- BuildFix23 remains the hardware-safe TPUP transport baseline; BuildFix24 is the first real multi-file update candidate intended to ride that corrected updater.
- Flow3D themes can define 1..3 primary waves with independent start/end Y, amplitude, alpha and phase. Scarlet Flow 1.1.0 uses two opposing diagonal sweeps.
- System/component errors are presented by a dedicated System Error popup and are no longer inserted into notification cards. Routine activity/success events are also suppressed from cards.
- Titan-native wLaunchELF remains responsive/in-process and the runtime readable-pointer boundary now covers the complete loaded module image so static text/constants can render on hardware. Rejected draw commands now generate explicit System Error diagnostics instead of a silent black app interior.
- BuildFix24 PUP target carries the BuildFix24 kernel, WLAUNCHELF.pkg 0.1.1 and Scarlet Flow 1.1.0 configuration.
- Host regression matrix is fully passing. MIPS linking and physical-PS2 confirmation of wLaunchELF rendering, Flow placement and the new error popup remain required.


## RC14 BuildFix25 candidate state
- Hardware TPUP updater uses a seek-free forward-only payload stream.
- BuildFix24 Flow/error-popup/wLaunchELF changes are retained.
- Requires one manual kernel replacement from BuildFix23 before the Build26 PUP self-test.

## RC14 BuildFix27 PUP / Flow / wLaunchELF state

- BuildFix25's seek-free forward-only TPUP updater is hardware-confirmed by a successful Build26 self-test PUP installation on physical PS2.
- Normal development distribution returns to `.pup`; BuildFix27 is intended to install directly over the running BuildFix25 kernel.
- Scarlet Flow 2.0.0 uses two theme-owned cubic primary-wave trajectories with independent entry/exit Y, two interior curve controls, drift, thickness, amplitude, alpha and phase. The theme can still define up to three primary waves.
- The Flow renderer no longer treats start/end as a straight baseline; it evaluates a cubic Bezier centerline before adding the low-amplitude Gerstner/noise surface motion.
- wLaunchELF 0.1.2 keeps the BuildFix24 readable-module-memory fix and adds a hardware-visible primitive canary plus explicit draw-rejection System Error diagnostics.
- The BuildFix27 PUP updates the installed `TITNWLE01` app files directly and also deposits `WLAUNCHELF.pkg`, so the post-update wLaunchELF test does not depend on manually reinstalling an older package.

## RC14 BuildFix28 state

- Scarlet Flow 3.0 is now reference-video-driven: one composite ribbon orbits/rotates over a long cycle instead of two primary ribbons crossing.
- Theme ABI remains generic and supports up to three primary waves; sweep behavior is optional per wave.
- BuildFix28 is intended to ship as TPUP using the hardware-validated forward-only updater.
- wLaunchELF remains included for the next hardware-visible application test.


## RC14 Build 35 Dynamic Theme API v2 state

- `.tthm` remains TPKG v2 and declarative-only; executable/microprogram payloads remain prohibited.
- `scene-v2` adds precomposed 512x256 RGB565 HOME/TOP frames plus 0..6 top-menu transition keyframes.
- HOME/top-menu scene themes do not render Flow; Settings, Profile, Saved Data/VMC, Library and fullscreen folder-option views reduce to the theme Flow palette/ribbon.
- Theme audio can override music, notification/profile/navigation/interface/shutdown sounds and a dedicated top-menu-enter event.
- Theme icon styling supports tint and lightweight ring/corner decorators.
- Build generation is 35. The BuildFix34 -> Build35 bootstrap PUP is kernel-only; existing wLaunchELF/Flow payloads are not republished.
- Host validation includes a BuildFix34 updater compatibility simulation of the one-file Build35 PUP; physical PS2 behavior remains authoritative.

## RC14 Build 36 Complex Theme API v3 state

- Theme Shell API v3 adds independent stable HOME/TOP full-scene idle loops to `scene-v2`; API v1 and v2 themes remain compatible.
- HOME and TOP idle animation uses monotonic millisecond timing, supports loop/ping-pong playback and resets whenever the shell leaves the stable state.
- Additional idle-frame budget is shared across HOME/TOP and capped at eight frames; with six transition frames the complete scene cache is bounded to 4 MiB EE RAM.
- `.tthm` builder/verifier now rejects package-title-ID / theme-manifest-ID mismatches in addition to executable payloads, unsafe paths and invalid scene budgets.
- Build 36 remains declarative and does not allow theme-owned ELF/IRX/VU code.
- Community authoring contract is documented in `docs/THEME_AUTHORING.md`.

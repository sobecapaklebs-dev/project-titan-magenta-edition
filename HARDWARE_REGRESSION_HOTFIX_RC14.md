# Project Titan Magenta Edition 0.3.90-rc14 — hardware regression hotfix

Scope: real-PS2 regressions reported after RC14. This hotfix intentionally keeps the RC14 renderer, native 720p double buffering, curated assets, Neutrino/Titan UL launch path, VMC/profile work, installer, and previous performance changes intact.

## 1. Idle help/footer bar

Root cause: the authored 854x448 UI canvas is vertically centered in the 1280x720 framebuffer. The footer background ended exactly at logical y=448, so the lower physical 720p margin remained uncovered and exposed the magenta background.

Fix: keep the footer/text origin unchanged, but extend only the dark footer background through the lower logical-to-physical margin. Legacy output has zero logical Y offset, so its geometry is unchanged.

## 2. Neutrino ISO + Titan UL catalog

Root cause addressed: the scanner used `opendir("mass:")` / `opendir("mass0:")` as device-availability probes and repeated that probe after normal end-of-directory. A successful child-directory scan could therefore be invalidated by an alias-root probe on real USB stacks.

Fixes:
- probe `mass:/` and `mass0:/` explicitly;
- treat `readdir()==NULL` with `errno==0` as normal EOF;
- do not re-probe a bare device alias after a successful directory scan;
- keep the existing `mass:` then `mass0:` pass order and UL/ISO discovery logic.

## 3. Audio recovery

Root causes addressed:
- a negative LIBSD/AUDSRV module-load result was treated as fatal even when the service was already resident and the AUDSRV RPC bind could work;
- audio assets missing during the first instant of USB availability were never retried;
- a failed first audio bootstrap was never retried by the kernel.

Fixes:
- AUDSRV RPC readiness is authoritative after module-load attempts;
- missing music/SFX are retried at a bounded cadence;
- the kernel retries a failed audio bootstrap every 180 rendered frames and starts music after recovery;
- late `mass:/TITAN/audio` availability no longer requires a reboot.

## 4. Debug Settings visibility

`★debug settings` is now listed only when DEX mode is enabled. The category is the final settings entry, so normal category indices stay unchanged. Disabling DEX while inside Debug Settings closes that page and returns selection to About Software.

## Validation

`bash scripts/validate_source.sh` passed after the changes, including:
- profile/VMC and mass:/mass0:/ storage fallback tests;
- Neutrino ISO + OPL UL launch tests;
- fast boot catalog/background scanner tests;
- DEX persistence;
- native 480i/720p double-buffer renderer contract;
- audio runtime (including resident-module and late-asset recovery cases);
- Magenta audio contract;
- Home grid/UL contract;
- UTF-8/source hardening/install transaction/curated asset checks.

No MIPS build was performed in the preparation environment because the PS2DEV toolchain is not installed there. No PS2 drive was modified.

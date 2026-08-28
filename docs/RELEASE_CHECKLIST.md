# Release Checklist

## A. Source hygiene

- [ ] Run `scripts/prepublish_audit.sh`.
- [ ] Run the final release-freeze audit against the exact hardware-tested tree.
- [ ] No ROM / ISO / BIOS / proprietary game data.
- [ ] No firmware blobs without redistribution permission.
- [ ] No credentials, tokens, `.env` files or private keys.
- [ ] No personal debug dumps or USB captures.
- [ ] No accidental absolute local paths in public release files.
- [ ] Third-party license / notice files are preserved.

## B. Build

- [ ] Host/source validation passes on the frozen tree.
- [ ] Kernel builds with the target PS2DEV toolchain.
- [ ] Native packages build and verify.
- [ ] `BOOT.ELF` is the uppercase Memory Card boot artifact.
- [ ] Release installer TINS has autonomous flag `0`.
- [ ] Release installer TINS contains `BOOT.ELF`.
- [ ] Release installer TINS embeds the same release-candidate kernel.
- [ ] Release binaries, if distributed separately, have SHA-256 files.

## C. Hardware — current candidate

Already exercised in the current test cycle:

- [x] Semi-autonomous installer completed on physical hardware.
- [x] DEV1 cold boot reached the Titan bootloader and USB kernel.
- [x] Library application scan passed the previous 68% freeze.
- [x] Package Installer commit/persistence path passed in the tested flow.
- [x] Project NES install/persist/launch/exit/relaunch passed.
- [x] wLaunchELF install/persist passed.
- [x] TitanEYE install/persist/runtime launch passed in the tested setup.
- [x] Welcome Cross-button presentation passed.

Still keep compatibility claims scoped to hardware actually tested:

- [ ] Final EN / PT-BR / ES smoke pass on the frozen candidate.
- [ ] Any controller/network/title combination advertised as specifically supported is represented by hardware evidence.

## D. Initial-release scope

- [x] Semi-autonomous / DEV1 is the supported install method.
- [x] Autonomous FMCB is disabled from the initial public release media.
- [x] Autonomous FMCB source is retained for future development.
- [ ] Documentation and release notes consistently reflect this scope.

## E. GitHub

- [ ] README describes the candidate accurately.
- [ ] `KNOWN_ISSUES.md` reflects current hardware results.
- [ ] `RELEASE_STATUS.md` is present.
- [ ] `CONTRIBUTING.md` is present.
- [ ] `SECURITY.md` is present.
- [x] License choice for original Project Titan code is explicit: MIT.
- [ ] Repository contains source, not private build/test history.
- [ ] Public export SHA-256 is recorded.
- [ ] Tag only after the final audited commit is known.

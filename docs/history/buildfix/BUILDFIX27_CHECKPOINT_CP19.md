# BuildFix27 Checkpoint CP19
STATUS: HOST_VALIDATED
PREVIOUS_VALID: CP18
BLOCK: wLaunchELF Titan-owned JPEG/PNG FIT viewer

- generalized existing hardened decoder with APP_MEDIA FIT/letterbox target
- existing icon/background artwork crop behavior preserved
- appended `media_present_image_file` at end of TitanAppApiV1
- kernel bounds compressed source to 512 KiB and owns Storage/libjpeg/libpng/media surface
- wLaunchELF uses only the new Titan API + media_draw
- executable JPEG and PNG fixture verifies full-image FIT letterboxing
- malformed image hardening and old TIMG conversion remain green

Contracts:
- buildfix27-app-image-fit: PASS
- buildfix27-wlaunchelf-image-viewer-contract: PASS
- Titan App runtime ASan/UBSan + ET_REL/R5900 fixtures: PASS
- CP16 ABI fixture: PASS
- asset conversion/hardening: PASS
- CP17/CP18 wLaunchELF regressions: PASS
- Project NES host suite: PASS

NEXT: CP20 HDD/PFS/VMC capability and mount-lifecycle adapters through Titan Storage/VFS.

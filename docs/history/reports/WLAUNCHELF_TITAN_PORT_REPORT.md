
## CP17 — Titan-rendered bounded text viewer/editor

Status: HOST_VALIDATED.

- Text files (`.txt/.cfg/.ini/.log/.md/.json/.xml`) are opened only through Titan Storage/VFS.
- Viewer payload is bounded to 8 KiB and rejects embedded NUL/control-binary content.
- Rendering uses Titan App draw APIs; no gsKit/framebuffer ownership is introduced.
- Editing is deliberately limited to 255 bytes, matching the shared Titan Text Input service instead of silently truncating larger files.
- Publication is transactional: edit -> `<path>.titantmp` -> `storage_commit_replace`; the known-good destination is never written directly.
- Viewer allocation is accounted through Titan App `alloc/release` and reclaimed on shutdown.
- Strict `-Werror`, ASan/UBSan lifecycle/editor fixture and BuildFix27 regression contracts pass.

## CP18 — bounded cooperative recursive copy/move

Status: HOST_VALIDATED.

- Directory transfers use an iterative DFS stack capped at depth 8; no C recursion and no whole-tree in-memory enumeration.
- Each update performs at most one directory-entry traversal action or one bounded file-copy chunk.
- Each file still publishes through `.titantmp` + `storage_commit_replace`.
- Recursive copy failure preserves the source tree; partial destination state is reported truthfully rather than hidden.
- Recursive move is two-phase: the full destination tree is copied first with source untouched, then a second post-order traversal removes source files/directories.
- Copying a directory into itself/its descendant and merging into an existing destination directory are rejected.
- ASan/UBSan host fixture proves nested copy and move and asserts no source removal occurs before all destination file commits.

## CP19 — Titan-owned JPEG/PNG viewer

Status: HOST_VALIDATED.

- Reused the existing hardened libjpeg/libpng scanline decoder instead of importing an app-owned image stack.
- Added an internal 512x256 RGB565 FIT target: full source image is preserved and letterboxed, while existing artwork/TIMG paths retain their historical center-crop semantics.
- App ABI v1 remains append-only with `media_present_image_file(path)` after the CP15 network tail.
- Runtime bounds compressed input to 512 KiB, reads through Titan Storage, decodes inside the kernel, yields every 8 rows on EE, and installs the result in the existing Titan media surface.
- wLaunchELF recognizes JPG/JPEG/PNG, invokes only the Titan image API and draws with `media_draw`; it owns neither libjpeg/libpng nor GS/VRAM/FileXio.
- Executable JPEG+PNG fixture verifies letterbox borders and nonzero image center; legacy TIMG conversion and malformed-image ASan/UBSan tests remain green.

## CP20 — managed storage mount capability gate

Status: HOST_VALIDATED / BACKEND_LIMIT_RECORDED.

- `hdd0:` remains truthfully exposed read-only through Titan Storage/VFS.
- No PFS mount backend exists in the current kernel source: no `fileXioMount/fileXioUmount` or equivalent Titan provider is present.
- `pfs:` and `vmc:` namespaces remain denied to native apps; wLaunchELF is not permitted to import its own HDD/PFS/IOP stack.
- Titan VMC remains an 8 MiB profile image managed/verified by `TitanVmc_*` and consumed by Titan game backends; it is not falsely exposed as a browseable mounted filesystem.
- A host gate records these facts and will fail if mount ownership changes without a corresponding integration audit.
- PFS browsing and mounted VMC filesystem browsing therefore remain an explicit backend prerequisite, not silently dropped scope.

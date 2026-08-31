# BuildFix27 VMC Traversal Report — CP32

CP32 generalizes the CP31 root-only adapter into bounded read-only nested directory traversal while retaining Titan ownership of the raw 8 MiB image.

- `TitanVmc_ListDirectory()` accepts normalized VMC-relative paths only.
- absolute paths, `.`/`..`, empty components, oversized components and paths deeper than 8 components fail closed.
- directory resolution follows only entries marked existing+directory and validates cluster/length bounds.
- FAT traversal remains bounded and rejects free/unallocated/self-referential links.
- Titan App ABI appends `vmc_list_active_path`; runtime validates that the input string belongs to module/heap memory before copying it into a bounded kernel buffer.
- wLaunchELF maintains a separate `vmc_path`, enters directories with X, climbs with O and exits VMC mode with R2; all VMC entries remain read-only.
- no raw mount, FileXio, gsKit or ExecPS2 ownership is exposed.

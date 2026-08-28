# BuildFix27 VMC Export Report — CP33

CP33 completes the read-only VMC adapter path needed by the native wLaunchELF port without creating a raw VMC mount.

- `TitanVmc_ReadFile()` resolves a normalized VMC-relative regular-file path, follows the file FAT chain with bounded validation, and returns only the requested byte range.
- Titan App ABI appends `vmc_read_active_file`; capacity is capped at 16 KiB and destination memory must belong to the module/its accounted heap.
- wLaunchELF exports a selected VMC file to the currently open ordinary-storage directory cooperatively.
- export writes only `destination.titantmp`; final publication uses `storage_commit_replace()` after the expected file size is completely read.
- any read/write/commit failure removes the temporary file and preserves the existing destination.
- VMC mutation remains unsupported: no write path touches the VMC image.

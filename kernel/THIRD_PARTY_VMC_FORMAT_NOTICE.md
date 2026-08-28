# Project Titan VMC formatter — format references

The Project Titan VMC formatter writes a PlayStation 2 memory-card filesystem
for an 8 MiB image. The implementation in `src/services/titan_vmc.c` was
written for Project Titan and uses the public filesystem layout documented by
Ross Ridge in **PlayStation 2 Memory Card File System**.

Compatibility choices were cross-checked against the VMC generator in
**Open PS2 Loader**, particularly its 8 MiB layout, erased-cluster state,
indirect FAT/FAT placement, root-directory entries, formatted flag and
`cardflags = 0x2b` behavior.

Open PS2 Loader is licensed under the Academic Free License 3.0. No OPL binary
or source file is bundled as part of this formatter; this notice records the
technical compatibility reference used during implementation and validation.

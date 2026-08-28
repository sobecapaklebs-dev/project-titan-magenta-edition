# BuildFix27 Final Provenance / License

## Project Titan source
Project Titan components retain their existing per-file/project licensing notices.

## Project NES / FCEUmm
- upstream repository: https://github.com/libretro/libretro-fceumm
- frozen commit: `b5e3566515c27dc66c9c20572171673126532e06`
- frozen Git tree: `6adfb382bf8a369f1a6ee224e89c08d981e22cec`
- supplied upstream ZIP SHA-256: `8153226f90508b46d03e217e91508f452c6ef0b4ddc797c2fac776f902081f12`
- upstream files imported: 687
- upstream license: GNU GPL v2
- imported source location: `third_party/fceumm/modern/`
- imported-file verification: `external/fceumm/FCEUMM_IMPORTED_SHA256SUMS`
- Project NES combined-module notice: `apps/project-nes/resources/COMBINED_LICENSE.txt`
- upstream GPL text used for package provenance: `third_party/fceumm/modern/Copying`

The frozen source was validated by recomputing the Git tree from the user-supplied archive before deterministic import. No alternate provider or later FCEUmm revision was substituted.

## PFS target runtime dependencies
The BF27 source contains integration/loader hooks but does not fabricate the official PS2SDK binaries:
- `ps2atad.irx`
- `ps2hdd.irx`
- `ps2fs.irx`

They must come from the external PS2SDK environment and are validated by `scripts/validate_pfs_target_prereqs.py`.

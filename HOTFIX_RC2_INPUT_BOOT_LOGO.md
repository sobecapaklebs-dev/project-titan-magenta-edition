# Project Titan Magenta Edition 0.3.90-RC2 input/boot-logo hotfix

This hotfix addresses the first PCSX2 smoke-test findings without compiling MIPS
code or touching any PlayStation 2 storage device.

## Fixed

- Restores the PS2 line boot mark that existed in the preserved pre-audit tree.
- Prevents `generate_system_assets.py` from overwriting an already supplied boot
  mark when the deterministic system assets are refreshed.
- Keeps PAD connection state across PADMAN transient command states. Previously,
  `PAD_STATE_EXECCMD` cleared `main_mode_requested`, causing `padSetMainMode()` to
  be reissued and stable reads to be treated as reconnections.
- Lets the welcome screen accept Cross when it is already held as the screen
  becomes ready, matching the bootloader-to-kernel handoff behavior.

## Validation scope

Host validation is safe to run. A PS2/MIPS build and hardware/runtime validation
are still required after applying the hotfix.

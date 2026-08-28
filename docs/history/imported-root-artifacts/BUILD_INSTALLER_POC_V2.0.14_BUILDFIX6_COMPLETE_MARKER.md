# Project Titan Installer POC v2.0.14 BuildFix6 — final commit marker

Scope is restricted to the final `INSTALL.COMPLETE` marker at 97%.

## Hardware evidence

BuildFix5 successfully progressed past the previous `titan.elf` publication freeze and reached 97% with the UI wave still alive. At 97%, the installer state machine has already published all manifest entries, closed the TINS stream, and is in `TITAN_INSTALL_JOB_WRITE_COMPLETE`. The only remaining operation before backup cleanup is creating/writing/closing `mass:/TITAN/INSTALL.COMPLETE`.

The BuildFix5 marker used FileXio NOWAIT and could remain pending indefinitely while the main render loop continued.

## BuildFix6

Only the tiny final marker uses a direct POSIX `open/write/close` transaction. Payload staging, CRC verification, USB->MC publication, rollback transport, TINS handling and all UI/render behavior are otherwise unchanged. The synchronous marker may cause a brief final-frame pause, but cannot leave the installer permanently polling an uncompleted FileXio marker request.

New telemetry: `OPEN_COMPLETE_MARKER_POSIX`, `WRITE_COMPLETE_MARKER_POSIX`, `CLOSE_COMPLETE_MARKER_POSIX`, and `COMPLETE_MARKER_COMMITTED`.

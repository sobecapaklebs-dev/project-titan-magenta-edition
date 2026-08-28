# Project Titan RT1 BuildFix8 — Persistent Diagnostic Sessions

BF8 fixes the BF7 flight-recorder lifetime bug discovered on physical hardware: rebooting the console to enter Debug Settings created a new `titan-current.bin`, destroying the crash session that the user wanted to inspect.

## Session preservation

On the first diagnostics storage pump of a new boot, before opening the new current log, the recorder rotates:

- `titan-previous-4.bin` -> `titan-previous-5.bin`
- `titan-previous-3.bin` -> `titan-previous-4.bin`
- `titan-previous-2.bin` -> `titan-previous-3.bin`
- `titan-previous.bin` -> `titan-previous-2.bin`
- `titan-current.bin` -> `titan-previous.bin`

Only after that does BF8 create/truncate the new `titan-current.bin`.

## Disk format v2

The diagnostics header is version 2 and includes flags. `TITAN_DIAG_FLAG_CLEAN_SHUTDOWN` is persisted on controlled console shutdown/restart. An older BF7 v1 recorder remains readable and is reported as `UNCLEAN_OR_RESET`, since v1 had no clean-shutdown flag.

## Debug Settings

Debug Settings now has 11 rows. The existing current-session export remains and a new `Export Previous Boot` action produces:

`mass:/TITAN/logs/titan-previous.txt`

The text header explicitly states either `CLEAN_SHUTDOWN` or `UNCLEAN_OR_RESET`.

## Scope

No bootloader, Wi-Fi handoff, Asset Service, theme, package, or installer behavior was changed except for diagnostic hooks on the already-existing controlled shutdown/restart paths.

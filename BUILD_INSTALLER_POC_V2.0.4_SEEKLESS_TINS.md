# Installer POC v2.0.4 — seekless TINS hardware fix

Hardware diagnosis: v2.0.3 can open/read the TINS magic through the inherited wLaunchELF USB stack, but TINS validation still used lseek()/SEEK_END repeatedly. Project Titan's TPUP hardware path had already established that USB streams must be consumed sequentially on real PS2 hardware.

v2.0.4 keeps TINS v1 unchanged and changes only the PS2 reader:
- header + entry table parsed sequentially;
- table CRC calculated while table bytes are read;
- payload preflight is forward-only;
- exact total size is validated by consuming the declared stream and requiring EOF;
- installation staging is forward-only; skipped EXPLOIT records are discarded sequentially;
- no lseek() remains in the active installer TINS path.

Existing TITAN_SYSTEM.TINS files remain compatible and do not need regeneration.

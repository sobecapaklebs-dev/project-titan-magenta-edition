# Porting Applications to Titan Native

1. Keep the application's functional core; remove standalone platform ownership.
2. Replace graphics/input/audio/storage/network backends with Titan App API adapters.
3. Keep `titan_app_init()` pure and move work to cooperative update steps.
4. Never create your own FileXio/PS2IP/gsKit/libpad/IRX lifecycle inside a `.tmod`.
5. Use temporary files + `storage_commit_replace` for destructive publication.
6. Use asynchronous text/network APIs; do not block the frame loop.
7. Treat external ELF launch as an explicit handoff, not as native execution.
8. Gate `struct_size` through the final field actually consumed by your module.
9. Run host lifecycle/fault tests, then R5900/PCSX2/hardware gates when the target toolchain is available.

## Networked utilities

For HTTP clients, use `network_get_*`. For an FTP-style utility, control Titan's FTP service with
`ftp_start/stop/status`; do not implement a second socket stack. The current Titan FTP service
supports anonymous control, PASV/EPSV, LIST/NLST, RETR, and transactional STOR.

## Storage limitations that remain explicit

BuildFix27 does not invent PFS or mounted-VMC providers. `hdd0:` remains read-only under the
current policy; PFS/VMC namespaces stay kernel-owned until a real mount backend exists.

# BuildFix27 Checkpoint CP36
STATUS: HOST_VALIDATED / TARGET_PROVIDER_BLOCKED
PREVIOUS_VALID: CP35
BLOCK: kernel-mediated app PFS request/status ABI

- Added append-only pfs_available/pfs_mount/pfs_unmount/pfs_status API tail.
- Runtime owns per-session PFS slots and reclaims them during app teardown.
- pfsN:/ storage paths are accepted only for ONLINE TitanStorageMount slots with matching read/write capabilities.
- storage_list_volumes advertises only provider-backed ONLINE PFS roots.
- Apps receive no FileXio descriptor, mount primitive, IRX ownership or raw provider handle.
- With no concrete PFS provider, availability/mount fail closed and no pfsN root appears.
- Existing VMC/FTP/network/storage ABI remains append-only and compatible.

NEXT: CP37 wLaunchELF PFS mount/status/unmount UX.

# BuildFix27 CP39 — PS2 PFS platform provenance

Primary source: ps2dev/ps2sdk commit `a13b5971ec0e39c7ba8b8559b80a4e81c8425352`.

Verified interfaces:
- `ee/rpc/filexio/include/fileXio_rpc.h`: `fileXioMount(const char*, const char*, int)` and `fileXioUmount(const char*)`.
- `common/include/io_common.h`: `FIO_MT_RDWR = 0x00`, `FIO_MT_RDONLY = 0x01`.
- `ee/rpc/hdd/src/libhdd.c`: official PS2SDK code mounts PFS partitions using `fileXioMount("pfs0:", hddFs[count].filename, FIO_MT_RDONLY)`.

Titan policy:
- convert internal `pfsN:/` to FileXio mountpoint `pfsN:` only inside the PS2 platform hook;
- advertise provider only if both `hdd` and `pfs` devices are reported by FileXio;
- map Titan write capability to `FIO_MT_RDWR`, otherwise `FIO_MT_RDONLY`;
- do not load/embed a PFS IRX in CP39 because no corresponding target binary/source build is materialized in this workspace.

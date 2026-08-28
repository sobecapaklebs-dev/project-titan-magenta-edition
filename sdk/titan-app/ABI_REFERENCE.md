# Titan App ABI v1 Reference — BuildFix27

## Required exports

```c
int titan_app_init(const TitanAppApiV1 *api, const char *app_root);
void titan_app_update(const TitanAppInput *input, uint32_t delta_ms);
void titan_app_draw(void);
void titan_app_shutdown(void); /* optional */
```

`titan_app_init()` is a handshake. Store/validate the API pointer and initialize module-local
state only. Resource-heavy work belongs after the runtime commits the session ACTIVE.

## BuildFix27 append-only service tail

The BuildFix27 tail includes, in order, reusable Storage/VFS volume discovery and atomic publish,
shared text input, asynchronous network GET, Titan-owned image decode/presentation, and FTP
lifecycle/status. Existing offsets before each append remain stable.

### Storage/VFS

`storage_list_volumes()` reports only volumes Titan can actually probe. Capability flags are
truthful; no generic free-space value is fabricated when a provider cannot supply it.
`storage_commit_replace()` publishes a temporary file transactionally.

### Network GET

`network_get_begin/status/read/cancel` is asynchronous. Apps never receive sockets, DNS handles,
TLS state, or worker threads. Result bytes remain Titan-owned and are copied through `read`.

### FTP

`ftp_start(root, port)`, `ftp_stop()`, and `ftp_status()` expose service lifecycle only.
The service uses Titan Network Stream internally with bounded opaque handles and Storage/VFS for
LIST/RETR/STOR. STOR writes to `<target>.titantmp` and atomically publishes only after clean EOF.

### Media image file

`media_present_image_file()` decodes supported PNG/JPEG through the kernel and presents a
512x256 RGB565 FIT/letterbox surface. Apps never own libjpeg/libpng or GS/VRAM state.

## Reserved controls

- `SELECT+CIRCLE`: emergency exit, intercepted before app update.
- `R3`: Titan fullscreen/window chrome toggle.

## Failure containment

Outstanding app allocations, media/audio state, network requests, text input, module image,
storage exclusivity, and Titan-owned FTP service state are reclaimed by runtime destruction.
In-process hard locks cannot be fully isolated without process/MMU isolation; existing Titan
watchdog/recovery remains the outer boundary.

## BuildFix29 append-only input tail

`input_get_state(player, ...)` returns normalized 32-bit state (buttons/sticks/triggers/pointer) for the assigned player. `input_get_device(player, ...)` returns stable logical identity, transport, VID/PID, player, capabilities and honest battery telemetry. Gate both calls using `offsetof(TitanAppApiV1, input_get_device)` and `api->struct_size`.

System/Home is reserved to the operating system. Capability discovery is allowed; delivery of the system-button press to app code is not part of this ABI while Titan Session/Overlay is active.

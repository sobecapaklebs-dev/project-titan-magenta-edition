# Titan Application SDK — BuildFix27

This SDK targets trusted in-process `titan-native` `.tmod` applications for Project Titan.
It is **not** a hardware sandbox and it does not grant direct ownership of GS, AUDSRV,
FileXio, PS2IP, IOP modules, sockets, or kernel threads.

## Runtime model

`IDLE -> PREPARE -> LOAD -> CREATE -> START -> ACTIVE -> EXIT_REQUEST -> QUIESCE -> DESTROY -> IDLE`

`SELECT+CIRCLE` is always Titan-owned. R3 is reserved for Titan window/fullscreen chrome.
Apps are cooperative: `titan_app_update()` must return promptly.

## ABI compatibility

`TITAN_APP_API_VERSION` remains 1. BuildFix27 extends `TitanAppApiV1` only by appending fields.
Do **not** require `api->struct_size >= sizeof(TitanAppApiV1)` unless your app truly consumes the
entire newest tail. Gate against the last field you use:

```c
#define TITAN_APP_REQUIRE_FIELD(api, field) \
    ((api) != 0 && (api)->abi_version == TITAN_APP_API_VERSION && \
     (api)->struct_size >= offsetof(TitanAppApiV1, field) + sizeof((api)->field))
```

## Ownership summary

- UI/video: Titan renderer and media surfaces.
- Audio: Titan owns AUDSRV; apps submit bounded PCM.
- Storage: sandbox file APIs plus policy-checked trusted VFS operations.
- Network GET: asynchronous Titan-owned DNS/TCP/TLS/HTTP.
- FTP: app controls lifecycle/status only; Titan owns TCP passive/control/data channels and VFS I/O.
- Text input: global asynchronous Titan text-input service.
- External ELF: explicit Titan stage2 handoff; never relabeled as native.

See `ABI_REFERENCE.md` and `PORTING_GUIDE.md`.

## BuildFix29 Unified Input queries

The ABI v1 tail now includes optional `input_get_state()` and `input_get_device()` fields. Check `struct_size` before use. They provide normalized multi-controller state and device metadata without exposing PADMAN, PADEMU, USB HID or Bluetooth transports. PS/Home/Guide remains Titan-owned: a device may advertise `TITAN_APP_INPUT_CAP_SYSTEM_BUTTON`, but `TITAN_APP_NBTN_SYSTEM` is masked from app input while Titan owns Session/Overlay routing.

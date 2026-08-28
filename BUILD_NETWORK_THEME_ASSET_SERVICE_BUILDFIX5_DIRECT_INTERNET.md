# RT1 BuildFix5 — Native Direct Internet Assets

BuildFix5 replaces BF4's development PC gateway transport while preserving the
BF4 responsiveness architecture.

## Functional delta

- PS2 downloads assets itself through Titan Network Core / PS2IP.
- No runtime `assets.cfg`, PC IP, or local gateway is required.
- HTTP and verified HTTPS are handled by `TitanHttpClient`.
- Direct public provider resolution uses the PS2 startup serial.
- PNG/JPEG decode runs only in the low-priority asset worker and is row-bounded.
- Final cache data stays TIMG v1 PSM16 (32x32 icon, 256x128 background).
- P0/P1 visible-icon priority, background debounce, idle enrichment, 8 KiB
  storage quanta, `.part` publication, CRC validation and transient VRAM upload
  are unchanged from BF4.

## Boot/ownership boundary

No bootloader code changed. Network remains deferred from the shell critical
boot path by the BF2 architecture. Asset downloading cannot start until Titan
Network Core reports ONLINE.

## Validation boundary

Host contracts cover source syntax, provider resolution, HTTP/S construction,
TLS hostname/time verification, image conversion, cache scheduling and existing
network/theme/installer/BDM regressions. The current environment does not have
the user's PS2DEV cross-toolchain, so the final R5900 link and physical-PS2
network/TLS behavior remain hardware-validation items.

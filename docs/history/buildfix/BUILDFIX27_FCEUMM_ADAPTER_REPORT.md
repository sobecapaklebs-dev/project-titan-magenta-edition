# BuildFix27 CP46 — FCEUmm/libretro Adapter Report

The Project NES frontend no longer assumes that the Legacy NROM core owns frame readiness or teardown.
A link-gated adapter converts the stable libretro callback surface into `ProjectNesCoreBackend` while keeping Titan services exclusively in the frontend/runtime.

Host fake-core coverage exercised:
- RGB565 negotiation and 256x240 frame delivery into a 512x256 Titan-owned surface;
- A/Up input callback mapping and rejection of inactive B;
- 800-frame stereo PCM batch capture and 1024-frame frontend capacity;
- 8 KiB save RAM size/export/import/dirty tracking;
- reset, unload and deinit lifecycle.

The real FCEUmm core is intentionally not linked until the frozen GPL source archive is materially imported and validated by the CP42 importer.

# Theme Engine v4 Resource Budget — BuildFix28 CP13

scene-v4 is bounded by the authored `scene_v4_vram_budget`, `scene_v4_ee_ram_budget`, and `scene_v4_reserved_headroom` envelope validated at package time and runtime.

The renderer does not create a second VRAM manager. RGB565 layer assets are loaded only during theme refresh into EE RAM, bounded by the declared EE budget. Drawing uses TitanVideo's existing 256x64 transient scratch surface; this preserves the existing 720p VRAM contract and leaves pikaVR/TitanVideo as the single GS resource/accounting owner.

Pressure policy is deterministic: NORMAL keeps all layers; warning pressure suppresses DECORATIVE first; higher pressure suppresses ENHANCED; critical boot/recovery state still uses the existing safe shell fallback. ESSENTIAL content is retained as long as the shell remains outside minimum recovery mode.

Frame path rules: no malloc/free, no filesystem I/O, no network I/O. Fixed staging buffers are 256x128 RGB565 for bounded rotation and 256x64 RGB565 for scratch uploads.

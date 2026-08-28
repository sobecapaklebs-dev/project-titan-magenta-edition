# Renderer and VRAM budget

The logical canvas is 854×448. 720p uses one uniform square-preserving scale
with a centered safe area; X and Y are not stretched independently.

- 480i: 640×448, PSM32, double buffer.
- 720p: 1280×720 visible, two PSM16 buffers with 1280×768 physical
  storage, completed-buffer presentation at VSync.
- Native resident prefix: 262,144 bytes (wave RGBA32; font/UI PSM8 alpha
  masks; shared RGBA32 CLUT; PSM16 avatar scratch).
- Native framebuffers: 2 × 1,966,080 bytes. Total: exactly 4,194,304 bytes.
- Font and UI alpha values remain exact indices into the shared CLUT; source
  assets and their visible color/alpha contract are unchanged.
- The optional text-input cache is replaced by its existing live-draw path in
  native mode. Avatars retain their original PSM16 pixels and stream in
  bounded eight-cell chunks.
- System icons: compact regions in the UI texture atlas, one textured sprite per
  icon rather than thousands of CPU-generated points.
- PATH1 VU1 wave: followed by a canonical PATH3 state barrier restoring frame,
  z-buffer, scissor, offsets, texture, clamp, alpha, test and primitive state.

PSM16 dithering is deliberately a hardware A/B gate. The release leaves global
DTHE disabled until text, icons, gradients and alpha have been compared on the
physical PS2 in both 480i and 720p. Enabling an untested global matrix is not
classified as a source fix.

## TGR game mode

TGR retains its dedicated 640x448 PSM32 double buffer and Z16 configuration.
Game textures occupy one 1 MiB arena rather than one global VRAM allocation per
texture. The arena supports mixed PSM16/PSM32 suballocations in 256-byte blocks;
binary-alpha textures remain PSM16 and only textures with intermediate alpha
pay the PSM32 cost. See `TGR_SM64_PS2_AUDIT.md` for the storage and lifetime
proofs.

/*
 * gfx_backend.h — Render-backend selection (GL default; Metal opt-in on macOS).
 */
#ifndef GFX_BACKEND_H
#define GFX_BACKEND_H

#include <stdbool.h>

/* True when the native Metal backend should be used. macOS-only, opt-in via the
 * GE007_RENDERER=metal environment variable. False on every other platform and
 * whenever the flag is unset — so OpenGL stays the default everywhere. The
 * result is cached on first call; it must be read consistently before and after
 * window creation (GL vs Metal window flags are mutually exclusive). */
bool gfx_backend_use_metal(void);

/* True when the experimental single cross-platform WebGPU (wgpu-native) backend
 * should be used, selected via GE007_RENDERER=webgpu. Available on all platforms;
 * only compiled/linked when MGB64_WEBGPU_BACKEND. Cached on first call and read
 * consistently before/after window creation (on macOS a WebGPU window is a Metal
 * window, mutually exclusive with a GL window). Off by default so GL stays the
 * default everywhere until the deliberate flip. */
bool gfx_backend_use_webgpu(void);

/* True when the OpenGL backend is active — i.e. NEITHER Metal NOR WebGPU. This
 * is the correct guard for GL-context-requiring code paths in the shared
 * interpreter (direct gl* calls, SDL_GL_SwapWindow, the GL minimap overlay):
 * gating them on `!use_metal()` alone lets a WebGPU session — which also has no
 * GL context — fall into GL calls and crash. Prefer this over `!use_metal()`
 * wherever the intent is "only when GL owns the frame". */
bool gfx_backend_use_opengl(void);

/* Force the OpenGL backend regardless of GE007_RENDERER / the WebGPU default.
 * Called by platform_sdl.c when the game adopts the MGB64_APP launcher's GL
 * window (that window has no CAMetalLayer, so WebGPU/Metal cannot render into
 * it). After this, use_webgpu()/use_metal() return false and use_opengl() true. */
void gfx_backend_force_opengl(void);

#endif /* GFX_BACKEND_H */

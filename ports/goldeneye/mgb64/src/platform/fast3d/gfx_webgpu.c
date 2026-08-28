/*
 * gfx_webgpu.c — Cross-platform WebGPU (wgpu-native) render backend.
 *
 * Implements the Fast3D `GfxRenderingAPI` vtable (gfx_rendering_api.h) — the
 * same ~23-function seam gfx_opengl.c and gfx_metal.mm fill — against the
 * standard webgpu.h C API. Selected at runtime by GE007_RENDERER=webgpu;
 * compiled and linked only when MGB64_WEBGPU_BACKEND (see cmake/webgpu.cmake).
 * Off by default so the OpenGL backend stays byte-identical until the
 * deliberate flip (Task 8 of docs/superpowers/plans/2026-07-13-webgpu-backend.md).
 *
 * TASK 1 SCOPE (this file at this stage): bring-up + a cleared frame in the
 * real MGB64 window.
 *   - init()        creates instance -> surface -> adapter -> device -> queue
 *                   (the spike's proven sequence) plus a WGPUSurface from the
 *                   platform window (a CAMetalLayer on macOS).
 *   - start_frame() lazily (re)configures the surface to the frontend
 *                   resolution, acquires the swapchain texture, and opens a
 *                   render pass that CLEARS to the frame's charcoal color.
 *   - end_frame()   ends the pass, submits, and presents.
 *   - on_resize()   forces a reconfigure on the next frame.
 * The shader / texture / state / draw entry points are deliberately safe
 * no-op stubs so gfx_pc.c's full DL-interpreter frame loop runs to a cleared
 * frame WITHOUT crashing. They are implemented in Tasks 2-6:
 *   T2 textures+samplers, T3 WGSL combiner emitter + draw_triangles,
 *   T4 depth/viewport/scissor/blend -> parity, T5 read_framebuffer_rgb,
 *   T6 draw_modern_mesh.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx_webgpu_compat.h"   /* dialect seam: webgpu.h/wgpu.h, pump, waits, surface */

#include "gfx_rendering_api.h"
#include "gfx_webgpu.h"          /* public surface helper (shared with AppHost) */
#include "gfx_webgpu_shader.h"   /* WGSL combiner emitter (Task 3) */
#include "gfx_uniforms.h"        /* g_pc* render/post-FX uniform state (shared w/ GL/Metal) */

/* gfx_current_dimensions is the frontend render resolution the viewports and
 * T&L are computed against (gfx_pc.c). Declared here exactly as gfx_metal.mm
 * does — the shared header is C++-guarded, so each backend re-declares the POD
 * view it needs. Read every frame to size the swapchain. */
struct GfxDimensions { uint32_t width, height; float aspect_ratio; };
extern struct GfxDimensions gfx_current_dimensions;

/* Platform window handle. On macOS the SDL window is a Metal-layer window
 * (platform_sdl.c creates the view when gfx_backend_use_webgpu() is true) and
 * platformGetMetalLayer() hands back its CAMetalLayer, which wgpu-native wraps
 * as a WGPUSurface. HWND/X11/Wayland handles arrive in Task 7. */
#ifdef __APPLE__
extern void *platformGetMetalLayer(void);
#else
/* platform_sdl.c: native window handles for non-macOS surface creation.
 * Returns a windowing-system tag (2=Win32, 3=X11, 4=Wayland; 0=unknown). */
extern int platformWebGpuWindowInfo(void *sdl_window, void **out1, void **out2,
                                    unsigned long long *out_win);
#endif
/* The SDL_Window the engine renders into (platform_sdl.c); NULL before window
 * creation. The app shell passes its own window to the bring-up helper instead. */
extern void *platformGetSdlWindow(void);

/* ------------------------------------------------------------------------
 * Backend state
 * ---------------------------------------------------------------------- */
static WGPUInstance      s_instance = NULL;
static WGPUAdapter       s_adapter  = NULL;
static WGPUDevice        s_device   = NULL;
static WGPUQueue         s_queue    = NULL;
static WGPUSurface       s_surface  = NULL;
static WGPUTextureFormat s_surface_format = WGPUTextureFormat_Undefined;
static bool              s_ready    = false;   /* device + surface both live */
/* WEB-025: set true by the device-lost callback (GPU process restart / driver
 * reset). Latches s_ready=false so every frame-path guard skips work; the JS
 * shell shows a "device lost — reload" panel. Full re-init is out of scope. */
static volatile bool     s_device_lost = false;
/* When the app shell owns the device/surface (launcher → game handoff), the
 * engine adopts them and must NOT release them at teardown. False = we created
 * them ourselves (standalone --level boot) and own their lifetime. */
static bool              s_owns_device = false;
/* WEB-015: the maxTextureDimension2D actually granted to the device. WebGPU's
 * DEFAULT device limit is 8192, but most desktop GPUs support 16384; bring-up
 * requests the adapter's real max and records the granted value here so the
 * offscreen-dim query and the upload-size reject both reflect the true cap
 * (not the 8192 default that clamped RenderScale 2 into resample blur). Stays
 * at the guaranteed 8192 floor when bring-up is skipped (host-adopted device). */
static uint32_t          s_max_tex_dim = 8192;

/* Host WebGPU handoff (src/platform/host_window.c). Present only when the app
 * shell created a device/queue/surface and registered them before boot. */
extern int   platformHasHostWebGpu(void);
extern void *platformHostWgpuInstance(void);
extern void *platformHostWgpuAdapter(void);
extern void *platformHostWgpuDevice(void);
extern void *platformHostWgpuQueue(void);
extern void *platformHostWgpuSurface(void);
extern int   platformHostWgpuSurfaceFormat(void);

/* Configured swapchain size; 0 forces a (re)configure on the next start_frame. */
static uint32_t s_cfg_w = 0, s_cfg_h = 0;

/* PERF-020: resize debounce state. The last requested (not-yet-committed) size and
 * how many consecutive frames it has been held; a drag is only applied once the size
 * settles for WGPU_RESIZE_STABLE_FRAMES frames (see wgpu_start_frame). */
static uint32_t s_resize_pending_w = 0, s_resize_pending_h = 0;
static int      s_resize_stable = 0;

/* Offscreen scene target: the game renders here (not straight to the surface),
 * so rendering is independent of window visibility (a hidden/occluded window has
 * no drawable) and the frame can be read back (screenshots, Task 5). BGRA8 to
 * match the surface so end_frame presents with a plain texture-to-texture copy.
 * Re-created when the render resolution changes. */
static WGPUTexture           s_scene_tex  = NULL;
static WGPUTextureView       s_scene_view = NULL;
static WGPUTexture           s_depth_tex  = NULL;
static WGPUTextureView       s_depth_view = NULL;
static uint32_t              s_scene_w = 0, s_scene_h = 0;
#define WGPU_DEPTH_FORMAT WGPUTextureFormat_Depth24Plus

/* Output post-FX target: the scene (s_scene_tex) is resolved through the
 * fullscreen output-VI-filter pass into here, and THIS is what gets presented /
 * read back / has the minimap drawn on top — mirroring GL's default-FB composite
 * and Metal's s_final_color. Only allocated/used when the filter is active; a
 * faithful (RemasterFX-off, gamma 1.0) frame keeps the plain scene->surface copy.
 * Same BGRA8 format + size as the scene target. */
static WGPUTexture           s_post_tex   = NULL;
static WGPUTextureView       s_post_view  = NULL;
/* The target the minimap overlay + the present copy + readback read from this
 * frame: s_post_view when the filter ran, else s_scene_view. Set in end_frame. */
static WGPUTextureView       s_present_target_view = NULL;
static WGPUTexture           s_present_target_tex  = NULL;

/* PERF-008: sticky "a frame is or may be read back" latch. Set once at startup for the
 * known env/flag-armed capture paths (see wgpu_readback_possible), and — as a universal
 * safety net — the first time wgpu_read_framebuffer_rgb actually runs, so ANY readback
 * caller (the gfx_pc.c diagnostic pixel probes, or a future one) pins every subsequent
 * frame to the offscreen present path even if its arming signal was not enumerated. */
static bool                  s_readback_latched    = false;

/* Per-frame objects, valid only between start_frame and end_frame. */
static WGPUCommandEncoder    s_encoder    = NULL;
static WGPURenderPassEncoder s_pass       = NULL;
static bool                  s_frame_open = false;

/* The app-shell F1 overlay renders into this render pass on the surface texture,
 * opened by wgpu_end_frame just before present. Non-NULL only during the
 * platformOverlayRender() call; the overlay reads it via the getters below. */
static WGPURenderPassEncoder s_overlay_pass = NULL;
static int s_overlay_w = 0, s_overlay_h = 0;

/* Exposed to the app shell's F1 overlay (ui_overlay.cpp) so it can draw ImGui
 * into the current surface pass via gfx_webgpu_imgui_render. NULL when no overlay
 * pass is open. Declared in gfx_webgpu.h. */
void *gfx_webgpu_current_overlay_pass(void) { return (void *)s_overlay_pass; }
void  gfx_webgpu_current_overlay_size(int *w, int *h) {
    if (w) *w = s_overlay_w;
    if (h) *h = s_overlay_h;
}

/* RDP memory-blend ("glass / chain-link fence" class) snapshot resources.
 * Before each draw whose shader samples the memory color, the open scene pass
 * is split and the scene target is copied here — the WGSL then reads it as the
 * N64 "memory color" (the WebGPU equivalent of gfx_opengl.c's per-batch
 * glCopyTexSubImage2D snapshot, W3.6 fence/glass regression fix). */
static WGPUTexture     s_snap_tex  = NULL;
static WGPUTextureView s_snap_view = NULL;
static uint32_t        s_snap_w = 0, s_snap_h = 0;
static WGPUSampler     s_snap_sampler = NULL;   /* nearest + clamp-to-edge */
/* Small per-frame ring of 16-byte viewport UBOs for the coverage-wrap shader
 * (GL uDiagViewport). Distinct buffers per distinct value are required because
 * wgpuQueueWriteBuffer executes before the command buffer — one buffer would
 * retroactively apply the LAST viewport to every draw. In practice the scene
 * viewport is constant within a frame, so slot 0 is reused. */
#define WGPU_DIAG_UBO_RING 8
static WGPUBuffer s_diag_ubo[WGPU_DIAG_UBO_RING];
static float      s_diag_ubo_val[WGPU_DIAG_UBO_RING][4];
static int        s_diag_ubo_used = 0;   /* slots written this frame (ring + overflow) */
/* WEB-051: overflow buffers for the exotic frame that needs > WGPU_DIAG_UBO_RING
 * distinct viewport values. GROW rather than reuse the last ring slot: because
 * wgpuQueueWriteBuffer runs before the command buffer, reusing an occupied slot
 * would retroactively rewrite an earlier draw's viewport. Created lazily, reused
 * across frames (only s_diag_ubo_used resets); the common path never touches
 * these. */
struct WgpuDiagUbo { WGPUBuffer buf; float val[4]; };
static struct WgpuDiagUbo *s_diag_ubo_ext = NULL;
static int                 s_diag_ubo_ext_cap = 0;

/* Clear color, pushed by gfx_pc.c before start_frame (see gfx_webgpu_set_clear_color). */
static double s_clear_r = 0.0, s_clear_g = 0.0, s_clear_b = 0.0;

/* Draw resources (Task 3): a 1x1 white fallback texture + a nearest sampler for
 * used-but-unuploaded texture slots, and one large vertex buffer bump-allocated
 * per frame (reset in start_frame; consumed by draw_triangles). */
static WGPUTexture     s_white_tex = NULL;
static WGPUTextureView s_white_view = NULL;
static WGPUSampler     s_default_sampler = NULL;
#define WGPU_VBUF_CAP (16u * 1024u * 1024u)
static WGPUBuffer s_vbuf = NULL;
static uint32_t   s_vbuf_off = 0;
/* WEB-023 (residual): CPU-side shadow of the per-frame vertex buffer. Every scene
 * draw (wgpu_draw_triangles) and the minimap overlay memcpy their batch into this
 * shadow at the bump offset instead of issuing a per-batch wgpuQueueWriteBuffer
 * (~100-200 of them a frame — one wasm↔JS crossing + one queue-write command each).
 * The accumulated range [0, s_vbuf_off) is uploaded ONCE in wgpu_end_frame just
 * before submit: queue writes execute before the command buffer (the WEB-053
 * ordering guarantee), so a single pre-submit upload of everything this frame's
 * draws referenced lands exactly what each draw's SetVertexBuffer(voff, bytes)
 * reads — byte-identical to the old per-batch writes, one crossing instead of many.
 * Sized to the same 16MB cap as s_vbuf and allocated in lockstep with it; calloc so
 * the inter-batch alignment padding (bump-skipped, never read) is defined. If the
 * malloc fails the writers fall back to per-batch writeBuffer, so a low-memory host
 * still renders — just without the batching win. Persistent like s_vbuf (never
 * freed; the backend has no teardown that releases s_vbuf). */
static uint8_t   *s_vbuf_shadow = NULL;

/* WEB-027: one small uniform (frame counter + render height) shared by every
 * combiner that reads SHADER_NOISE. Bound at group(0) @binding(7) ONLY for those
 * pipelines (info->uses_noise). Written ONCE per frame (wgpu_update_noise_ubo,
 * from start_frame) — never per draw — so N64 static/fizz animates each frame the
 * way the GL backend's frame_count uniform does. */
static WGPUBuffer s_noise_ubo   = NULL;
static uint32_t   s_noise_frame = 0;

/* WEB-052: modern-mesh (scene decor) uniform ring. One persistent uniform buffer
 * holding a ring of 256-byte-aligned slots (the WebGPU minUniformBufferOffset-
 * Alignment guarantee); each decor draw writes its 96-byte uniform into a FRESH
 * slot and binds it with a dynamic offset, so the per-mesh bind group is cached
 * (in WgpuModernEntry) and reused across frames instead of allocating a UBO + a
 * bind group per draw. The ring is reset per frame (s_modern_ubo_used = 0 in
 * start_frame) and GROWN on overflow — never reusing a slot already written this
 * frame, because wgpuQueueWriteBuffer pre-executes the command buffer (the same
 * retroactive-rewrite hazard the diag ring and WEB-053 guard). Growing recreates
 * the buffer, so cached bind groups carry a generation stamp (bg_gen) and rebuild
 * against the new buffer on first reuse. */
#define WGPU_MODERN_UBO_ALIGN 256u
#define WGPU_MODERN_UBO_INIT  64
static WGPUBuffer s_modern_ubo      = NULL;
static int        s_modern_ubo_cap  = 0;   /* slots in s_modern_ubo */
static int        s_modern_ubo_used = 0;   /* slots consumed this frame */
static uint32_t   s_modern_ubo_gen  = 0;   /* bumped on (re)create; stamps cached bgs */

/* Dynamic depth / viewport / scissor state (Task 4). WebGPU bakes depth into the
 * pipeline, so the depth fields feed the pipeline cache key; viewport/scissor are
 * render-pass encoder state applied per draw. */
static bool     s_depth_test = false, s_depth_update = false, s_depth_compare = false;
static uint16_t s_zmode = 0;
static int s_vp_x = 0, s_vp_y = 0, s_vp_w = 0, s_vp_h = 0;
static int s_sc_x = 0, s_sc_y = 0, s_sc_w = 0, s_sc_h = 0;
static bool s_sc_set = false;

/* depth-clip-control granted at device creation: 3D pipelines set
 * unclippedDepth so far-plane-crossing geometry depth-clamps like GL/Metal
 * (g_depth_clamp_enabled invariance; DAM-R1). */
static bool s_unclipped_depth_supported = false;

/* PERF-005: count of async render-pipeline creations currently in flight. Bumped
 * when wgpu_pipeline_for kicks an async create (web-live only), dropped in the
 * on_pipeline_ready callback. wgpu_end_frame drains the future queue only while
 * this is > 0. Declared unconditionally: on native it stays pinned at 0 (nothing
 * ever increments it — the async kick is #ifdef __EMSCRIPTEN__), so the end-frame
 * drain check is a permanently-dead branch there and native behavior is unchanged. */
static int s_pending_pipelines = 0;

/* PERF-005b: count of draw batches dropped THIS FRAME because their render
 * pipeline is still PENDING (async create in flight). A frame with any such
 * drop is visually incomplete — world geometry is simply missing, which on
 * screen reads as sky/backdrop "bleeding" through walls (the level-entry and
 * first-sight pop-in of PERF-005). wgpu_end_frame uses this to withhold the
 * PRESENT of incomplete frames (hold the last complete image) instead of
 * showing them; see there for the bounded-hold contract. FAILED pipelines do
 * NOT count — a permanently-failed create must present degraded rather than
 * hold forever. Native never stores a PENDING slot, so this stays 0 there. */
static int s_frame_pending_skips = 0;

/* WEB-023-lite: the viewport/scissor rect LAST APPLIED to the current render-pass
 * encoder, so wgpu_draw_triangles can skip re-emitting an unchanged rect (gfx_pc
 * re-sets the same viewport+scissor for every draw in a run — hundreds of
 * redundant Set calls per frame). These track the FINAL (Y-flipped, clamped)
 * values actually handed to the encoder. Render-pass state does NOT carry across
 * passes, so wgpu_reset_pass_dynamic_state() MUST clear the "applied" flags at
 * every pass begin (start_frame + the memory-blend split-resume). */
static bool s_vp_applied = false;
static int  s_vp_ax = 0, s_vp_ay = 0, s_vp_aw = 0, s_vp_ah = 0;
static bool s_sc_applied = false;
static int  s_sc_ax = 0, s_sc_ay = 0, s_sc_aw = 0, s_sc_ah = 0;

/* PERF-014: the render pipeline and group(0) bind group LAST APPLIED to the
 * current render-pass encoder (s_pass), so wgpu_draw_triangles can skip the
 * redundant SetPipeline/SetBindGroup that gfx_pc's per-draw-group material
 * re-setup emits (consecutive draws in a run frequently repeat the same
 * pipeline+bind group; on web each Set is a wasm↔JS crossing, ~100-200/frame).
 * NULL is the "nothing applied yet" sentinel. Like the viewport/scissor trackers
 * these are render-pass encoder state that does NOT carry across passes, so
 * wgpu_reset_pass_dynamic_state() clears them at every s_pass begin — otherwise
 * the first draw of a new pass would wrongly skip a needed SetPipeline. Note:
 * wgpu_draw_modern_mesh also writes s_pass's pipeline/bind group DIRECTLY,
 * interleaved with the triangle draws in the same scene pass and bypassing this
 * dedup, so it updates these trackers to keep the next wgpu_draw_triangles honest
 * (see there). */
static WGPURenderPipeline s_pipe_applied = NULL;
static WGPUBindGroup      s_bg_applied   = NULL;

static void wgpu_reset_pass_dynamic_state(void) {
    s_vp_applied = false;
    s_sc_applied = false;
    s_pipe_applied = NULL;   /* PERF-014: fresh pass = no pipeline bound */
    s_bg_applied   = NULL;   /* PERF-014: fresh pass = no bind group bound */
}

/* ZMODE_DEC decal (gfx_opengl.c / gfx_metal.mm): coplanar decals get a negative
 * polygon offset so they win the depth test against the surface they overlay. */
static bool wgpu_depth_is_decal(void) {
    return s_zmode == 0xc00 && s_depth_test && s_depth_compare;
}

/* ------------------------------------------------------------------------
 * Async request helpers (wgpu-native fires these during processEvents), mirroring
 * the validated spike (tests/test_webgpu_spike.c).
 * ---------------------------------------------------------------------- */
typedef struct { WGPUAdapter adapter; int done; WGPURequestAdapterStatus status; } AdapterReq;
static void on_adapter(WGPURequestAdapterStatus s, WGPUAdapter a, WGPUStringView m, void *u1, void *u2) {
    (void)m; (void)u2; AdapterReq *r = (AdapterReq *)u1; r->status = s; r->adapter = a; r->done = 1;
}
typedef struct { WGPUDevice device; int done; WGPURequestDeviceStatus status; } DeviceReq;
static void on_device(WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView m, void *u1, void *u2) {
    (void)m; (void)u2; DeviceReq *r = (DeviceReq *)u1; r->status = s; r->device = d; r->done = 1;
}

static WGPUStringView wgpu_sv(const char *s) {
    WGPUStringView v; v.data = s; v.length = s ? strlen(s) : 0; return v;
}

/* Log device errors instead of letting wgpu-native's default uncaptured-error
 * handler panic and abort the process — a malformed shader or a bad draw must
 * degrade to a logged, skipped frame, never crash the game. */
static void on_device_error(WGPUDevice const *device, WGPUErrorType type,
                            WGPUStringView msg, void *u1, void *u2) {
    (void)device; (void)u1; (void)u2;
    fprintf(stderr, "[webgpu] device error (type=%d): %.*s\n",
            (int)type, (int)msg.length, msg.data ? msg.data : "");
    fflush(stderr);
}

/* WEB-025: the GPU device was lost (process restart, driver reset, TDR). Latch
 * the lost flag + clear s_ready so every frame path becomes a clean no-op, and
 * surface a human-readable panel to the JS shell instead of a permanently frozen
 * canvas. Reason "Destroyed" is our own teardown releasing the device, NOT a
 * failure — stay silent for it. Full re-init is deliberately out of scope. */
static void on_device_lost(WGPUDevice const *device, WGPUDeviceLostReason reason,
                           WGPUStringView msg, void *u1, void *u2) {
    (void)device; (void)u1; (void)u2;
    if (reason == WGPUDeviceLostReason_Destroyed) {
        return;
    }
    fprintf(stderr, "[webgpu] device lost (reason=%d): %.*s\n",
            (int)reason, (int)msg.length, msg.data ? msg.data : "");
    fflush(stderr);
    s_device_lost = true;
    s_ready = false;
    WGPU_COMPAT_REPORT_FAILURE(
        "The graphics device was lost — reload the page to continue from your "
        "last auto-save.");
}

/* ------------------------------------------------------------------------
 * Surface creation (platform-specific window -> WGPUSurface) — the dialect seam.
 *
 * One of only two inline `#ifdef __EMSCRIPTEN__` sites in this file (seam rule,
 * Task W7): the wgpu-native surface-source structs (MetalLayer / Win32 / X11 /
 * Wayland) exist only in wgpu-native's webgpu.h, so they must stay on the
 * native side of this fork; the browser builds a canvas-selector surface.
 * (The other is PERF-005's async-pipeline block at wgpu_pipeline_for /
 * on_pipeline_ready: it must be absent from the native TU so native stays
 * byte-for-byte HEAD, and the async create/callback API has no native test
 * coverage. Its per-frame completion drain lives in the compat seam as
 * WGPU_COMPAT_DRAIN.)
 *
 * Native path is parameterized by the platform handle so BOTH the engine
 * (standalone) and the app shell (AppHost, which owns the window/layer before
 * the game adopts it) create surfaces the same way. macOS uses `metal_layer`
 * (a CAMetalLayer); every other native platform uses `window` (resolved to
 * HWND/X11/Wayland by platformWebGpuWindowInfo). Browser ignores both handles
 * (the page owns exactly one canvas). Declared in gfx_webgpu_compat.h.
 *
 * Verified against the emdawnwebgpu port's webgpu.h at build time; keep
 * "#canvas" in sync with web/index.html (W5).
 * ---------------------------------------------------------------------- */
WGPUSurface wgpuCompatCreateSurface(WGPUInstance instance, void *metal_layer,
                                    struct SDL_Window *window) {
    if (instance == NULL) {
        return NULL;
    }
#ifdef __EMSCRIPTEN__
    (void)metal_layer;
    (void)window;   /* the page owns exactly one canvas */
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas = {0};
    canvas.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvas.selector = (WGPUStringView){ "#canvas", WGPU_STRLEN };
    WGPUSurfaceDescriptor desc = {0};
    desc.nextInChain = &canvas.chain;
    return wgpuInstanceCreateSurface(instance, &desc);
#else
    WGPUSurfaceDescriptor sd = {0};
    sd.label = wgpu_sv("mgb64-surface");
#ifdef __APPLE__
    (void)window;
    if (metal_layer == NULL) {
        fprintf(stderr, "[webgpu] no CAMetalLayer — cannot create surface\n");
        return NULL;
    }
    WGPUSurfaceSourceMetalLayer ml = {0};
    ml.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    ml.layer = metal_layer;
    sd.nextInChain = (WGPUChainedStruct *)&ml;
    return wgpuInstanceCreateSurface(instance, &sd);
#else
    (void)metal_layer;
    void *h1 = NULL, *h2 = NULL;
    unsigned long long win = 0;
    int sys = platformWebGpuWindowInfo((void *)window, &h1, &h2, &win);
    if (sys == 2) {   /* Win32 */
        WGPUSurfaceSourceWindowsHWND w = {0};
        w.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        w.hinstance = h2;
        w.hwnd = h1;
        sd.nextInChain = (WGPUChainedStruct *)&w;
        return wgpuInstanceCreateSurface(instance, &sd);
    } else if (sys == 3) {   /* X11 */
        WGPUSurfaceSourceXlibWindow x = {0};
        x.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        x.display = h1;
        x.window = (uint64_t)win;
        sd.nextInChain = (WGPUChainedStruct *)&x;
        return wgpuInstanceCreateSurface(instance, &sd);
    } else if (sys == 4) {   /* Wayland */
        WGPUSurfaceSourceWaylandSurface wl = {0};
        wl.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
        wl.display = h1;
        wl.surface = h2;
        sd.nextInChain = (WGPUChainedStruct *)&wl;
        return wgpuInstanceCreateSurface(instance, &sd);
    }
    fprintf(stderr, "[webgpu] unsupported windowing system (tag=%d) — no surface\n", sys);
    return NULL;
#endif  /* __APPLE__ */
#endif  /* __EMSCRIPTEN__ */
}

/* Pick the swapchain format. WEB-049: the browser takes caps.formats[0] — the
 * platform's own preferred canvas format, in preference order — so an Android
 * GPU that prefers RGBA8 is not forced through a per-present BGRA8 swizzle.
 * Native keeps the long-standing BGRA8-preferring scan (the dialect flag lives
 * in gfx_webgpu_compat.h, so this file stays free of inline __EMSCRIPTEN__): the
 * offscreen scene target adopts s_surface_format and the readback swizzle keys
 * off it, so keeping the native choice pinned to BGRA8 keeps every recorded
 * baseline byte-identical (Metal already advertises BGRA8 first). Both dialects
 * fall back to BGRA8 only when the surface advertises no formats at all.
 * Parameterized so the shared bring-up helper can use it before the
 * s_surface/s_adapter statics are assigned. */
static WGPUTextureFormat wgpu_choose_format(WGPUSurface surface, WGPUAdapter adapter) {
    WGPUSurfaceCapabilities caps = {0};
    if (wgpuSurfaceGetCapabilities(surface, adapter, &caps) != WGPUStatus_Success ||
        caps.formatCount == 0) {
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        return WGPUTextureFormat_BGRA8Unorm;   /* safe default (formatCount==0) */
    }
    WGPUTextureFormat chosen = caps.formats[0];   /* platform-preferred */
    if (!WGPU_COMPAT_PREFER_FIRST_SURFACE_FORMAT) {
        for (size_t i = 0; i < caps.formatCount; ++i) {
            if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm) {
                chosen = WGPUTextureFormat_BGRA8Unorm;
                break;
            }
        }
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    return chosen;
}

/* WEB-049: choose the surface composite-alpha mode. Prefer an explicit Opaque
 * over Auto — on the browser Auto can resolve to premultiplied alpha, which
 * bleeds the page through wherever a frame's alpha carries sub-1 coverage (the
 * fence/glass RDP memory-blend surfaces). Opaque tells the compositor to ignore
 * frame alpha. Fall back to Auto only when the surface does not advertise Opaque
 * (Auto is guaranteed valid). Cached: the capability query runs once. Uses the
 * s_surface/s_adapter statics, so it is called from wgpu_configure_surface after
 * bring-up has assigned them (both the standalone and host-handoff paths do). */
static WGPUCompositeAlphaMode wgpu_choose_alpha_mode(void) {
    static int resolved = 0;
    static WGPUCompositeAlphaMode mode = WGPUCompositeAlphaMode_Auto;
    if (resolved) {
        return mode;
    }
    resolved = 1;
    if (s_surface == NULL || s_adapter == NULL) {
        return mode;   /* Auto — caps unavailable */
    }
    WGPUSurfaceCapabilities caps = {0};
    if (wgpuSurfaceGetCapabilities(s_surface, s_adapter, &caps) == WGPUStatus_Success) {
        for (size_t i = 0; i < caps.alphaModeCount; ++i) {
            if (caps.alphaModes[i] == WGPUCompositeAlphaMode_Opaque) {
                mode = WGPUCompositeAlphaMode_Opaque;
                break;
            }
        }
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    return mode;
}

/* PERF-051: present-mode selection. FIFO (vsync) is the default and the
 * byte-identity baseline — it matches the GL/Metal swap and is the only mode the
 * WebGPU spec guarantees the surface advertises. GE007_WEBGPU_PRESENT opts into a
 * lower-latency mode (the F5 uncapped-FPS prerequisite: render-only frames cannot
 * exceed the refresh rate under FIFO):
 *     fifo (default) | mailbox | immediate
 * The requested mode is selected ONLY when the surface capabilities advertise it;
 * otherwise it falls back to FIFO with one stderr note. Latched once — the env is
 * process-constant, and this runs from wgpu_configure_surface (called every resize).
 *
 * Web: the browser present is rAF-driven (emdawnwebgpu no-ops the present, see
 * gfx_webgpu_compat.h), so present mode is a native concern; the knob is harmless
 * there by construction — an unset env keeps FIFO (byte-identical to today), and the
 * env is never set on the web build. No inline __EMSCRIPTEN__ guard needed (seam
 * rule): the default path is identical on both platforms. */
static WGPUPresentMode wgpu_choose_present_mode(void) {
    static int resolved = 0;
    static WGPUPresentMode mode = WGPUPresentMode_Fifo;
    if (resolved) {
        return mode;
    }
    resolved = 1;
    const char *want = getenv("GE007_WEBGPU_PRESENT");
    if (want == NULL || *want == '\0' || strcmp(want, "fifo") == 0) {
        return mode;   /* default / explicit FIFO — the byte-identity baseline */
    }
    WGPUPresentMode requested;
    if (strcmp(want, "mailbox") == 0) {
        requested = WGPUPresentMode_Mailbox;
    } else if (strcmp(want, "immediate") == 0) {
        requested = WGPUPresentMode_Immediate;
    } else {
        fprintf(stderr, "[webgpu] GE007_WEBGPU_PRESENT='%s' unrecognized "
                        "(fifo|mailbox|immediate); using fifo\n", want);
        return mode;
    }
    /* Select only if the surface advertises it — a non-advertised presentMode is a
     * wgpuSurfaceConfigure validation error. Caps unavailable ⇒ keep FIFO. */
    if (s_surface == NULL || s_adapter == NULL) {
        return mode;
    }
    WGPUSurfaceCapabilities caps = {0};
    bool advertised = false;
    if (wgpuSurfaceGetCapabilities(s_surface, s_adapter, &caps) == WGPUStatus_Success) {
        for (size_t i = 0; i < caps.presentModeCount; ++i) {
            if (caps.presentModes[i] == requested) {
                advertised = true;
                break;
            }
        }
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    if (advertised) {
        mode = requested;
    } else {
        fprintf(stderr, "[webgpu] present mode '%s' not advertised by the surface; "
                        "using fifo\n", want);
    }
    return mode;
}

/* Whether an 8-bit color target stores B,G,R,A (vs R,G,B,A) — so the readback
 * paths extract RGB correctly. BGRA8 is what wgpu_choose_format prefers and what
 * every current target advertises, but a platform that only offers RGBA8Unorm
 * would otherwise get R/B-swapped screenshots. */
static bool wgpu_format_is_bgra(WGPUTextureFormat f) {
    return f == WGPUTextureFormat_BGRA8Unorm || f == WGPUTextureFormat_BGRA8UnormSrgb;
}

static int wgpu_dump_surface_frame(void);   /* GE007_WEBGPU_DUMP_SURFACE target, or -1 */

static void wgpu_configure_surface(uint32_t w, uint32_t h) {
    if (s_surface == NULL || s_device == NULL || w == 0 || h == 0) {
        return;
    }
    WGPUSurfaceConfiguration cfg = {0};
    cfg.device = s_device;
    cfg.format = s_surface_format;
    /* The scene is rendered offscreen and copied here at present, so the surface
     * only needs to be a copy destination (plus RenderAttachment, which surfaces
     * require). If a platform disallows CopyDst, wgpuSurfaceConfigure raises a
     * device error that on_device_error logs (never aborts) and present is
     * skipped — offscreen rendering + readback still work. */
    /* CopyDst receives the scene blit on the offscreen present path (always kept, as
     * any frame may take it). CopySrc lets GE007_WEBGPU_DUMP_SURFACE read the presented
     * frame (scene + overlay) back — requested ONLY when that dump is armed (PERF-008):
     * a permanently CopySrc surface can block a compositor fast-path on some platforms. */
    cfg.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst;
    if (wgpu_dump_surface_frame() >= 0) {
        cfg.usage |= WGPUTextureUsage_CopySrc;
    }
    cfg.width = w;
    cfg.height = h;
    cfg.alphaMode = wgpu_choose_alpha_mode();   /* WEB-049: Opaque when advertised, else Auto */
    cfg.presentMode = wgpu_choose_present_mode();   /* PERF-051: FIFO default (vsync), GE007_WEBGPU_PRESENT opt-in */
    wgpuSurfaceConfigure(s_surface, &cfg);
    s_cfg_w = w;
    s_cfg_h = h;
}

/* Full WebGPU bring-up for a native window handle: instance -> surface ->
 * adapter -> device -> queue -> surface format, mirroring the validated spike
 * (tests/test_webgpu_spike.c). On success returns true with every out-param set
 * (the caller owns the returned objects); on any failure returns false, the
 * out-params untouched, and the caller treats the backend as inert. Shared by
 * the engine's own wgpu_init AND the app shell (AppHost, via gfx_webgpu.h) so
 * the request sequence + error callback live in exactly one place. */
bool gfx_webgpu_bringup(void *metal_layer, void *sdl_window,
                        WGPUInstance *out_instance, WGPUAdapter *out_adapter,
                        WGPUDevice *out_device, WGPUQueue *out_queue,
                        WGPUSurface *out_surface, int *out_format) {
    /* WEB-026: acquire in order (instance -> surface -> adapter), tracking each
     * handle so any failure path releases what it acquired (goto fail) instead
     * of leaking. All three start NULL so a NULL-guarded cleanup is safe from
     * every early exit. */
    WGPUInstance instance = NULL;
    WGPUSurface  surface  = NULL;
    WGPUAdapter  adapter  = NULL;

    instance = wgpuCreateInstance(NULL);
    if (instance == NULL) {
        fprintf(stderr, "[webgpu] wgpuCreateInstance failed\n");
        return false;   /* nothing acquired yet */
    }

    /* Surface first, so it can be passed as the adapter's compatibleSurface.
     * Routed through the dialect seam (native window vs browser canvas). */
    surface = wgpuCompatCreateSurface(instance, metal_layer, sdl_window);
    if (surface == NULL) {
        fprintf(stderr, "[webgpu] surface creation failed — backend inert\n");
        goto fail;
    }

    /* WEB-026: the request-result structs are STATIC, not stack locals. On a
     * timed-out bring-up the WGPU_COMPAT_WAIT loop returns while the request is
     * still pending; when the callback finally resolves it writes its result
     * through the userdata pointer. A stack local would be a dead frame by then
     * (silent wasm-stack corruption on exactly the slow machines where bring-up
     * times out); a file-scope static is always a live, harmless landing site.
     * Reset before each use. Bring-up runs once per process (single caller), so
     * the shared statics are never re-entered. */
    static AdapterReq areq;
    areq = (AdapterReq){0};
    WGPURequestAdapterCallbackInfo acb = {0};
    acb.mode = WGPUCallbackMode_AllowProcessEvents;
    acb.callback = on_adapter;
    acb.userdata1 = &areq;
    WGPURequestAdapterOptions aopts = {0};
    aopts.compatibleSurface = surface;
    aopts.powerPreference = WGPUPowerPreference_HighPerformance;
    wgpuInstanceRequestAdapter(instance, &aopts, acb);
    WGPU_COMPAT_WAIT(areq.done, instance, NULL, WGPU_COMPAT_BRINGUP_WAIT_ITERS);
    if (!areq.done || areq.status != WGPURequestAdapterStatus_Success || areq.adapter == NULL) {
        fprintf(stderr, "[webgpu] adapter request failed (status=%d)\n", (int)areq.status);
        goto fail;
    }
    adapter = areq.adapter;

    WGPUAdapterInfo info = {0};
    wgpuAdapterGetInfo(adapter, &info);
    fprintf(stderr, "[webgpu] adapter backend=%d device=%.*s\n",
            (int)info.backendType, (int)info.device.length,
            info.device.data ? info.device.data : "");
    wgpuAdapterInfoFreeMembers(info);

    static DeviceReq dreq;   /* WEB-026: static — see the adapter-request note above. */
    WGPURequestDeviceCallbackInfo dcb = {0};
    dcb.mode = WGPUCallbackMode_AllowProcessEvents;
    dcb.callback = on_device;
    dcb.userdata1 = &dreq;
    /* WEB-015: raise maxTextureDimension2D from WebGPU's 8192 default to the
     * adapter's real maximum. Query the adapter, ask the device for up to that
     * in requiredLimits (all other fields stay UNDEFINED = library defaults), so
     * large viewports at the default RenderScale 2 stop hitting the 8192 clamp.
     * WGPU_LIMITS_INIT leaves every field UNDEFINED; a {0}-init would instead
     * REQUIRE 0 for every limit and fail device creation. */
    WGPULimits adapter_limits = WGPU_LIMITS_INIT;
    uint32_t want_max_tex = 8192;
    if (wgpuAdapterGetLimits(adapter, &adapter_limits) == WGPUStatus_Success &&
        adapter_limits.maxTextureDimension2D > want_max_tex) {
        want_max_tex = adapter_limits.maxTextureDimension2D;
    }
    WGPULimits required_limits = WGPU_LIMITS_INIT;
    required_limits.maxTextureDimension2D = want_max_tex;

    WGPUDeviceDescriptor ddesc = {0};
    ddesc.label = wgpu_sv("mgb64-device");
    ddesc.requiredLimits = &required_limits;
    /* DAM-R1 root cause (DAM_PARITY_DEEP_DIVE 2026-07-17 §4.1): gfx_init sets
     * g_depth_clamp_enabled=true for this backend (sim-hash invariance — the CPU
     * clipper then passes far-plane-crossing triangles through, exactly like the
     * GL/Metal depth-clamp paths), but WebGPU's DEFAULT primitive state clips
     * depth — so distant horizon geometry silently vanished (sky slivers over
     * the Dam cliffs; any far terrain on any level). Make the claim honest:
     * request depth-clip-control and set unclippedDepth on the 3D pipelines. */
    WGPUFeatureName required_features[1];
    s_unclipped_depth_supported = wgpuAdapterHasFeature(adapter, WGPUFeatureName_DepthClipControl) != 0;
    if (s_unclipped_depth_supported) {
        required_features[0] = WGPUFeatureName_DepthClipControl;
        ddesc.requiredFeatures = required_features;
        ddesc.requiredFeatureCount = 1;
    } else {
        fprintf(stderr,
                "[webgpu] adapter lacks depth-clip-control: far-plane-crossing "
                "geometry will be CLIPPED (GL depth-clamps it) — expect missing "
                "far terrain (DAM-R1 class)\n");
    }
    ddesc.uncapturedErrorCallbackInfo.callback = on_device_error;
    /* WEB-025: register the device-lost callback at creation so a later GPU loss
     * (process restart / driver reset) surfaces a reload panel instead of a
     * frozen canvas. AllowSpontaneous: it may fire at any time, not only inside
     * a ProcessEvents pump. */
    ddesc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    ddesc.deviceLostCallbackInfo.callback = on_device_lost;
    dreq = (DeviceReq){0};
    wgpuAdapterRequestDevice(adapter, &ddesc, dcb);
    WGPU_COMPAT_WAIT(dreq.done, instance, NULL, WGPU_COMPAT_BRINGUP_WAIT_ITERS);
    if (!dreq.done || dreq.status != WGPURequestDeviceStatus_Success || dreq.device == NULL) {
        /* WEB-015/WEB-026: a blocklisted or limited adapter can reject the raised
         * maxTextureDimension2D. Retry once with library-default limits so device
         * creation still succeeds (at the 8192 default cap; s_max_tex_dim stays
         * at its floor). Covers native wgpu-native and web alike. */
        fprintf(stderr, "[webgpu] device request failed (status=%d); retrying with default limits\n",
                (int)dreq.status);
        ddesc.requiredLimits = NULL;
        dreq = (DeviceReq){0};
        wgpuAdapterRequestDevice(adapter, &ddesc, dcb);
        WGPU_COMPAT_WAIT(dreq.done, instance, NULL, WGPU_COMPAT_BRINGUP_WAIT_ITERS);
        if (!dreq.done || dreq.status != WGPURequestDeviceStatus_Success || dreq.device == NULL) {
            fprintf(stderr, "[webgpu] device request failed after default-limits retry (status=%d)\n",
                    (int)dreq.status);
            goto fail;
        }
    }
    WGPUDevice device = dreq.device;

    /* WEB-015: record the max texture dimension the device actually granted, so
     * gfx_webgpu_max_offscreen_dim() and the upload reject use the true cap. */
    {
        WGPULimits granted = WGPU_LIMITS_INIT;
        if (wgpuDeviceGetLimits(device, &granted) == WGPUStatus_Success &&
            granted.maxTextureDimension2D >= 8192) {
            s_max_tex_dim = granted.maxTextureDimension2D;
        }
        fprintf(stderr, "[webgpu] maxTextureDimension2D=%u (default 8192)\n",
                (unsigned)s_max_tex_dim);
    }

    *out_instance = instance;
    *out_adapter  = adapter;
    *out_device   = device;
    *out_queue    = wgpuDeviceGetQueue(device);
    *out_surface  = surface;
    *out_format   = (int)wgpu_choose_format(surface, adapter);
    return true;

    /* WEB-026: release everything acquired so far on any failure (the P3 leak
     * fold-in). NULL-guarded, so it is safe from every early exit above. */
fail:
    if (adapter  != NULL) wgpuAdapterRelease(adapter);
    if (surface  != NULL) wgpuSurfaceRelease(surface);
    if (instance != NULL) wgpuInstanceRelease(instance);
    return false;
}

/* ------------------------------------------------------------------------
 * Vtable: init / resize / frame lifecycle
 * ---------------------------------------------------------------------- */
static void wgpu_init(void) {
    s_ready = false;

    /* App-shell handoff: the launcher already stood up the WebGPU device and
     * surface for its own UI; adopt them wholesale so the game and launcher
     * share one device/surface (no second present target, no ownership war).
     * We do NOT own these objects — teardown must leave them to the shell. */
    if (platformHasHostWebGpu()) {
        s_instance       = (WGPUInstance)platformHostWgpuInstance();
        s_adapter        = (WGPUAdapter)platformHostWgpuAdapter();
        s_device         = (WGPUDevice)platformHostWgpuDevice();
        s_queue          = (WGPUQueue)platformHostWgpuQueue();
        s_surface        = (WGPUSurface)platformHostWgpuSurface();
        s_surface_format = (WGPUTextureFormat)platformHostWgpuSurfaceFormat();
        s_owns_device    = false;
        if (s_device == NULL || s_queue == NULL || s_surface == NULL) {
            fprintf(stderr, "[webgpu] host handoff incomplete — backend inert\n");
            WGPU_COMPAT_REPORT_FAILURE(
                "The graphics device could not be started (incomplete handoff). "
                "Reload the page to try again.");
            return;
        }
        s_ready = true;
        fprintf(stderr, "[webgpu] adopted host device/surface (format=%d)\n",
                (int)s_surface_format);
        return;
    }

    s_owns_device = true;
    void *layer = NULL;
#ifdef __APPLE__
    layer = platformGetMetalLayer();
#endif
    int fmt = 0;
    if (!gfx_webgpu_bringup(layer, platformGetSdlWindow(),
                            &s_instance, &s_adapter, &s_device, &s_queue,
                            &s_surface, &fmt)) {
        /* helper logged the specific failure; backend stays inert. Surface a
         * human-readable message to the JS shell (WEB-003) so the user isn't
         * left staring at a permanently black canvas. */
        WGPU_COMPAT_REPORT_FAILURE(
            "Your browser exposes WebGPU but no usable GPU device could be "
            "created (it may be blocklisted, disabled, or unsupported). "
            "The game can't render here.");
        return;
    }
    s_surface_format = (WGPUTextureFormat)fmt;
    s_ready = true;
    fprintf(stderr, "[webgpu] backend initialized (surface format=%d)\n", (int)s_surface_format);
}

static void wgpu_on_resize(void) {
    /* Force a reconfigure on the next start_frame; the actual size is read from
     * gfx_current_dimensions there (mirrors the Metal backend's lazy target
     * re-creation). */
    s_cfg_w = 0;
    s_cfg_h = 0;
}

/* WEB-027: create (once) + refresh (once per frame) the noise uniform. Carries
 * {frame_count, window_height} for the SHADER_NOISE hash. Written from
 * start_frame so it is updated exactly once per frame, not per draw. */
static void wgpu_update_noise_ubo(void) {
    if (!s_ready) {
        return;
    }
    if (s_noise_ubo == NULL) {
        WGPUBufferDescriptor bd = {0};
        bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        bd.size = 16;
        s_noise_ubo = wgpuDeviceCreateBuffer(s_device, &bd);
        if (s_noise_ubo == NULL) {
            return;   /* alloc failure: noise draws degrade (bind group build skips) */
        }
    }
    s_noise_frame++;
    /* frame counter + render height (GL uploads current_height; here s_scene_h is
     * the render resolution the fragment coords are computed against). */
    float params[4] = { (float)s_noise_frame, (float)s_scene_h, 0.0f, 0.0f };
    wgpuQueueWriteBuffer(s_queue, s_noise_ubo, 0, params, sizeof(params));
}

static void wgpu_start_frame(void) {
    s_frame_open = false;
    s_vbuf_off = 0;   /* reset the per-frame vertex bump allocator */
    s_sc_set = false; /* scissor is re-established by gfx_pc each frame */
    s_diag_ubo_used = 0; /* reset the per-frame viewport-UBO ring */
    s_modern_ubo_used = 0; /* WEB-052: reset the per-frame modern-mesh UBO ring */
    wgpu_reset_pass_dynamic_state(); /* WEB-023-lite: fresh pass = no rect applied yet */
    if (!s_ready) {
        return;
    }

    /* Render at the frontend's resolution (gfx_current_dimensions) — the same
     * resolution the viewports and T&L are computed against, exactly like the
     * Metal backend. */
    uint32_t req_w = gfx_current_dimensions.width;
    uint32_t req_h = gfx_current_dimensions.height;
    if (req_w == 0 || req_h == 0) {
        return;   /* dimensions not established yet — skip this frame cleanly */
    }

    /* PERF-020: debounce window-drag resizes. A native window drag changes
     * gfx_current_dimensions every frame, and each change reconfigured the surface
     * AND destroyed+recreated the three full-res offscreen targets (scene, depth,
     * post) — pure churn for a size that is about to change again next frame.
     * Instead, hold the current committed size until the requested size has been
     * stable for WGPU_RESIZE_STABLE_FRAMES consecutive frames, then apply it once.
     *
     * Why this variant cannot produce a visible error/black frame: the surface and
     * all three offscreen targets are ALWAYS the same committed size (they only ever
     * change together, atomically, when we commit) — so the end-frame present copy's
     * extent {s_scene_w, s_scene_h} always exactly matches both the source target and
     * the surface destination, and can never over-run. While the window outgrows the
     * held surface, wgpuSurfaceGetCurrentTexture returns SuccessSuboptimal (already
     * accepted as present_ok) and the compositor scales — no error, no black frame.
     * The transient during an active drag is at most a briefly-scaled / viewport-
     * clamped image for a few frames, which snaps crisp the instant the size settles.
     *
     * Cannot wedge: this runs every frame, and any size held for STABLE_FRAMES
     * commits — so the final size after a drag (held indefinitely once the mouse is
     * released) always applies. The first size ever seen (s_cfg_w == 0) and any size
     * already live (req == committed) apply immediately, so a fixed-resolution run
     * (every headless gate) never debounces and stays byte-identical.
     *
     * Web note: the browser canvas resizes are rare and ResizeObserver-driven (not a
     * per-frame drag), so in practice this only affects native drags; the mechanism is
     * identical and harmless on web (a rare canvas resize applies within a few frames). */
    #define WGPU_RESIZE_STABLE_FRAMES 3
    uint32_t rw, rh;
    if (s_cfg_w == 0 || s_cfg_h == 0 || s_scene_w == 0 || s_scene_h == 0 ||
        (req_w == s_cfg_w && req_h == s_cfg_h)) {
        /* Initial bring-up, or the requested size is already live: apply now. */
        rw = req_w; rh = req_h;
        s_resize_pending_w = req_w; s_resize_pending_h = req_h;
        s_resize_stable = 0;
    } else {
        /* Requested size differs from the committed one: debounce. */
        if (req_w == s_resize_pending_w && req_h == s_resize_pending_h) {
            s_resize_stable++;
        } else {
            s_resize_pending_w = req_w; s_resize_pending_h = req_h;
            s_resize_stable = 1;
        }
        if (s_resize_stable >= WGPU_RESIZE_STABLE_FRAMES) {
            rw = req_w; rh = req_h;   /* stable long enough — commit the new size */
            s_resize_stable = 0;
        } else {
            rw = s_cfg_w; rh = s_cfg_h;   /* keep rendering at the committed size */
        }
    }

    if (rw != s_cfg_w || rh != s_cfg_h) {
        wgpu_configure_surface(rw, rh);
    }
    /* Reset the SSAO scene-projection coefficients each frame (mirrors
     * gfx_opengl.c:4182 and gfx_metal.mm:2036): the largest-far projection seen
     * during this frame's draws (gfx_pc.c:16128) wins. Without the reset the
     * `proj_b != 0` SSAO gate would latch on forever and menu/HUD frames that set
     * no projection would still get AO. proj_a is set in lockstep with proj_b, so
     * only proj_b/x/y are cleared — exactly as GL/Metal do. */
    g_pc_ssao_proj_b = 0.0f;
    g_pc_ssao_proj_x = 0.0f;
    g_pc_ssao_proj_y = 0.0f;
    /* (Re)create the offscreen scene target + depth buffer at the render res. */
    if (s_scene_view == NULL || s_scene_w != rw || s_scene_h != rh) {
        if (s_scene_view != NULL) { wgpuTextureViewRelease(s_scene_view); s_scene_view = NULL; }
        if (s_scene_tex != NULL)  { wgpuTextureRelease(s_scene_tex);      s_scene_tex = NULL; }
        if (s_depth_view != NULL) { wgpuTextureViewRelease(s_depth_view); s_depth_view = NULL; }
        if (s_depth_tex != NULL)  { wgpuTextureRelease(s_depth_tex);      s_depth_tex = NULL; }
        WGPUTextureDescriptor td = {0};
        td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
                   WGPUTextureUsage_TextureBinding;
        td.dimension = WGPUTextureDimension_2D;
        td.size.width = rw; td.size.height = rh; td.size.depthOrArrayLayers = 1;
        td.format = s_surface_format;   /* BGRA8 — matches the surface for the present copy */
        td.mipLevelCount = 1; td.sampleCount = 1;
        s_scene_tex = wgpuDeviceCreateTexture(s_device, &td);
        s_scene_view = s_scene_tex ? wgpuTextureCreateView(s_scene_tex, NULL) : NULL;

        WGPUTextureDescriptor dd = {0};
        /* TextureBinding: the SSAO post-FX pass samples this depth target as a
         * texture_depth_2d (default-off; inert when Video.Ssao=0). Adding the usage
         * flag does not change any rendered pixel — the scene pass output is
         * unaffected, so faithful frames stay byte-identical. */
        dd.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        dd.dimension = WGPUTextureDimension_2D;
        dd.size.width = rw; dd.size.height = rh; dd.size.depthOrArrayLayers = 1;
        dd.format = WGPU_DEPTH_FORMAT;
        dd.mipLevelCount = 1; dd.sampleCount = 1;
        s_depth_tex = wgpuDeviceCreateTexture(s_device, &dd);
        s_depth_view = s_depth_tex ? wgpuTextureCreateView(s_depth_tex, NULL) : NULL;

        /* Output post-FX target (same format/size as the scene). RenderAttachment
         * so the filter pass writes it; CopySrc so present/readback/dump copy it. */
        if (s_post_view != NULL) { wgpuTextureViewRelease(s_post_view); s_post_view = NULL; }
        if (s_post_tex != NULL)  { wgpuTextureRelease(s_post_tex);      s_post_tex = NULL; }
        WGPUTextureDescriptor pt = {0};
        pt.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
                   WGPUTextureUsage_TextureBinding;
        pt.dimension = WGPUTextureDimension_2D;
        pt.size.width = rw; pt.size.height = rh; pt.size.depthOrArrayLayers = 1;
        pt.format = s_surface_format;
        pt.mipLevelCount = 1; pt.sampleCount = 1;
        s_post_tex = wgpuDeviceCreateTexture(s_device, &pt);
        s_post_view = s_post_tex ? wgpuTextureCreateView(s_post_tex, NULL) : NULL;

        s_scene_w = rw; s_scene_h = rh;
    }
    if (s_scene_view == NULL || s_depth_view == NULL) {
        return;
    }

    /* WEB-027: advance + upload the per-frame noise uniform now that s_scene_h is
     * established, before any draw builds a bind group that references it. */
    wgpu_update_noise_ubo();

    s_encoder = wgpuDeviceCreateCommandEncoder(s_device, NULL);

    WGPURenderPassColorAttachment att = {0};
    att.view = s_scene_view;
    att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;   /* required for a 2D color target */
    att.loadOp = WGPULoadOp_Clear;
    att.storeOp = WGPUStoreOp_Store;
    att.clearValue.r = s_clear_r;
    att.clearValue.g = s_clear_g;
    att.clearValue.b = s_clear_b;
    att.clearValue.a = 1.0;
    WGPURenderPassDepthStencilAttachment depth = {0};
    depth.view = s_depth_view;
    depth.depthLoadOp = WGPULoadOp_Clear;
    depth.depthStoreOp = WGPUStoreOp_Store;
    depth.depthClearValue = 1.0f;   /* 1.0 = far, with WebGPU's 0..1 clip (0 = near) */

    WGPURenderPassDescriptor rp = {0};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &att;
    rp.depthStencilAttachment = &depth;
    s_pass = wgpuCommandEncoderBeginRenderPass(s_encoder, &rp);
    s_frame_open = true;
}

typedef struct { int done; WGPUMapAsyncStatus status; } WgpuMapReq;
static void on_map(WGPUMapAsyncStatus s, WGPUStringView m, void *u1, void *u2) {
    (void)m; (void)u2; WgpuMapReq *r = (WgpuMapReq *)u1; r->status = s; r->done = 1;
}

/* GE007_WEBGPU_DUMP_FRAME=<n> writes presented frame n to a PPM (debug/validation
 * seed for the Task 5 readback). Returns the target frame, or -1 if unset. */
static int wgpu_dump_target_frame(void) {
    static int cached = -2;
    if (cached == -2) {
        const char *e = getenv("GE007_WEBGPU_DUMP_FRAME");
        cached = e ? atoi(e) : -1;
    }
    return cached;
}

/* GE007_WEBGPU_DUMP_SURFACE=<n> writes the PRESENTED surface (scene + F1 overlay)
 * for frame n to a PPM — the scene dump above is overlay-free (pre-blit). */
static int wgpu_dump_surface_frame(void) {
    static int cached = -2;
    if (cached == -2) {
        const char *e = getenv("GE007_WEBGPU_DUMP_SURFACE");
        cached = e ? atoi(e) : -1;
    }
    return cached;
}

/* Map `buf` (bytesPerRow=bpr, BGRA8) and write a w*h RGB PPM to `path`. */
static void wgpu_write_ppm(WGPUBuffer buf, uint32_t bpr, uint32_t w, uint32_t h, const char *path) {
    size_t size = (size_t)bpr * h;
    /* WEB-026: static so a timed-out map's late-resolving callback lands on live
     * storage, not a dead stack frame. Single-threaded + synchronous, so no two
     * maps are ever in flight; reset per call. */
    static WgpuMapReq mr;
    mr = (WgpuMapReq){0};
    WGPUBufferMapCallbackInfo ci = {0};
    ci.mode = WGPUCallbackMode_AllowProcessEvents;
    ci.callback = on_map;
    ci.userdata1 = &mr;
    wgpuBufferMapAsync(buf, WGPUMapMode_Read, 0, size, ci);
    /* WEB-004: pass s_instance (not NULL) so the browser pump can drive
     * wgpuInstanceProcessEvents — the mapAsync callback ONLY fires during
     * ProcessEvents, so a NULL instance froze the tab for minutes on web. On
     * native the WAIT macro prefers the (non-NULL) device and calls
     * wgpuDevicePoll exactly as before — byte-identical. */
    WGPU_COMPAT_WAIT(mr.done, s_instance, s_device, 100000);
    if (!mr.done || mr.status != WGPUMapAsyncStatus_Success) {
        fprintf(stderr, "[webgpu] frame dump map failed (status=%d)\n", (int)mr.status);
        return;
    }
    const uint8_t *px = (const uint8_t *)wgpuBufferGetConstMappedRange(buf, 0, size);
    const bool bgra = wgpu_format_is_bgra(s_surface_format);
    FILE *f = px ? fopen(path, "wb") : NULL;
    if (f != NULL) {
        fprintf(f, "P6\n%u %u\n255\n", w, h);
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t *row = px + (size_t)y * bpr;
            for (uint32_t x = 0; x < w; x++) {
                const uint8_t *p = row + (size_t)x * 4;   /* BGRA8 (or RGBA8) */
                uint8_t rgb[3] = { bgra ? p[2] : p[0], p[1], bgra ? p[0] : p[2] };
                fwrite(rgb, 1, 3, f);
            }
        }
        fclose(f);
        fprintf(stderr, "[webgpu] wrote frame dump %s (%ux%u)\n", path, w, h);
    }
    wgpuBufferUnmap(buf);
}

/* ------------------------------------------------------------------------
 * Output-VI-filter post-FX pass (FXAA / bloom / grade / tonemap / gamma /
 * vignette / CAS sharpen / dither / RGB555). A fullscreen-triangle pass that
 * resolves s_scene_tex -> s_post_tex, faithfully porting gfx_opengl.c's output
 * filter (see gfx_webgpu_postfx_wgsl). Gating mirrors GL exactly: uApplyPost ==
 * g_pcRemasterFX, each effect further gated on its own g_pc* setting. SSAO
 * (planar v1) reads the sampleable scene depth target (default-off; Video.Ssao).
 * ---------------------------------------------------------------------- */
typedef struct {
    float srcSize[2];
    float dstSize[2];
    float colorScale, colorBias, gamma, saturation;
    float contrast, brightness, vignette, sharpen;
    float bloomThreshold, bloomIntensity, levelSat, levelCon;
    float colorTint[3];
    int32_t applyPost;
    int32_t dither, bloom, fxaa;
    int32_t tonemap, rgb555, fbH, ssao;
    /* SSAO (planar v1) — ports gfx_opengl.c's uSsao* uniforms. */
    float ssaoRadius, ssaoIntensity, ssaoAspect, ssaoProjA;
    float ssaoProjB;   /* struct ends at 128 bytes — WGSL 16-byte-rounded, no trailing pad */
} WgpuPostU;

static WGPURenderPipeline  s_post_pipe = NULL;
static WGPUBindGroupLayout s_post_bgl  = NULL;
static WGPUBuffer          s_post_ubuf = NULL;
static WGPUSampler         s_post_sampN = NULL;   /* nearest + clamp */
static WGPUSampler         s_post_sampL = NULL;   /* linear + clamp */
static WGPUBindGroup       s_post_bg = NULL;
static WGPUTextureView     s_post_bg_view = NULL; /* scene view the bind group binds */

static float wgpu_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* True when the output filter changes any pixel — i.e. the master remaster
 * switch is on, or a non-identity display gamma is set. When false, end_frame
 * keeps the plain scene->surface copy so faithful frames stay byte-identical to
 * the pre-post-FX backend (and the tape/aperture gates). Mirrors the spirit of
 * gfx_opengl_output_color_adjust_active (the diag colorScale/bias/rgb555 knobs
 * are GL-file statics, not ported — they are off by default). */
static bool wgpu_postfx_active(void) {
    float gamma = wgpu_clampf(g_pcVideoGamma, 0.5f, 2.5f);
    if (gamma < 0.999f || gamma > 1.001f) {
        return true;
    }
    return g_pcRemasterFX != 0;
}

static bool wgpu_ensure_postfx(void) {
    if (s_post_pipe != NULL) {
        return true;
    }
    if (!s_ready) {
        return false;
    }
    WGPUShaderSourceWGSL src = {0};
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code = wgpu_sv(gfx_webgpu_postfx_wgsl());
    WGPUShaderModuleDescriptor smd = {0};
    smd.nextInChain = (WGPUChainedStruct *)&src;
    WGPUShaderModule mod = wgpuDeviceCreateShaderModule(s_device, &smd);
    if (mod == NULL) {
        return false;
    }

    /* group 0: uniform(0), scene texture(1), nearest sampler(2), linear sampler(3),
     * scene depth texture(4, for SSAO), non-filtering depth sampler(5). */
    WGPUBindGroupLayoutEntry e[6] = {0};
    e[0].binding = 0; e[0].visibility = WGPUShaderStage_Fragment;
    e[0].buffer.type = WGPUBufferBindingType_Uniform;
    e[0].buffer.minBindingSize = sizeof(WgpuPostU);
    e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
    e[1].texture.sampleType = WGPUTextureSampleType_Float;
    e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment;
    e[2].sampler.type = WGPUSamplerBindingType_Filtering;
    e[3].binding = 3; e[3].visibility = WGPUShaderStage_Fragment;
    e[3].sampler.type = WGPUSamplerBindingType_Filtering;
    /* SSAO depth read: depth textures are unfilterable — sampleType Depth + a
     * NonFiltering sampler (nearest/clamp, matching GL/Metal's depth sampler). */
    e[4].binding = 4; e[4].visibility = WGPUShaderStage_Fragment;
    e[4].texture.sampleType = WGPUTextureSampleType_Depth;
    e[4].texture.viewDimension = WGPUTextureViewDimension_2D;
    e[5].binding = 5; e[5].visibility = WGPUShaderStage_Fragment;
    e[5].sampler.type = WGPUSamplerBindingType_NonFiltering;
    WGPUBindGroupLayoutDescriptor bgld = {0};
    bgld.entryCount = 6;
    bgld.entries = e;
    s_post_bgl = wgpuDeviceCreateBindGroupLayout(s_device, &bgld);

    WGPUBufferDescriptor ubd = {0};
    ubd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    ubd.size = sizeof(WgpuPostU);
    s_post_ubuf = wgpuDeviceCreateBuffer(s_device, &ubd);

    WGPUSamplerDescriptor sn = {0};
    sn.addressModeU = sn.addressModeV = sn.addressModeW = WGPUAddressMode_ClampToEdge;
    sn.magFilter = sn.minFilter = WGPUFilterMode_Nearest;
    sn.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sn.maxAnisotropy = 1;
    s_post_sampN = wgpuDeviceCreateSampler(s_device, &sn);
    WGPUSamplerDescriptor sl = sn;
    sl.magFilter = sl.minFilter = WGPUFilterMode_Linear;
    s_post_sampL = wgpuDeviceCreateSampler(s_device, &sl);

    WGPUPipelineLayoutDescriptor pld = {0};
    pld.bindGroupLayoutCount = 1;
    pld.bindGroupLayouts = &s_post_bgl;
    WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(s_device, &pld);

    WGPUColorTargetState color = {0};
    color.format = s_surface_format;
    color.writeMask = WGPUColorWriteMask_All;   /* opaque overwrite; no blend */
    WGPUFragmentState fs = {0};
    fs.module = mod; fs.entryPoint = wgpu_sv("fs_main");
    fs.targetCount = 1; fs.targets = &color;
    WGPURenderPipelineDescriptor pd = {0};
    pd.layout = pl;
    pd.vertex.module = mod; pd.vertex.entryPoint = wgpu_sv("vs_main");
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.primitive.unclippedDepth = s_unclipped_depth_supported ? WGPU_TRUE : WGPU_FALSE;
    pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFFu;
    pd.fragment = &fs;   /* no depth-stencil: fullscreen resolve has no depth */
    s_post_pipe = wgpuDeviceCreateRenderPipeline(s_device, &pd);

    wgpuPipelineLayoutRelease(pl);
    wgpuShaderModuleRelease(mod);
    return s_post_pipe != NULL && s_post_ubuf != NULL && s_post_sampN != NULL &&
           s_post_sampL != NULL;
}

/* Run the output filter: s_scene_tex -> `target`, using the still-open frame encoder.
 * `target` is s_post_view on the offscreen present path, or the acquired surface view
 * on the PERF-008 direct-to-surface path. Returns true if the target now holds the
 * filtered frame. */
static bool wgpu_run_postfx(WGPUTextureView target) {
    if (!wgpu_ensure_postfx() || s_encoder == NULL || s_scene_view == NULL ||
        target == NULL || s_scene_w == 0 || s_scene_h == 0) {
        return false;
    }

    int gp = g_pcGradePresets ? 1 : 0;
    int apply_post = g_pcRemasterFX ? 1 : 0;
    WgpuPostU u = {0};
    u.srcSize[0] = (float)s_scene_w; u.srcSize[1] = (float)s_scene_h;
    u.dstSize[0] = (float)s_scene_w; u.dstSize[1] = (float)s_scene_h;
    u.colorScale = 1.0f;   /* GE007_DIAG_OUTPUT_FILTER_COLOR knobs not ported (diag-only) */
    u.colorBias  = 0.0f;
    u.gamma = wgpu_clampf(g_pcVideoGamma, 0.5f, 2.5f);
    u.saturation = wgpu_clampf(g_pcVideoSaturation, 0.0f, 2.0f);
    u.contrast   = wgpu_clampf(g_pcVideoContrast, 0.5f, 2.0f);
    u.brightness = wgpu_clampf(g_pcVideoBrightness, -0.5f, 0.5f);
    u.vignette   = wgpu_clampf(g_pcVignette, 0.0f, 1.0f);
    u.sharpen    = apply_post ? wgpu_clampf(g_pcSharpen, 0.0f, 1.0f) : 0.0f;
    u.bloomThreshold = g_pcBloomThreshold;
    u.bloomIntensity = g_pcBloomIntensity;
    u.levelSat = gp ? g_pcGradeLevelSat : 1.0f;
    u.levelCon = gp ? g_pcGradeLevelCon : 1.0f;
    u.colorTint[0] = gp ? g_pcGradeLevelTintR : 1.0f;
    u.colorTint[1] = gp ? g_pcGradeLevelTintG : 1.0f;
    u.colorTint[2] = gp ? g_pcGradeLevelTintB : 1.0f;
    u.applyPost = apply_post;
    u.dither = g_pcOutputDither ? 1 : 0;
    u.bloom = g_pcBloom ? 1 : 0;
    u.fxaa = (apply_post && g_pcFxaa) ? 1 : 0;
    u.tonemap = (apply_post && g_pcTonemap) ? 1 : 0;
    u.rgb555 = 0;   /* GE007_DIAG_OUTPUT_RGB555 not ported (diag-only, default off) */
    u.fbH = (int32_t)s_scene_h;
    /* SSAO (planar v1) — gate + uniforms mirror gfx_opengl.c:3561-3579 exactly.
     * apply_post already == g_pcRemasterFX (the remaster-master gate), so SSAO is a
     * remaster effect; proj_b != 0 means a scene projection was captured this frame
     * (menu/HUD frames leave it 0 -> no AO). WebGPU is never MSAA, so the GL/Metal
     * "SSAO off under MSAA" limit does not apply. Video.SsaoMode=hemisphere (v2) is
     * a Metal-only effect; like GL, WebGPU falls back to planar v1 with a one-time
     * note. */
    int ssao_on = (apply_post && g_pcSsao != 0 && g_pc_ssao_proj_b != 0.0f) ? 1 : 0;
    if (ssao_on && g_pcSsaoMode == 2) {
        static int warned_ssao_mode;
        if (!warned_ssao_mode) {
            fprintf(stderr, "[webgpu] Video.SsaoMode=hemisphere is a Metal-only effect; "
                            "WebGPU falls back to planar SSAO v1.\n");
            warned_ssao_mode = 1;
        }
    }
    u.ssao = ssao_on;
    u.ssaoRadius = g_pcSsaoRadius * 0.02f;   /* radius key -> UV offset scale (load-bearing) */
    u.ssaoIntensity = g_pcSsaoIntensity;
    u.ssaoAspect = s_scene_h > 0 ? (float)s_scene_w / (float)s_scene_h : 1.0f;
    u.ssaoProjA = g_pc_ssao_proj_a;
    u.ssaoProjB = g_pc_ssao_proj_b;
    wgpuQueueWriteBuffer(s_queue, s_post_ubuf, 0, &u, sizeof(u));

    /* (Re)build the bind group when the scene view changes (resolution change). */
    if (s_post_bg == NULL || s_post_bg_view != s_scene_view) {
        if (s_post_bg != NULL) { wgpuBindGroupRelease(s_post_bg); s_post_bg = NULL; }
        WGPUBindGroupEntry be[6] = {0};
        be[0].binding = 0; be[0].buffer = s_post_ubuf; be[0].size = sizeof(WgpuPostU);
        be[1].binding = 1; be[1].textureView = s_scene_view;
        be[2].binding = 2; be[2].sampler = s_post_sampN;
        be[3].binding = 3; be[3].sampler = s_post_sampL;
        /* SSAO depth: bound unconditionally (the shader only reads it when u.ssao==1,
         * so faithful/SSAO-off frames are unaffected). s_depth_view is recreated in
         * lockstep with s_scene_view, so the same rebuild trigger covers it. The
         * nearest/clamp s_post_sampN doubles as the NonFiltering depth sampler. */
        be[4].binding = 4; be[4].textureView = s_depth_view;
        be[5].binding = 5; be[5].sampler = s_post_sampN;
        WGPUBindGroupDescriptor bgd = {0};
        bgd.layout = s_post_bgl;
        bgd.entryCount = 6;
        bgd.entries = be;
        s_post_bg = wgpuDeviceCreateBindGroup(s_device, &bgd);
        s_post_bg_view = s_scene_view;
    }
    if (s_post_bg == NULL) {
        return false;
    }

    WGPURenderPassColorAttachment att = {0};
    att.view = target;
    att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    att.loadOp = WGPULoadOp_Clear;   /* fullscreen triangle covers all pixels */
    att.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor rp = {0};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &att;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(s_encoder, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, s_post_pipe);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, s_post_bg, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return true;
}

/* PERF-008: could ANY frame in this session be read back or dumped? The offscreen
 * present target must be retained (and the trailing texture-to-texture present copy
 * kept) for such frames, because read_framebuffer_rgb + the frame/surface PPM dumps
 * read s_present_target_tex AFTER present — a surface view is freed by then. Every
 * trigger is armed at process start (a CLI flag or an env var), so a sticky latch is
 * a safe conservative SUPERSET:
 *   - the --screenshot-frame session (g_screenshotFrameSessionActive), its armed frame
 *     (g_autoScreenshotFrame) and --screenshot-game-timer (g_autoScreenshotGameTimer),
 *     which the parity/oracle screenshot ctests drive;
 *   - the AUDIT-0003 screenshot series (GE007_SCREENSHOT_SERIES_DIR);
 *   - the frame-30 GE007_SCREENSHOT one-shot;
 *   - the in-end_frame WebGPU PPM dumps (GE007_WEBGPU_DUMP_FRAME / _SURFACE);
 *   - the diag display-cast / menu captures;
 *   - the F2 manual screenshot, itself inert unless GE007_DEV_HOTKEYS is set;
 *   - the gfx_pc.c diagnostic pixel probes (GE007_TRACE_*_PIXEL / _FB_CAPTURE), which
 *     read back mid-frame; the sticky s_readback_latched (tripped by the first actual
 *     read_framebuffer_rgb) also nets these — and any future reader — from then on.
 * When none are armed (normal gameplay / the web demo) a post-FX frame renders
 * straight into the surface. Sticky so a flag that self-clears after firing (the
 * auto-screenshot frame/timer reset to -1) still pins us to the offscreen path.
 * Correctness-first: any unrecognised readback route ⇒ keep the offscreen path. */
static bool wgpu_readback_possible(void) {
    if (s_readback_latched) {
        return true;
    }
    extern int g_screenshotFrameSessionActive;
    extern int g_autoScreenshotFrame;
    extern int g_autoScreenshotGameTimer;
    if (g_screenshotFrameSessionActive || g_autoScreenshotFrame >= 0 ||
        g_autoScreenshotGameTimer >= 0) {
        s_readback_latched = true;
        return true;
    }
    static int env_armed = -1;   /* env triggers are process-constant; parse once */
    if (env_armed < 0) {
        env_armed = (getenv("GE007_SCREENSHOT_SERIES_DIR")             != NULL ||
                     getenv("GE007_SCREENSHOT")                        != NULL ||
                     getenv("GE007_WEBGPU_DUMP_FRAME")                 != NULL ||
                     getenv("GE007_WEBGPU_DUMP_SURFACE")               != NULL ||
                     getenv("GE007_DIAG_DISPLAYCAST_SCREENSHOT_TIMER") != NULL ||
                     getenv("GE007_DIAG_MENU_SCREENSHOT_MENU")         != NULL ||
                     getenv("GE007_DEV_HOTKEYS")                       != NULL ||
                     getenv("GE007_TRACE_SETTEX_FB_CAPTURE")           != NULL ||
                     getenv("GE007_TRACE_SETTEX_PIXEL")                != NULL ||
                     getenv("GE007_TRACE_TRI_PIXEL")                   != NULL ||
                     getenv("GE007_TRACE_ROOM_XLU_DEFER_PIXEL")        != NULL ||
                     getenv("GE007_TRACE_SKY_PREP_PIXEL")              != NULL) ? 1 : 0;
    }
    if (env_armed) {
        s_readback_latched = true;
    }
    return env_armed != 0;
}

static void wgpu_end_frame(void) {
    if (!s_frame_open) {
        /* start_frame bailed (not ready / no texture) — nothing to submit. */
        return;
    }
    wgpuRenderPassEncoderEnd(s_pass);
    wgpuRenderPassEncoderRelease(s_pass);
    s_pass = NULL;

    /* PERF-005b: a frame that dropped draw batches because their async pipelines
     * are still compiling (PERF-005 web-live path) is visually incomplete — the
     * missing world geometry reads as sky/backdrop "bleeding" through walls at
     * level entry and on first-sight materials mid-mission. Do NOT present such a
     * frame: skip the surface acquire below entirely, so the canvas (web: the
     * canvas only updates on a frame whose getCurrentTexture was taken) / window
     * (native: present is guarded on present_ok) keeps the LAST COMPLETE image
     * while the offscreen scene still renders, the sim advances, and the end-of-
     * frame drain lands the pending pipelines. Typical hold is 1-4 frames.
     * Bounded: after WGPU_PRESENT_HOLD_MAX consecutive holds we present anyway,
     * so a wedged compile can never freeze the output (and FAILED pipelines never
     * count toward the hold — see s_frame_pending_skips). Native and web
     * --deterministic never store PENDING slots, so the counter is permanently 0
     * there and this block is behavior-neutral for every byte-exact gate. */
    #define WGPU_PRESENT_HOLD_MAX 30
    static int s_present_hold_frames = 0;
    bool hold_present = false;
    if (s_frame_pending_skips > 0 && s_present_hold_frames < WGPU_PRESENT_HOLD_MAX) {
        hold_present = true;
        s_present_hold_frames++;
    } else {
        s_present_hold_frames = 0;
    }
    s_frame_pending_skips = 0;

    /* Acquire the window drawable up front: the PERF-008 direct-to-surface path below
     * renders the output filter straight into it, and the offscreen path uses it as
     * the present-copy destination. A hidden/occluded window has no drawable — that's
     * fine, the offscreen scene still rendered (and can be dumped/read back). Exactly
     * one GetCurrentTexture per frame, as before (just hoisted above the resolve). */
    WGPUSurfaceTexture st = {0};
    if (!hold_present) {
        wgpuSurfaceGetCurrentTexture(s_surface, &st);
    }
    bool present_ok = st.texture != NULL &&
        (st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
         st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal);

    /* PERF-008: when the output filter is active AND this frame is never read back,
     * render the FINAL post-FX pass straight into the surface, eliminating the
     * trailing full-resolution texture-to-texture present copy (~4-15 MB/frame plus a
     * store on tile-based GPUs). The offscreen present target + copy are retained for
     * any frame that can be read back — the screenshot/parity/dump harness reads
     * s_present_target_tex AFTER present (AUDIT-0003) and a surface view is gone by
     * then — and for faithful/no-post-FX frames (no fullscreen pass to redirect, and a
     * blit would cost as much as the copy). wgpu_readback_possible() is a conservative
     * session-level superset; in any doubt we keep the byte-identical offscreen path. */
    WGPUTextureView surface_view = NULL;
    bool direct = false;
    if (present_ok && !wgpu_readback_possible() && wgpu_postfx_active()) {
        surface_view = wgpuTextureCreateView(st.texture, NULL);
        if (surface_view != NULL && wgpu_run_postfx(surface_view)) {
            direct = true;
            /* Presented pixels now live only in the surface (released after present),
             * so there is no persistent readback target — safe, since the gate above
             * proved this frame is never read back. NULL steers any stray readback to
             * the still-valid raw scene rather than a freed surface texture. */
            s_present_target_tex  = NULL;
            s_present_target_view = surface_view;
        } else if (surface_view != NULL) {
            wgpuTextureViewRelease(surface_view);   /* post-FX unavailable; fall back */
            surface_view = NULL;
        }
    }

    /* Offscreen present path (byte-identical to the pre-PERF-008 backend). Resolve the
     * raw scene through the VI filter into s_post_tex when active — THAT becomes the
     * frame that is minimap-composited, copied to the surface and read back — matching
     * GL (output filter into the default FB, THEN the minimap on top) and Metal
     * (s_final_color, THEN minimap). A faithful frame (filter inactive) keeps the raw
     * scene as the present source. The minimap is drawn AFTER the filter so it is not
     * tonemapped/graded, exactly as on GL/Metal. */
    if (!direct) {
        s_present_target_tex  = s_scene_tex;
        s_present_target_view = s_scene_view;
        if (wgpu_postfx_active() && wgpu_run_postfx(s_post_view)) {
            s_present_target_tex  = s_post_tex;
            s_present_target_view = s_post_view;
        }
    }

    /* Minimap / radar overlay: a 2D screen-space pass into the present target after
     * the post-FX (the GL path draws it in gfx_end_frame, Metal in mtl_end_frame;
     * gfx_end_frame skips it for non-GL backends). Reads Input.MinimapEnabled +
     * the frame queue internally; no-op when disabled/empty. */
    {
        extern void minimap_overlay_draw_queued_frames_webgpu(int fb_width, int fb_height);
        minimap_overlay_draw_queued_frames_webgpu((int)s_scene_w, (int)s_scene_h);
    }

    /* Optional debug frame dump: copy the presented frame (post-FX + minimap) into
     * a mappable buffer (works even when the window is hidden, unlike a surface
     * readback). */
    static int frame_no = -1;
    frame_no++;
    WGPUBuffer dump_buf = NULL;
    uint32_t dump_bpr = 0;
    if (frame_no == wgpu_dump_target_frame() && s_scene_w > 0 && s_scene_h > 0) {
        dump_bpr = ((s_scene_w * 4u + 255u) / 256u) * 256u;   /* 256-align for CopyTextureToBuffer */
        WGPUBufferDescriptor bd = {0};
        bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        bd.size = (uint64_t)dump_bpr * s_scene_h;
        dump_buf = wgpuDeviceCreateBuffer(s_device, &bd);
        if (dump_buf != NULL) {
            WGPUTexelCopyTextureInfo src = {0};
            src.texture = s_present_target_tex; src.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyBufferInfo dst = {0};
            dst.buffer = dump_buf;
            dst.layout.bytesPerRow = dump_bpr;
            dst.layout.rowsPerImage = s_scene_h;
            WGPUExtent3D ext = { s_scene_w, s_scene_h, 1 };
            wgpuCommandEncoderCopyTextureToBuffer(s_encoder, &src, &dst, &ext);
        }
    }

    /* Present copy: OFFSCREEN path only. The direct path (PERF-008) already rendered
     * the final frame straight into the surface, so it needs no copy. Same format +
     * size (surface configured to the scene res). A hidden/occluded window has no
     * drawable — present is skipped; the offscreen frame still rendered and read back. */
    if (present_ok && !direct) {
        WGPUTexelCopyTextureInfo cs = {0};
        cs.texture = s_present_target_tex; cs.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyTextureInfo cd = {0};
        cd.texture = st.texture; cd.aspect = WGPUTextureAspect_All;
        WGPUExtent3D ext = { s_scene_w, s_scene_h, 1 };
        wgpuCommandEncoderCopyTextureToTexture(s_encoder, &cs, &cd, &ext);
    }

    /* In-game overlay (F1 menu). The hook is called EXACTLY ONCE every frame —
     * matching the GL gfx_end_frame's unconditional call — so its per-frame logic
     * (e.g. the headless open/close test tick, which advances the engine-frame
     * ordinal) stays in lockstep even on a frame that drops its drawable.
     * The bandwidth-heavy Load+Store surface pass only opens when we actually
     * have a drawable AND the overlay is visible (platformOverlayWantsInput);
     * otherwise the overlay's draw is skipped (current_overlay_pass() == NULL) so
     * standalone boots (no hooks → 0) and closed-overlay gameplay pay nothing. */
    extern void platformOverlayRender(void);
    extern int  platformOverlayWantsInput(void);
    WGPUTextureView overlay_view = NULL;
    if (present_ok && platformOverlayWantsInput()) {
        overlay_view = wgpuTextureCreateView(st.texture, NULL);
    }
    if (overlay_view != NULL) {
        WGPURenderPassColorAttachment oa = {0};
        oa.view = overlay_view;
        oa.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        oa.loadOp = WGPULoadOp_Load;      /* preserve the blitted scene */
        oa.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor orp = {0};
        orp.colorAttachmentCount = 1;
        orp.colorAttachments = &oa;
        s_overlay_pass = wgpuCommandEncoderBeginRenderPass(s_encoder, &orp);
        s_overlay_w = (int)s_scene_w;
        s_overlay_h = (int)s_scene_h;
        platformOverlayRender();
        wgpuRenderPassEncoderEnd(s_overlay_pass);
        wgpuRenderPassEncoderRelease(s_overlay_pass);
        s_overlay_pass = NULL;
        wgpuTextureViewRelease(overlay_view);
    } else {
        platformOverlayRender();   /* per-frame logic only; no pass, no draw */
    }

    /* Optional surface dump: the presented frame (scene + overlay), unlike the
     * scene dump above which is overlay-free. Requires the surface CopySrc usage. */
    WGPUBuffer surf_dump_buf = NULL;
    uint32_t surf_dump_bpr = 0;
    if (present_ok && frame_no == wgpu_dump_surface_frame() && s_cfg_w > 0 && s_cfg_h > 0) {
        surf_dump_bpr = ((s_cfg_w * 4u + 255u) / 256u) * 256u;
        WGPUBufferDescriptor bd = {0};
        bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        bd.size = (uint64_t)surf_dump_bpr * s_cfg_h;
        surf_dump_buf = wgpuDeviceCreateBuffer(s_device, &bd);
        if (surf_dump_buf != NULL) {
            WGPUTexelCopyTextureInfo src = {0};
            src.texture = st.texture; src.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyBufferInfo dst = {0};
            dst.buffer = surf_dump_buf;
            dst.layout.bytesPerRow = surf_dump_bpr;
            dst.layout.rowsPerImage = s_cfg_h;
            WGPUExtent3D ext = { s_cfg_w, s_cfg_h, 1 };
            wgpuCommandEncoderCopyTextureToBuffer(s_encoder, &src, &dst, &ext);
        }
    }

    /* WEB-023 (residual): ONE vertex upload per frame. Every scene draw and the
     * minimap overlay memcpy'd their batch into s_vbuf_shadow at its bump offset;
     * push the whole referenced range [0, s_vbuf_off) to the GPU in a SINGLE
     * wgpuQueueWriteBuffer here — replacing the ~100-200 per-batch writes (each a
     * wasm↔JS crossing + a queue command). Queue writes execute before this command
     * buffer (WEB-053), so every draw's SetVertexBuffer(voff, bytes) reads exactly
     * the bytes staged for it, byte-identical to the old per-batch scheme. Covers the
     * hidden-drawable frame too: no present, but the offscreen scene still drew and
     * is submitted right below. s_vbuf_off is kept 4-byte aligned by the bump, so the
     * size is a valid queue-write size; skipped when the shadow is absent (writers
     * fell back to per-batch writes) or the frame drew no geometry (s_vbuf_off == 0). */
    if (s_vbuf != NULL && s_vbuf_shadow != NULL && s_vbuf_off > 0) {
        wgpuQueueWriteBuffer(s_queue, s_vbuf, 0, s_vbuf_shadow, s_vbuf_off);
    }

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(s_encoder, NULL);
    wgpuQueueSubmit(s_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(s_encoder);
    s_encoder = NULL;

    if (dump_buf != NULL) {
        char path[128];
        snprintf(path, sizeof(path), "/tmp/webgpu_frame_%d.ppm", frame_no);
        wgpu_write_ppm(dump_buf, dump_bpr, s_scene_w, s_scene_h, path);
        wgpuBufferRelease(dump_buf);
    }
    if (surf_dump_buf != NULL) {
        char path[128];
        snprintf(path, sizeof(path), "/tmp/webgpu_surface_%d.ppm", frame_no);
        wgpu_write_ppm(surf_dump_buf, surf_dump_bpr, s_cfg_w, s_cfg_h, path);
        wgpuBufferRelease(surf_dump_buf);
    }

    if (present_ok) {
        /* Dialect seam (gfx_webgpu_compat.h): native presents explicitly via
         * wgpuSurfacePresent; the browser canvas auto-presents when the frame's
         * JS task yields (requestAnimationFrame), and emscripten's binding
         * aborts on an explicit present — so the browser side is a no-op. */
        WGPU_COMPAT_PRESENT(s_surface);
    }
    if (surface_view != NULL) {
        wgpuTextureViewRelease(surface_view);   /* PERF-008 direct-path render target */
        /* Review fix: on a direct frame s_present_target_view aliased this view;
         * null it so nothing can deref a released view across the frame boundary
         * (nothing does today — readback uses s_present_target_tex — but a
         * dangling handle is a trap for future overlay/readback work). */
        if (s_present_target_view == surface_view) {
            s_present_target_view = NULL;
        }
    }
    if (st.texture != NULL) {
        wgpuTextureRelease(st.texture);
    }

    /* PERF-005: drain async pipeline completions. emdawnwebgpu fires the
     * AllowSpontaneous callbacks on its own event loop, but pumping
     * ProcessEvents once per frame while creates are outstanding guarantees a
     * landed pipeline becomes serve-able promptly (bounded pop-in). Guarded on
     * s_pending_pipelines > 0 so the steady state (nothing in flight) is a pure
     * no-op that never dispatches unrelated futures — and so native, where the
     * counter is permanently 0 and WGPU_COMPAT_DRAIN expands to a no-op, is
     * wholly untouched. */
    if (s_pending_pipelines > 0) {
        WGPU_COMPAT_DRAIN(s_instance);
    }

    s_frame_open = false;
}

static void wgpu_finish_render(void) {
    /* Readback/GPU-drain sync lands with read_framebuffer_rgb in Task 5. */
}

/* ------------------------------------------------------------------------
 * Vtable: shaders + render pipelines (Task 3)
 *
 * gfx_pc.c owns the shader-pointer lifecycle: it calls lookup_shader, and on a
 * miss create_and_load_new_shader, then load_shader + shader_get_info + draw.
 * Each ShaderProgram holds the compiled WGSL module, the derived vertex layout,
 * the bind-group + pipeline layouts, and a small cache of WGPURenderPipelines
 * keyed by dynamic state (WebGPU bakes blend/depth/format into the pipeline, so
 * the GL immediate setters collapse into this lazy lookup — same shape as the
 * Metal backend's mtl_pso_for).
 * ---------------------------------------------------------------------- */
/* PERF-005: on web-live the pipeline is created asynchronously (see
 * wgpu_pipeline_for), so a cache slot carries a lifecycle state. EMPTY is 0 so
 * the zero-initialized s_shaders table starts every slot EMPTY. Native (and web
 * under --deterministic) only ever stores READY — the sync path never produces a
 * PENDING/FAILED entry, so its lookup behavior is identical to before PERF-005. */
enum WgpuPipeState {
    WGPU_PIPE_EMPTY = 0,   /* unused slot                                   */
    WGPU_PIPE_PENDING,     /* async create in flight (web-live only)        */
    WGPU_PIPE_READY,       /* pipe is valid and serve-able                  */
    WGPU_PIPE_FAILED       /* async create failed; keep skipping (no re-kick) */
};
struct WgpuPipeEntry { uint32_t key; WGPURenderPipeline pipe; uint8_t state; };

struct ShaderProgram {
    uint64_t shader_id0;
    uint32_t shader_id1;
    struct WgpuShaderInfo info;
    WGPUShaderModule     module;
    WGPUVertexAttribute  vattrs[24];
    WGPUBindGroupLayout  bgl;       /* NULL when the combiner samples no textures */
    WGPUPipelineLayout   playout;
    /* Pipeline cache keyed by dynamic (blend|depth) state. Sized well above the
     * realistic combo count per combiner (a handful); the round-robin eviction
     * in wgpu_pipeline_for is a never-leak backstop for the theoretical maximum. */
    struct WgpuPipeEntry pipes[32];
    int npipes;
    int pipe_evict;   /* round-robin slot for the (never-hit) overflow case */
};
#define WGPU_PIPE_CACHE 32

#define WGPU_SHADER_MAX 1024
static struct ShaderProgram s_shaders[WGPU_SHADER_MAX];
static int s_shader_count = 0;
static int s_shader_evict = 0;   /* WEB-050: round-robin victim once the table is full */

/* Currently loaded shader + dynamic blend state (set by load_shader /
 * set_blend_mode, read by draw_triangles). */
static struct ShaderProgram *s_cur_shader = NULL;
static enum GfxBlendMode      s_cur_blend = GFX_BLEND_DISABLED;

/* Persistent draw bind-group cache (see wgpu_draw_triangles). Keyed on the
 * {bgl, view0, samp0, view1, samp1, snapview, diag_ubo} pointer tuple. The
 * previous single-entry cache thrashed on every material change — a frame that
 * cycles N materials created N bind groups per frame, and the resulting JS-side
 * object churn (Dawn createBindGroup + object-table inserts, visible in browser
 * CPU profiles) fed GC pauses that degraded 1% lows. Entries persist across
 * frames (materials recur every frame) with round-robin eviction as the
 * never-leak backstop.
 *
 * WEB-068: the key is a RAW POINTER tuple, so a cached entry MUST be invalidated
 * when a texture view it references is released — see wgpu_bg_cache_invalidate_view
 * below. The original design note here claimed "stale views can never be looked up:
 * a deleted texture's view pointer is replaced by the white fallback in the key,
 * which misses." That reasoning only covers the CURRENT draw's freshly-built key; it
 * missed that STALE entries built by earlier draws outlive the texture and keep its
 * raw view pointer. wgpuTextureViewRelease frees the C-handle ADDRESS for reuse while
 * the cached bind group keeps the underlying object alive (a strong ref) — so a later
 * texture whose new view lands at the recycled address FALSE-MATCHED a stale entry and
 * the draw sampled the OLD texture (proven natively: menu glyphs rendered as other
 * glyphs, "Select"→"Гissi2A"). A classic ABA on the handle address, which is the key.
 *
 * PERF-019: that invalidation was a full 512-entry sweep per released view — and a
 * level transition (gfx_clear_texture_cache) deletes every pooled texture, so the storm
 * was texture_count × 512 scans. Each WgpuTexEntry now carries a reverse index of the
 * cache slots referencing its view (wgpu_tex_bg_ref_add on insert), so a release walks
 * only its own slots (wgpu_bg_cache_invalidate_view_indexed). No dedicated level-flush
 * hook was added: the natural flush IS gfx_clear_texture_cache's per-texture delete
 * loop (gfx_pc.c) — which the reverse index already makes cheap — and a distinct backend
 * flush entry point would require touching gfx_pc.c / gfx_rendering_api.h, out of scope
 * here. The process-lifetime pinning is unchanged (still bounded at WGPU_BG_CACHE×WAYS)
 * and remains the device-teardown WATCH ITEM below. */
#define WGPU_BG_CACHE 512            /* power of two; 4-way set-associative */
#define WGPU_BG_WAYS  4
struct WgpuBgEntry { const void *key[7]; WGPUBindGroup bg; };
static struct WgpuBgEntry s_bg_cache_tab[WGPU_BG_CACHE];
static uint32_t s_bg_cache_way = 0;  /* round-robin victim way on set overflow */

/* WEB-068: drop every cached draw bind group that references `view` before its
 * C-handle is released, so a future view reusing the freed address cannot false-match
 * a stale entry. The view can occupy key slots 1 (view0), 3 (view1) and 5 (snapshot
 * "memory color"); the sampler slots (2, 4), the bgl (0) and the diag UBO (6) hold
 * objects that are never released for the process lifetime, so they cannot ABA. This
 * full O(512) sweep is now used only for the SNAPSHOT view (slot 5, not a WgpuTexEntry)
 * on scene resize, and as the PERF-019 overflow fallback — the common texture-view
 * releases (delete / recreate-on-resize) go through wgpu_bg_cache_invalidate_view_indexed,
 * which walks a per-texture reverse index instead. The per-slot drop predicate here is
 * the canonical one the indexed path re-verifies against, so the two stay identical. */
static void wgpu_bg_cache_invalidate_view(WGPUTextureView view) {
    if (view == NULL) {
        return;
    }
    const void *v = (const void *)view;
    for (int i = 0; i < WGPU_BG_CACHE; i++) {
        struct WgpuBgEntry *e = &s_bg_cache_tab[i];
        if (e->bg != NULL &&
            (e->key[1] == v || e->key[3] == v || e->key[5] == v)) {
            wgpuBindGroupRelease(e->bg);
            e->bg = NULL;
            memset(e->key, 0, sizeof(e->key));
        }
    }
}
/* WEB-068 defense-in-depth: same purge for a bind-group LAYOUT about to be
 * released (key slot 0). Only reachable from WEB-050 shader eviction, itself an
 * unreachable >WGPU_SHADER_MAX regime — but a released bgl handle recycled under
 * a stale key is the exact ABA class the view fix closes, so close it here too
 * rather than re-litigate if the regime is ever entered. */
static void wgpu_bg_cache_invalidate_bgl(WGPUBindGroupLayout bgl) {
    if (bgl == NULL) {
        return;
    }
    const void *v = (const void *)bgl;
    for (int i = 0; i < WGPU_BG_CACHE; i++) {
        struct WgpuBgEntry *e = &s_bg_cache_tab[i];
        if (e->bg != NULL && e->key[0] == v) {
            wgpuBindGroupRelease(e->bg);
            e->bg = NULL;
            memset(e->key, 0, sizeof(e->key));
        }
    }
}
/* WATCH ITEM: entries pin their WGPUBindGroup (and transitively the texture
 * views/samplers baked into it) for the process lifetime — there is no
 * device-teardown path that clears this table (bounded at 2048 live entries:
 * WGPU_BG_CACHE * WGPU_BG_WAYS). Fine today because the process only ever
 * owns one WGPUDevice; a future hot-reload / device-recreate path MUST flush
 * s_bg_cache_tab (and release the pinned bind groups) first, or it will
 * reference bind groups built against a destroyed device. */

static uint32_t wgpu_bg_key_hash(const void *const key[7]) {
    uintptr_t h = 0x9e3779b9u;
    for (int i = 0; i < 7; i++) {
        h ^= (uintptr_t)key[i] >> 4;   /* pointers are >=16-aligned; drop zeros */
        h *= 0x85ebca6bu;
    }
    return (uint32_t)(h ^ (h >> 16));
}

static struct ShaderProgram *wgpu_lookup_shader(uint64_t shader_id0, uint32_t shader_id1) {
    for (int i = 0; i < s_shader_count; ++i) {
        if (s_shaders[i].shader_id0 == shader_id0 && s_shaders[i].shader_id1 == shader_id1) {
            return &s_shaders[i];
        }
    }
    return NULL;
}

/* Build the bind-group layout for a combiner: (tex,sampler) pairs for each used
 * texture at bindings (0,1) and (2,3). NULL when no textures are sampled. */
static WGPUBindGroupLayout wgpu_make_bgl(const struct WgpuShaderInfo *info) {
    WGPUBindGroupLayoutEntry ents[8];   /* 2 tex + 2 samp + snap tex/samp + diag + noise */
    int ne = 0;
    for (int t = 0; t < 2; t++) {
        if (!info->used_textures[t]) continue;
        WGPUBindGroupLayoutEntry te = {0};
        te.binding = (uint32_t)(t * 2);
        te.visibility = WGPUShaderStage_Fragment;
        te.texture.sampleType = WGPUTextureSampleType_Float;
        te.texture.viewDimension = WGPUTextureViewDimension_2D;
        ents[ne++] = te;
        WGPUBindGroupLayoutEntry se = {0};
        se.binding = (uint32_t)(t * 2 + 1);
        se.visibility = WGPUShaderStage_Fragment;
        se.sampler.type = WGPUSamplerBindingType_Filtering;
        ents[ne++] = se;
    }
    if (info->diag_rdp_memory_blend || info->diag_rdp_cvg_memory_blend) {
        WGPUBindGroupLayoutEntry te = {0};
        te.binding = 4;   /* scene snapshot ("memory color") */
        te.visibility = WGPUShaderStage_Fragment;
        te.texture.sampleType = WGPUTextureSampleType_Float;
        te.texture.viewDimension = WGPUTextureViewDimension_2D;
        ents[ne++] = te;
        WGPUBindGroupLayoutEntry se = {0};
        se.binding = 5;
        se.visibility = WGPUShaderStage_Fragment;
        se.sampler.type = WGPUSamplerBindingType_Filtering;
        ents[ne++] = se;
    }
    if (info->diag_rdp_cvg_memory_blend) {
        WGPUBindGroupLayoutEntry ue = {0};
        ue.binding = 6;   /* GL-convention viewport (uDiagViewport) */
        ue.visibility = WGPUShaderStage_Fragment;
        ue.buffer.type = WGPUBufferBindingType_Uniform;
        ue.buffer.minBindingSize = 16;
        ents[ne++] = ue;
    }
    /* WEB-027: per-frame noise uniform, only for combiners that read SHADER_NOISE
     * (keeps noise-free pipelines at their exact prior binding set). */
    if (info->uses_noise) {
        WGPUBindGroupLayoutEntry ne_ent = {0};
        ne_ent.binding = 7;   /* uNoise (frame counter + render height) */
        ne_ent.visibility = WGPUShaderStage_Fragment;
        ne_ent.buffer.type = WGPUBufferBindingType_Uniform;
        ne_ent.buffer.minBindingSize = 16;
        ents[ne++] = ne_ent;
    }
    if (ne == 0) {
        return NULL;
    }
    WGPUBindGroupLayoutDescriptor d = {0};
    d.entryCount = (size_t)ne;
    d.entries = ents;
    return wgpuDeviceCreateBindGroupLayout(s_device, &d);
}

/* WEB-050: release a shader slot's GPU objects (module, cached pipelines, layouts)
 * so the slot can be recompiled into a different combiner instead of leaking. */
static void wgpu_release_shader_gpu(struct ShaderProgram *prg) {
    for (int i = 0; i < prg->npipes; i++) {
        if (prg->pipes[i].pipe != NULL) {
            wgpuRenderPipelineRelease(prg->pipes[i].pipe);
            prg->pipes[i].pipe = NULL;
        }
    }
    prg->npipes = 0;
    prg->pipe_evict = 0;
    if (prg->playout != NULL) { wgpuPipelineLayoutRelease(prg->playout); prg->playout = NULL; }
    if (prg->bgl != NULL) {
        wgpu_bg_cache_invalidate_bgl(prg->bgl);   /* WEB-068: purge stale key-slot-0 refs first */
        wgpuBindGroupLayoutRelease(prg->bgl);
        prg->bgl = NULL;
    }
    if (prg->module != NULL)  { wgpuShaderModuleRelease(prg->module);    prg->module = NULL; }
}

static struct ShaderProgram *wgpu_create_and_load_new_shader(uint64_t shader_id0, uint32_t shader_id1) {
    struct ShaderProgram *prg = wgpu_lookup_shader(shader_id0, shader_id1);
    if (prg != NULL) {
        s_cur_shader = prg;
        return prg;
    }
    /* WEB-050: the table holds WGPU_SHADER_MAX distinct combiners. It is never
     * expected to fill (the whole game uses far fewer), but the old backstop
     * aliased slot 0 forever — new materials silently rendered with slot 0's
     * combiner (the silent-wrong class the DLCOL sweep hunted). Instead, evict a
     * slot round-robin (the pipeline cache's backstop philosophy): release its
     * GPU objects and recompile the new combiner into it. NOTE: gfx_pc caches a
     * ShaderProgram* per ColorCombiner, so at >WGPU_SHADER_MAX LIVE combiners the
     * victim's cached pointer would then read a different combiner's shader — but
     * that regime is equally unreachable and strictly no worse than (and no longer
     * leaks like) the slot-0 alias. The bind-group cache keys on bgl POINTER
     * VALUES: pinned strong refs keep the released layout's OBJECT alive but not
     * its handle ADDRESS unique (the WEB-068 lesson), so eviction purges matching
     * cache entries via wgpu_bg_cache_invalidate_bgl before releasing the bgl. */
    bool evicting = false;
    if (s_shader_count >= WGPU_SHADER_MAX) {
        static bool warned = false;
        if (!warned) {
            fprintf(stderr, "[webgpu] shader table full (%d) — evicting round-robin\n",
                    WGPU_SHADER_MAX);
            warned = true;
        }
        prg = &s_shaders[s_shader_evict];
        s_shader_evict = (s_shader_evict + 1) % WGPU_SHADER_MAX;
        wgpu_release_shader_gpu(prg);
        evicting = true;
    } else {
        prg = &s_shaders[s_shader_count];
    }
    memset(prg, 0, sizeof(*prg));
    prg->shader_id0 = shader_id0;
    prg->shader_id1 = shader_id1;

    char *wgsl = gfx_webgpu_build_wgsl(shader_id0, shader_id1, &prg->info);
    if (wgsl == NULL || !s_ready) {
        free(wgsl);
        if (!evicting) s_shader_count++;
        s_cur_shader = prg;
        return prg;   /* inert program; draw guards on prg->module */
    }

    WGPUShaderSourceWGSL src = {0};
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code = wgpu_sv(wgsl);
    WGPUShaderModuleDescriptor smd = {0};
    smd.nextInChain = (WGPUChainedStruct *)&src;
    prg->module = wgpuDeviceCreateShaderModule(s_device, &smd);
    free(wgsl);

    /* Vertex attributes for the pipeline's WGPUVertexBufferLayout. */
    for (int i = 0; i < prg->info.num_attrs; i++) {
        int sz = prg->info.attrs[i].size;
        prg->vattrs[i].format = sz == 1 ? WGPUVertexFormat_Float32
                              : sz == 2 ? WGPUVertexFormat_Float32x2
                              : sz == 3 ? WGPUVertexFormat_Float32x3
                                        : WGPUVertexFormat_Float32x4;
        prg->vattrs[i].offset = (uint64_t)prg->info.attrs[i].offset * sizeof(float);
        prg->vattrs[i].shaderLocation = (uint32_t)prg->info.attrs[i].location;
    }

    /* WEB-028 (diagnostic half): WebGPU guarantees only 16 vertex attributes and
     * 16 inter-stage (varying) variables; a maximal N64 combiner's CCFeatures
     * walk can emit more, and pipeline creation would then fail with a
     * console-only validation error and a silently-skipped batch (missing
     * geometry). Every VsIn attribute except clip position becomes a VsOut
     * varying, so varyings == num_attrs - 1 exactly (gfx_webgpu_shader.c). Log
     * once per shader id (this creation path runs once per id) so a real
     * crossing is diagnosable. The packing fix is deferred. */
    {
        int n_attrs = prg->info.num_attrs;
        int n_vary  = n_attrs > 0 ? n_attrs - 1 : 0;
        if (n_attrs > 16 || n_vary > 16) {
            fprintf(stderr,
                    "[webgpu] combiner exceeds WebGPU attribute/varying limit "
                    "(attrs=%d, varyings=%d, max 16) shader id=%016llx/%08x — "
                    "pipeline may fail and skip this batch\n",
                    n_attrs, n_vary,
                    (unsigned long long)shader_id0, (unsigned)shader_id1);
            fflush(stderr);
        }
    }

    prg->bgl = wgpu_make_bgl(&prg->info);
    WGPUPipelineLayoutDescriptor pld = {0};
    if (prg->bgl != NULL) {
        pld.bindGroupLayoutCount = 1;
        pld.bindGroupLayouts = &prg->bgl;
    }
    prg->playout = wgpuDeviceCreatePipelineLayout(s_device, &pld);

    if (!evicting) s_shader_count++;   /* WEB-050: eviction reuses a slot, no growth */
    s_cur_shader = prg;
    return prg;
}

static void wgpu_load_shader(struct ShaderProgram *new_prg) { s_cur_shader = new_prg; }
static void wgpu_unload_shader(struct ShaderProgram *old_prg) {
    (void)old_prg;
    s_cur_shader = NULL;
}

static void wgpu_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    if (prg != NULL) {
        if (num_inputs != NULL) *num_inputs = (uint8_t)prg->info.num_inputs;
        if (used_textures != NULL) {
            used_textures[0] = prg->info.used_textures[0];
            used_textures[1] = prg->info.used_textures[1];
        }
    } else {
        if (num_inputs != NULL) *num_inputs = 0;
        if (used_textures != NULL) { used_textures[0] = false; used_textures[1] = false; }
    }
}

/* Map a GfxBlendMode to a WGPUBlendState, matching gfx_opengl_set_blend_mode's
 * default (diag mode 0) exactly. glBlendFunc applies the same (src,dst) factors
 * to BOTH the color and alpha channels, so both components use identical factors
 * here. Returns false for the opaque modes (no blend state attached):
 *   - DISABLED, and the two RDP-memory modes (GL disables HW blend for those —
 *     their blending is shader-side framebuffer sampling, a diag path not yet
 *     ported; the opaque HW state still matches GL).
 *   - MODULATE -> (DST_COLOR, ZERO)  = src*dst.
 *   - ALPHA / ALPHA_COVERAGE / ALPHA_CVG_WRAP_STENCIL -> (SRC_ALPHA, 1-SRC_ALPHA). */
static bool wgpu_blend_state(enum GfxBlendMode mode, WGPUBlendState *out) {
    memset(out, 0, sizeof(*out));
    if (mode == GFX_BLEND_DISABLED ||
        mode == GFX_BLEND_ALPHA_RDP_MEMORY ||
        mode == GFX_BLEND_ALPHA_RDP_CVG_MEMORY) {
        return false;   /* opaque */
    }
    WGPUBlendFactor src, dst;
    if (mode == GFX_BLEND_MODULATE) {
        src = WGPUBlendFactor_Dst;      /* GL_DST_COLOR */
        dst = WGPUBlendFactor_Zero;     /* GL_ZERO */
    } else {                            /* ALPHA + coverage/stencil variants */
        src = WGPUBlendFactor_SrcAlpha;
        dst = WGPUBlendFactor_OneMinusSrcAlpha;
    }
    out->color.operation = WGPUBlendOperation_Add;
    out->color.srcFactor = src;
    out->color.dstFactor = dst;
    out->alpha.operation = WGPUBlendOperation_Add;
    out->alpha.srcFactor = src;
    out->alpha.dstFactor = dst;
    return true;
}

/* BLEND-1: coverage-alpha preservation, mirroring gfx_opengl.c:1905 and
 * gfx_metal.mm:3368. The RDP coverage-memory path stores a synthetic 3-bit
 * coverage in the scene-target ALPHA channel; a later draw reads it back to
 * emulate N64 CVG_DST_WRAP. When that feature is active, ordinary translucent
 * draws interleaved between two cvg-memory draws must NOT overwrite the stored
 * coverage — GL masks alpha off (glColorMask(T,T,T,FALSE)), Metal drops it into
 * the PSO colorWriteMask, and WebGPU bakes an RGB-only writeMask into the
 * pipeline via the key (below). The WebGPU scene is ALWAYS rendered into the
 * offscreen s_scene_tex (== GL's scene target, bound-by-default because
 * room_xlu_cvg_memory defaults on), so — exactly like Metal — GL's
 * g_scene_target_bound term is unconditionally true here and folded out. */
static bool wgpu_room_cvg_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *d = getenv("GE007_DISABLE_ROOM_XLU_CVG_MEMORY");
        const char *e = getenv("GE007_ROOM_XLU_CVG_MEMORY");
        cached = 1;
        if ((d != NULL && d[0] != '\0' && d[0] != '0') || (e != NULL && e[0] == '0')) cached = 0;
    }
    return cached != 0;
}
static bool wgpu_diag_cvg_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("GE007_DIAG_XLU_RDP_CVG_MEMORY_BLEND_CC");
        cached = (e != NULL && e[0] != '\0' && strcmp(e, "0") != 0) ? 1 : 0;
    }
    return cached != 0;
}
/* The exact GL predicate (gfx_opengl.c:1906-1913) modulo the always-true
 * scene-target term: preserve the coverage alpha for the four ordinary
 * translucent modes GL masks — but NOT for the RDP-memory / RDP-CVG-memory modes
 * themselves, which must WRITE the coverage they compute. A pure function of the
 * blend mode plus the session-constant env flags, so encoding it into the
 * pipeline key never splits a cache slot beyond what its blend bits already do. */
static bool wgpu_preserve_cov_alpha(enum GfxBlendMode blend) {
    return (wgpu_room_cvg_enabled() || wgpu_diag_cvg_enabled()) &&
           (blend == GFX_BLEND_ALPHA || blend == GFX_BLEND_MODULATE ||
            blend == GFX_BLEND_ALPHA_COVERAGE ||
            blend == GFX_BLEND_ALPHA_CVG_WRAP_STENCIL);
}

/* Scratch backing store for the pointer members of a WGPURenderPipelineDescriptor.
 * The descriptor holds raw pointers into these sub-structs, so they must outlive
 * the create call — the caller keeps one on its stack and hands it to
 * wgpu_fill_pipeline_desc by pointer (WebGPU consumes both synchronously). */
struct WgpuPipeScratch {
    WGPUVertexBufferLayout vbl;
    WGPUBlendState         blend;
    WGPUColorTargetState   color;
    WGPUFragmentState      fs;
    WGPUDepthStencilState  ds;
};

/* PERF-005: assemble the render-pipeline descriptor for `prg` at the explicit
 * 8-bit dynamic-state `key`, DECODING it (not reading the s_depth_* globals) so
 * the synchronous and asynchronous create paths build a BIT-IDENTICAL descriptor.
 * That identity is the #1 correctness guard: an async pipeline that differs from
 * what the draw would have built synchronously is a silent render divergence.
 * Everything else the descriptor reads (s_surface_format, s_unclipped_depth_supported,
 * prg->module/vattrs/info/playout) is stable config, identical on both paths.
 *
 * Key layout (see wgpu_pipeline_for): blend = bits 0-3, depth_test = bit 4,
 * depth_update = bit 5, depth_compare = bit 6, decal = bit 7,
 * preserve_cov_alpha = bit 8 (BLEND-1). */
static void wgpu_fill_pipeline_desc(struct ShaderProgram *prg, uint32_t key,
                                    WGPURenderPipelineDescriptor *pd,
                                    struct WgpuPipeScratch *sc) {
    enum GfxBlendMode blend = (enum GfxBlendMode)(key & 0xF);
    bool depth_test    = (key >> 4) & 1;
    bool depth_update  = (key >> 5) & 1;
    bool depth_compare = (key >> 6) & 1;
    bool decal         = (key >> 7) & 1;
    bool preserve_cov_alpha = (key >> 8) & 1;   /* BLEND-1: RGB-only writeMask */

    memset(sc, 0, sizeof(*sc));

    sc->vbl.stepMode = WGPUVertexStepMode_Vertex;
    sc->vbl.arrayStride = (uint64_t)prg->info.num_floats * sizeof(float);
    sc->vbl.attributeCount = (size_t)prg->info.num_attrs;
    sc->vbl.attributes = prg->vattrs;

    bool has_blend = wgpu_blend_state(blend, &sc->blend);
    sc->color.format = s_surface_format;
    /* BLEND-1: mask alpha off (RGB-only) so an interleaved ordinary XLU draw does
     * not clobber the stored RDP coverage-alpha; else write all four channels. */
    sc->color.writeMask = preserve_cov_alpha
        ? (WGPUColorWriteMask_Red | WGPUColorWriteMask_Green | WGPUColorWriteMask_Blue)
        : WGPUColorWriteMask_All;
    sc->color.blend = has_blend ? &sc->blend : NULL;

    sc->fs.module = prg->module;
    sc->fs.entryPoint = wgpu_sv("fs_main");
    sc->fs.targetCount = 1;
    sc->fs.targets = &sc->color;

    /* Depth: LessEqual when the N64 mode tests+compares (0..1 clip, 0 = near),
     * else Always; write when it tests+updates. Decal gets a small negative bias
     * so coplanar overlays win the test (mirrors GL glPolygonOffset(-2,-2)). */
    sc->ds.format = WGPU_DEPTH_FORMAT;
    sc->ds.depthCompare = (depth_test && depth_compare) ? WGPUCompareFunction_LessEqual
                                                        : WGPUCompareFunction_Always;
    sc->ds.depthWriteEnabled = (depth_test && depth_update)
                                   ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    if (decal) {
        sc->ds.depthBias = -2;
        sc->ds.depthBiasSlopeScale = -2.0f;
    }
    /* Depth-only format: stencil faces must be their default (Always/Keep) with
     * zero masks, or WebGPU rejects the pipeline. */
    sc->ds.stencilFront.compare = WGPUCompareFunction_Always;
    sc->ds.stencilFront.failOp = WGPUStencilOperation_Keep;
    sc->ds.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    sc->ds.stencilFront.passOp = WGPUStencilOperation_Keep;
    sc->ds.stencilBack = sc->ds.stencilFront;

    memset(pd, 0, sizeof(*pd));
    pd->layout = prg->playout;
    pd->vertex.module = prg->module;
    pd->vertex.entryPoint = wgpu_sv("vs_main");
    pd->vertex.bufferCount = 1;
    pd->vertex.buffers = &sc->vbl;
    pd->primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd->primitive.frontFace = WGPUFrontFace_CCW;
    pd->primitive.cullMode = WGPUCullMode_None;   /* N64 backface handled on CPU */
    pd->primitive.unclippedDepth = s_unclipped_depth_supported ? WGPU_TRUE : WGPU_FALSE;
    pd->depthStencil = &sc->ds;
    pd->multisample.count = 1;
    pd->multisample.mask = 0xFFFFFFFFu;
    pd->fragment = &sc->fs;
}

/* Reserve a pipeline-cache slot for a new key: append while the cache has room,
 * else round-robin evict (the never-hit backstop — a combiner uses a handful of
 * blend|depth combos, far under WGPU_PIPE_CACHE). Releasing the evicted slot's
 * pipe is safe: the render pass retains any pipeline already bound this frame.
 * Caller fills key/pipe/state. */
static int wgpu_pipe_reserve_slot(struct ShaderProgram *prg) {
    int slot;
    if (prg->npipes < WGPU_PIPE_CACHE) {
        slot = prg->npipes++;
    } else {
        slot = prg->pipe_evict;
        prg->pipe_evict = (prg->pipe_evict + 1) % WGPU_PIPE_CACHE;
        if (prg->pipes[slot].pipe != NULL) {
            wgpuRenderPipelineRelease(prg->pipes[slot].pipe);
        }
    }
    return slot;
}

/* ------------------------------------------------------------------------
 * PERF-005 Phase 2 (W4.3): pipeline record/replay prewarm.
 *
 * PERF-005 killed the synchronous first-sight compile stall by kicking pipeline
 * creates async (web-live) — but that traded the freeze for transient pop-in /
 * present-holds whenever the camera meets a COLD material (worst at level entry,
 * where EVERY material is cold). Phase 2 removes the cold set for revisits and
 * repeat sessions: RECORD the (shader_id0, shader_id1, pipe-key) tuples actually
 * used per stage, PERSIST them to a small text manifest in the save dir, and
 * REPLAY them SYNCHRONOUSLY during the next load screen (gfx_webgpu_prewarm_stage,
 * called from boss.c while the watchdog is suppressed and the load screen is up).
 * The async path stays the safety net for genuinely-new keys.
 *
 * CRUX (shader-creation-at-load): a recorded shader may not have a ShaderProgram
 * yet at load time (its first draw hasn't happened). That is fine — the shader is
 * built PURELY from the ids: wgpu_create_and_load_new_shader → gfx_webgpu_build_wgsl
 * → gfx_cc_get_features is a self-contained decode of (shader_id0, shader_id1) with
 * ZERO gfx_pc render state, so prewarm creates the module too. Full scope: a
 * persisted manifest warms everything from the second launch's first frame.
 *
 * Scope gate: record/persist/prewarm run only when the webgpu backend is the
 * active one and NOT under --deterministic (so every byte-exact gate stays on the
 * untouched synchronous HEAD path — prewarm never runs there). Persistence is
 * best-effort: any I/O failure just means "no prewarm", never fatal. */
extern const char *savedirPath(const char *filename);          /* src/platform/savedir.c */
extern int port_env_bool(const char *name, int default_on, const char *help); /* src/platform/port_env.h */

#define WGPU_PREWARM_FILE       "ge007_pipecache.txt"
#define WGPU_PREWARM_MAX        8192   /* total records across all stages */
#define WGPU_PREWARM_PER_STAGE  512    /* soft cap per stage (drop-on-full, one note) */

struct WgpuPrewarmRec { uint64_t id0; uint32_t id1; uint32_t key; int stage; };
static struct WgpuPrewarmRec s_prewarm_recs[WGPU_PREWARM_MAX];
static int  s_prewarm_n = 0;
static int  s_prewarm_cur_stage = -1;   /* stage the recorder tags new keys with */
static bool s_prewarm_dirty = false;    /* current stage's set grew since last flush */
static bool s_prewarm_loaded = false;   /* manifest read from disk once */

/* Enabled iff not deterministic and not explicitly disabled. Memoized: both
 * inputs are settled by the time the recorder first runs (argv already parsed),
 * and this is on the per-create record hot-ish path. */
static int wgpu_prewarm_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        extern int g_deterministic;
        cached = (!g_deterministic &&
                  port_env_bool("GE007_PIPECACHE", 1,
                      "PERF-005 Phase 2 record/replay pipeline prewarm (0 = off)"))
                 ? 1 : 0;
    }
    return cached;
}

/* Add (stage,id0,id1,key) if new and under the caps. Returns 1 when a record was
 * appended, 0 on duplicate or cap. Shared by disk-load and live-record. */
static int wgpu_prewarm_intern(int stage, uint64_t id0, uint32_t id1, uint32_t key) {
    int stage_count = 0;
    for (int i = 0; i < s_prewarm_n; i++) {
        if (s_prewarm_recs[i].stage != stage) continue;
        if (s_prewarm_recs[i].id0 == id0 && s_prewarm_recs[i].id1 == id1 &&
            s_prewarm_recs[i].key == key) {
            return 0;   /* already recorded */
        }
        stage_count++;
    }
    if (stage_count >= WGPU_PREWARM_PER_STAGE || s_prewarm_n >= WGPU_PREWARM_MAX) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            fprintf(stderr, "[webgpu] pipecache full (stage %d) — extra materials "
                            "won't prewarm\n", stage);
        }
        return 0;
    }
    s_prewarm_recs[s_prewarm_n].stage = stage;
    s_prewarm_recs[s_prewarm_n].id0 = id0;
    s_prewarm_recs[s_prewarm_n].id1 = id1;
    s_prewarm_recs[s_prewarm_n].key = key;
    s_prewarm_n++;
    return 1;
}

/* Rewrite the WHOLE manifest (all stages) when any stage grew. Best-effort: an
 * open/write failure is swallowed (clears dirty so we don't retry every frame).
 * File I/O only — safe to call post-device-teardown (e.g. from atexit). */
static void wgpu_prewarm_flush(void) {
    if (!wgpu_prewarm_enabled() || !s_prewarm_dirty) {
        return;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s", savedirPath(WGPU_PREWARM_FILE));
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        s_prewarm_dirty = false;   /* give up silently; no prewarm is acceptable */
        return;
    }
    for (int i = 0; i < s_prewarm_n; i++) {
        fprintf(f, "%d %016llx %08x %08x\n",
                s_prewarm_recs[i].stage,
                (unsigned long long)s_prewarm_recs[i].id0,
                (unsigned)s_prewarm_recs[i].id1,
                (unsigned)s_prewarm_recs[i].key);
    }
    fclose(f);
    s_prewarm_dirty = false;
}

/* Load the manifest into the per-stage sets once. Registers an atexit flush so a
 * single-stage native session (e.g. a headless --screenshot-exit boot) still
 * persists what it recorded even though it never hits a stage transition; web
 * relies on the transition flush + the shell's syncfs timer instead. */
static void wgpu_prewarm_ensure_loaded(void) {
    if (s_prewarm_loaded) {
        return;
    }
    s_prewarm_loaded = true;
    if (!wgpu_prewarm_enabled()) {
        return;
    }
    atexit(wgpu_prewarm_flush);
    char path[1024];
    snprintf(path, sizeof(path), "%s", savedirPath(WGPU_PREWARM_FILE));
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return;   /* no manifest yet (first ever run) — nothing to prewarm */
    }
    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        int stage; unsigned long long id0; unsigned id1, key;
        if (sscanf(line, "%d %llx %x %x", &stage, &id0, &id1, &key) == 4) {
            wgpu_prewarm_intern(stage, (uint64_t)id0, (uint32_t)id1, (uint32_t)key);
        }
    }
    fclose(f);
}

/* Record a (shader,key) actually created for the current stage. Called on every
 * genuine pipeline cache miss in wgpu_pipeline_for (before the sync/async split),
 * so it captures the exact key set that would otherwise be cold on a revisit. */
static void wgpu_prewarm_record(uint64_t id0, uint32_t id1, uint32_t key) {
    if (!wgpu_prewarm_enabled() || s_prewarm_cur_stage < 0) {
        return;
    }
    if (wgpu_prewarm_intern(s_prewarm_cur_stage, id0, id1, key)) {
        s_prewarm_dirty = true;
    }
}

#ifdef __EMSCRIPTEN__
/* PERF-005: async render-pipeline completion (web-live only; see the seam-rule
 * note at wgpuCompatCreateSurface for why this block is #ifdef'd rather than in
 * the compat header — it touches file-static cache state). Recovers prg=u1 and
 * key=u2, locates the PENDING slot the kick reserved, and installs the pipeline
 * (READY) or marks the slot FAILED. Fires during the wgpu_end_frame ProcessEvents
 * drain. The whole apparatus is compiled OUT on native, where the synchronous
 * path makes a pipeline ready the instant it is created. */
static void on_pipeline_ready(WGPUCreatePipelineAsyncStatus status,
                              WGPURenderPipeline pipeline,
                              WGPUStringView message,
                              void *u1, void *u2) {
    struct ShaderProgram *prg = (struct ShaderProgram *)u1;
    uint32_t key = (uint32_t)(uintptr_t)u2;
    if (s_pending_pipelines > 0) {
        s_pending_pipelines--;
    }
    /* Find the PENDING slot this create was kicked for. At most one PENDING entry
     * per key exists: the kick reserves it, and every later draw with the same key
     * finds it in the lookup and returns NULL without re-kicking. */
    struct WgpuPipeEntry *e = NULL;
    for (int i = 0; i < prg->npipes; i++) {
        if (prg->pipes[i].key == key && prg->pipes[i].state == WGPU_PIPE_PENDING) {
            e = &prg->pipes[i];
            break;
        }
    }
    if (e == NULL) {
        /* Slot evicted before the compile finished — unreachable in practice
         * (per-combiner combos << WGPU_PIPE_CACHE). Release the orphan pipeline so
         * it cannot leak. */
        if (pipeline != NULL) {
            wgpuRenderPipelineRelease(pipeline);
        }
        return;
    }
    if (status == WGPUCreatePipelineAsyncStatus_Success && pipeline != NULL) {
        e->pipe = pipeline;
        e->state = WGPU_PIPE_READY;
    } else {
        /* Stays FAILED: the lookup keeps returning NULL for this key, so a create
         * that already failed is never re-kicked (avoids a per-draw create storm).
         * A failed create means an invalid descriptor, which would fail
         * synchronously too — pop-in is the only available outcome. */
        e->pipe = NULL;
        e->state = WGPU_PIPE_FAILED;
        static int s_pipe_fail_logged = 0;
        if (s_pipe_fail_logged < 8) {
            s_pipe_fail_logged++;
            fprintf(stderr,
                    "[webgpu] async pipeline create failed (key=0x%02x status=%d): %.*s\n",
                    (unsigned)key, (int)status,
                    (int)message.length, message.data ? message.data : "");
        }
    }
}
#endif  /* __EMSCRIPTEN__ */

/* Lazily build + cache the render pipeline for the current (shader, blend, depth)
 * dynamic state — WebGPU bakes all of it into the pipeline (mtl_pso_for shape).
 *
 * PERF-005: the descriptor half is factored into wgpu_fill_pipeline_desc so the
 * sync and async create paths build a bit-identical descriptor. On web-live
 * (non-deterministic) a cache miss kicks an async create and returns NULL —
 * wgpu_draw_triangles skips the batch this frame (transient pop-in) until the
 * pipeline lands, eliminating the first-sight synchronous compile hitch. Native,
 * and web under --deterministic, keep the synchronous create so every byte-exact
 * gate (parity/screenshot/tape, all --deterministic) stays on the identical HEAD
 * path. */
static WGPURenderPipeline wgpu_pipeline_for(struct ShaderProgram *prg, enum GfxBlendMode blend) {
    if (prg->module == NULL) {
        return NULL;
    }
    bool decal = wgpu_depth_is_decal();
    uint32_t key = (uint32_t)blend
                 | ((uint32_t)(s_depth_test ? 1 : 0)    << 4)
                 | ((uint32_t)(s_depth_update ? 1 : 0)  << 5)
                 | ((uint32_t)(s_depth_compare ? 1 : 0) << 6)
                 | ((uint32_t)(decal ? 1 : 0)           << 7)
                 | ((uint32_t)(wgpu_preserve_cov_alpha(blend) ? 1 : 0) << 8);
    for (int i = 0; i < prg->npipes; i++) {
        if (prg->pipes[i].key == key) {
            struct WgpuPipeEntry *e = &prg->pipes[i];
            if (e->state == WGPU_PIPE_READY) {
                return e->pipe;
            }
            /* PENDING (async create in flight) or FAILED: skip the batch this
             * frame. Returning here — rather than falling through to the miss
             * path — is what prevents re-kicking a create for a key already in
             * flight (or one that failed). Native never stores a non-READY entry,
             * so it never reaches this and its behavior is unchanged from HEAD.
             * PERF-005b: a PENDING skip marks the frame visually incomplete so
             * wgpu_end_frame withholds its present; FAILED does not (permanent). */
            if (e->state == WGPU_PIPE_PENDING) {
                s_frame_pending_skips++;
            }
            return NULL;
        }
    }

    /* Cache miss. PERF-005 Phase 2: record this (shader,key) for the current stage
     * so a future load screen can prewarm it (both the sync and async create below
     * are genuine first-sight CREATEs — this is the one place to capture them). */
    wgpu_prewarm_record(prg->shader_id0, prg->shader_id1, key);

    /* Build the descriptor ONCE via the shared helper; both the sync and async
     * paths below submit this exact descriptor. */
    struct WgpuPipeScratch sc;
    WGPURenderPipelineDescriptor pd;
    wgpu_fill_pipeline_desc(prg, key, &pd, &sc);

#ifdef __EMSCRIPTEN__
    /* Web-live async path — gated OFF under --deterministic so every recorded
     * gate stays on the synchronous path. Native never compiles this block (the
     * async create/callback API has no native test coverage), so native is always
     * synchronous and byte-for-byte HEAD. Reserve a PENDING slot FIRST so
     * subsequent draws for this key hit the lookup above and do NOT re-kick, then
     * fire the async create and skip this batch this frame. */
    extern int g_deterministic;
    if (!g_deterministic) {
        int slot = wgpu_pipe_reserve_slot(prg);
        prg->pipes[slot].key = key;
        prg->pipes[slot].pipe = NULL;
        prg->pipes[slot].state = WGPU_PIPE_PENDING;
        s_pending_pipelines++;
        WGPUCreateRenderPipelineAsyncCallbackInfo cb = {0};
        cb.mode = WGPUCallbackMode_AllowSpontaneous;
        cb.callback = on_pipeline_ready;
        cb.userdata1 = prg;
        cb.userdata2 = (void *)(uintptr_t)key;
        wgpuDeviceCreateRenderPipelineAsync(s_device, &pd, cb);
        s_frame_pending_skips++;   /* PERF-005b: this batch is missing from the frame */
        return NULL;
    }
#endif

    /* Synchronous create: native always; web under --deterministic. Identical to
     * the pre-PERF-005 behavior (a failed create stores nothing and is retried on
     * the next draw), now stamping the entry's state READY. */
    WGPURenderPipeline pipe = wgpuDeviceCreateRenderPipeline(s_device, &pd);
    if (pipe != NULL) {
        int slot = wgpu_pipe_reserve_slot(prg);
        prg->pipes[slot].key = key;
        prg->pipes[slot].pipe = pipe;
        prg->pipes[slot].state = WGPU_PIPE_READY;
    }
    return pipe;
}

/* PERF-005 Phase 2: switch the recorder to `stage`. Flushes the PREVIOUS stage's
 * manifest first if it grew during play (persist at the transition, not per-create).
 * Called from boss.c on every stage (re)load, adjacent to gfx_webgpu_prewarm_stage.
 * A near no-op when prewarm is disabled (deterministic / GE007_PIPECACHE=0) or when
 * the webgpu backend isn't the active one (nothing ever records, so nothing flushes).
 * Declared in gfx_webgpu.h. */
void gfx_webgpu_set_stage(int stage) {
    if (!wgpu_prewarm_enabled()) {
        s_prewarm_cur_stage = stage;   /* harmless; record() is a no-op when disabled */
        return;
    }
    wgpu_prewarm_ensure_loaded();
    if (s_prewarm_dirty) {
        wgpu_prewarm_flush();          /* persist the stage we are leaving */
    }
    s_prewarm_cur_stage = stage;
}

/* PERF-005 Phase 2: synchronously build every pipeline recorded for `stage` on a
 * prior visit/session, so its materials are already READY before the first draw —
 * no cold-material pop-in / present-hold on entry. Runs inside boss.c's load window
 * (watchdog suppressed, load screen up); the creates are the POINT of that window.
 *
 * IMPORTANT (Asyncify, PERF-031): this must NOT suspend. wgpuDeviceCreateRenderPipeline
 * (sync) and the shader-module create it may trigger are non-suspending on both
 * dialects — the only suspending calls are adapter/device bring-up pumps, which are
 * absent here. Do NOT add emscripten_sleep / ProcessEvents in this path.
 *
 * Bit-identical guarantee: the descriptor is built through the SHARED
 * wgpu_fill_pipeline_desc, so a prewarmed pipeline is byte-for-byte what the draw
 * would have created for the same key — no async/sync render divergence.
 * Declared in gfx_webgpu.h. */
void gfx_webgpu_prewarm_stage(int stage) {
    if (!wgpu_prewarm_enabled()) {
        return;
    }
    if (!s_ready || s_device == NULL) {
        return;   /* device not up yet — records still accrue; warm on the next visit */
    }
    wgpu_prewarm_ensure_loaded();

    /* wgpu_create_and_load_new_shader sets s_cur_shader as a side effect; prewarm
     * must not perturb the draw path's current-shader state, so snapshot + restore. */
    struct ShaderProgram *saved_cur = s_cur_shader;
    int considered = 0, warmed = 0, shaders_built = 0;

    for (int i = 0; i < s_prewarm_n; i++) {
        if (s_prewarm_recs[i].stage != stage) {
            continue;
        }
        considered++;
        uint64_t id0 = s_prewarm_recs[i].id0;
        uint32_t id1 = s_prewarm_recs[i].id1;
        uint32_t key = s_prewarm_recs[i].key;

        struct ShaderProgram *prg = wgpu_lookup_shader(id0, id1);
        if (prg == NULL) {
            /* Shader not created yet (its first draw hasn't happened). Build it now
             * — pure function of the ids (WGSL derived from the combiner encoding),
             * no gfx_pc render state needed. This is the crux that makes second-launch
             * cold-warming possible. */
            prg = wgpu_create_and_load_new_shader(id0, id1);
            if (prg != NULL && prg->module != NULL) {
                shaders_built++;
            }
        }
        if (prg == NULL || prg->module == NULL) {
            continue;   /* inert program (WGSL build failed) — skip, draw path guards too */
        }

        /* Already READY (prewarmed earlier this session, or created by a draw)? skip. */
        bool have = false;
        for (int j = 0; j < prg->npipes; j++) {
            if (prg->pipes[j].key == key && prg->pipes[j].state == WGPU_PIPE_READY) {
                have = true;
                break;
            }
        }
        if (have) {
            continue;
        }

        struct WgpuPipeScratch sc;
        WGPURenderPipelineDescriptor pd;
        wgpu_fill_pipeline_desc(prg, key, &pd, &sc);
        WGPURenderPipeline pipe = wgpuDeviceCreateRenderPipeline(s_device, &pd);   /* SYNC */
        if (pipe != NULL) {
            int slot = wgpu_pipe_reserve_slot(prg);
            prg->pipes[slot].key = key;
            prg->pipes[slot].pipe = pipe;
            prg->pipes[slot].state = WGPU_PIPE_READY;
            warmed++;
        }
    }

    s_cur_shader = saved_cur;

    if (port_env_bool("GE007_PIPECACHE_TRACE", 0,
            "PERF-005 Phase 2: log record/replay pipeline prewarm counts at level load")) {
        fprintf(stderr, "[webgpu] pipecache prewarm stage %d: warmed %d pipeline(s), "
                        "built %d shader(s), %d recorded\n",
                stage, warmed, shaders_built, considered);
        fflush(stderr);
    }
}

/* ------------------------------------------------------------------------
 * Vtable: textures + samplers (Task 2)
 *
 * The N64 interpreter's model (mirroring gfx_opengl.c): new_texture() hands out
 * an opaque id; select_texture(tile, id) makes that id current on a tile;
 * upload_texture(rgba,w,h) uploads into the id current on the most-recently
 * selected tile; set_sampler_parameters(tile,...) sets the filter/wrap for that
 * tile. WebGPU has no global bind state, so textures + samplers are looked up
 * by id and staged per-tile here; the draw path (Task 3) binds tile 0/1's view
 * + sampler into a bind group at submit time.
 *
 * Ids are recycled through a free-list over a growable array (like glGenTextures
 * reuse), so live GPU resources stay bounded by the interpreter's texture cache
 * (~1024) rather than growing with monotonic ids over a long session.
 * ---------------------------------------------------------------------- */
#define WGPU_TX_MIRROR 0x1u   /* G_TX_MIRROR (PR/gbi.h) — mirrored here to avoid
                                 pulling the N64 GBI headers into this TU */
#define WGPU_TX_CLAMP  0x2u   /* G_TX_CLAMP */

/* PERF-019: per-texture reverse index into the bind-group cache. Each entry records
 * the bg-cache slot indices whose key references THIS texture's current `view` (in
 * key slot 1 or 3). Releasing the view then walks only these candidate slots instead
 * of sweeping all WGPU_BG_CACHE (512) entries — the level-transition delete storm
 * (gfx_clear_texture_cache deletes every pooled texture) drops from texture_count×512
 * to texture_count×refs. The list is a conservative SUPERSET: a slot round-robin-
 * evicted+reused after registration stays listed (a stale candidate) but is filtered
 * at release by re-checking the exact same pointer predicate the full sweep uses, so
 * the invalidation DECISION is byte-identical (WEB-068 ABA discipline preserved). On
 * overflow (a texture referenced by more than WGPU_TEX_BG_REFS distinct slots) the
 * entry falls back to the exact full sweep — always correct, just not accelerated. */
#define WGPU_TEX_BG_REFS 16
struct WgpuTexEntry {
    WGPUTexture     tex;
    WGPUTextureView view;
    int             w, h;
    bool            used;
    bool            bg_ref_overflow;                /* list overflowed → release full-sweeps */
    uint8_t         bg_ref_n;                       /* live candidate count */
    uint16_t        bg_refs[WGPU_TEX_BG_REFS];      /* bg-cache slot indices (0..WGPU_BG_CACHE-1) */
};
static struct WgpuTexEntry *s_tex = NULL;   /* indexed by (id - 1) */
static uint32_t s_tex_cap = 0;
static uint32_t s_tex_hi = 0;               /* highest id ever allocated */
static uint32_t *s_tex_free = NULL;         /* stack of freed ids for reuse */
static uint32_t s_tex_free_n = 0, s_tex_free_cap = 0;

/* PERF-019: register a bg-cache slot as referencing this texture's view. Called from
 * the draw path when a bind group is (re)built. Deduped so a slot re-inserted for the
 * same view after an eviction is not double-listed; on a full list the entry flips to
 * overflow (→ full sweep on release) rather than dropping a reference (which would
 * under-invalidate — the ABA correctness bug). NULL entry = the white fallback view,
 * which is never released, so it needs no reverse index. */
static void wgpu_tex_bg_ref_add(struct WgpuTexEntry *e, uint32_t slot) {
    if (e == NULL || e->bg_ref_overflow) {
        return;
    }
    for (uint8_t i = 0; i < e->bg_ref_n; i++) {
        if (e->bg_refs[i] == (uint16_t)slot) {
            return;   /* already listed */
        }
    }
    if (e->bg_ref_n >= WGPU_TEX_BG_REFS) {
        e->bg_ref_overflow = true;
        return;
    }
    e->bg_refs[e->bg_ref_n++] = (uint16_t)slot;
}

/* PERF-019: invalidate every cached bind group that references this texture's view,
 * using the reverse index instead of a full 512-entry sweep. Must be called BEFORE
 * the view's C-handle is released (reads e->view). Semantics are identical to
 * wgpu_bg_cache_invalidate_view(e->view): the per-slot drop predicate is byte-for-byte
 * the same, so no referencing entry survives (WEB-068). Clears the list afterward —
 * the old view is gone; a recreated texture starts with an empty index. */
static void wgpu_bg_cache_invalidate_view_indexed(struct WgpuTexEntry *e) {
    if (e == NULL || e->view == NULL) {
        return;
    }
    if (e->bg_ref_overflow) {
        wgpu_bg_cache_invalidate_view(e->view);   /* list overflowed — exact full sweep */
    } else {
        const void *v = (const void *)e->view;
        for (uint8_t i = 0; i < e->bg_ref_n; i++) {
            struct WgpuBgEntry *slot = &s_bg_cache_tab[e->bg_refs[i]];
            /* Re-verify against the SAME predicate the full sweep uses: a slot may have
             * been evicted+reused since registration, so drop it only if it still
             * references v (keeps the decision identical to the full sweep). */
            if (slot->bg != NULL &&
                (slot->key[1] == v || slot->key[3] == v || slot->key[5] == v)) {
                wgpuBindGroupRelease(slot->bg);
                slot->bg = NULL;
                memset(slot->key, 0, sizeof(slot->key));
            }
        }
    }
    e->bg_ref_n = 0;
    e->bg_ref_overflow = false;
}

/* Per-tile binding staged by select_texture / set_sampler_parameters and read by
 * the Task 3 draw path. */
static uint32_t    s_bound_tex[2] = {0, 0};
static WGPUSampler s_bound_sampler[2] = {NULL, NULL};
static int         s_active_tile = 0;       /* tile of the last select_texture */

/* Sampler cache keyed by (linear, cms, cmt). Small: the N64 uses a handful of
 * distinct (filter, wrap) combinations. */
struct WgpuSamplerEntry { int linear; uint32_t cms, cmt; WGPUSampler sampler; };
static struct WgpuSamplerEntry s_samplers[64];
static int s_sampler_n = 0;

static WGPUAddressMode wgpu_cm_to_address(uint32_t cm) {
    if (cm & WGPU_TX_CLAMP) {
        return WGPUAddressMode_ClampToEdge;
    }
    return (cm & WGPU_TX_MIRROR) ? WGPUAddressMode_MirrorRepeat : WGPUAddressMode_Repeat;
}

static struct WgpuTexEntry *wgpu_tex_lookup(uint32_t id) {
    if (id == 0 || id > s_tex_hi) {
        return NULL;
    }
    struct WgpuTexEntry *e = &s_tex[id - 1];
    return e->used ? e : NULL;
}

static uint32_t wgpu_new_texture(void) {
    uint32_t id;
    if (s_tex_free_n > 0) {
        id = s_tex_free[--s_tex_free_n];
    } else {
        id = ++s_tex_hi;
        if (id > s_tex_cap) {
            uint32_t ncap = s_tex_cap ? s_tex_cap * 2 : 256;
            struct WgpuTexEntry *n = (struct WgpuTexEntry *)realloc(s_tex, ncap * sizeof(*s_tex));
            if (n == NULL) {
                --s_tex_hi;
                return 0;   /* allocation failure — interpreter treats 0 as none */
            }
            memset(n + s_tex_cap, 0, (ncap - s_tex_cap) * sizeof(*s_tex));
            s_tex = n;
            s_tex_cap = ncap;
        }
    }
    struct WgpuTexEntry *e = &s_tex[id - 1];
    e->tex = NULL;
    e->view = NULL;
    e->w = e->h = 0;
    e->used = true;
    e->bg_ref_n = 0;             /* PERF-019: fresh id owns no cached bind groups yet */
    e->bg_ref_overflow = false;
    return id;
}

static void wgpu_delete_texture(uint32_t texture_id) {
    struct WgpuTexEntry *e = wgpu_tex_lookup(texture_id);
    if (e == NULL) {
        return;
    }
    if (e->view != NULL) {
        wgpu_bg_cache_invalidate_view_indexed(e);   /* PERF-019/WEB-068: purge stale cache refs (reverse index) */
        wgpuTextureViewRelease(e->view); e->view = NULL;
    }
    if (e->tex != NULL)  { wgpuTextureRelease(e->tex);      e->tex = NULL; }
    e->used = false;
    if (s_tex_free_n >= s_tex_free_cap) {
        uint32_t ncap = s_tex_free_cap ? s_tex_free_cap * 2 : 256;
        uint32_t *n = (uint32_t *)realloc(s_tex_free, ncap * sizeof(*n));
        if (n == NULL) {
            return;   /* drop the id (never reused); GPU resource already freed */
        }
        s_tex_free = n;
        s_tex_free_cap = ncap;
    }
    s_tex_free[s_tex_free_n++] = texture_id;
}

static void wgpu_select_texture(int tile, uint32_t texture_id) {
    if (tile < 0 || tile > 1) {
        return;
    }
    s_active_tile = tile;
    s_bound_tex[tile] = texture_id;
}

static bool wgpu_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
    if (!s_ready || rgba32_buf == NULL || width <= 0 || height <= 0 ||
        (uint32_t)width > s_max_tex_dim || (uint32_t)height > s_max_tex_dim) {
        return false;
    }
    struct WgpuTexEntry *e = wgpu_tex_lookup(s_bound_tex[s_active_tile]);
    if (e == NULL) {
        return false;
    }

    /* The copy descriptor is identical for the in-place and recreate paths. */
    WGPUTexelCopyTextureInfo dst = {0};
    dst.mipLevel = 0;
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout = {0};
    layout.offset = 0;
    layout.bytesPerRow = (uint32_t)width * 4u;
    layout.rowsPerImage = (uint32_t)height;
    WGPUExtent3D ext = { (uint32_t)width, (uint32_t)height, 1 };
    const size_t bytes = (size_t)width * (size_t)height * 4u;

    /* WEB-053: a re-upload whose dimensions match the live texture (the format
     * is always RGBA8Unorm here) writes into the existing texture in place
     * instead of destroy+recreate. Keeps the WGPUTexture AND its view — so any
     * cached draw bind groups that reference the view stay valid and warm —
     * eliminating per-frame texture churn for animated/streamed surfaces.
     * ORDERING INVARIANT (final-review F3): wgpuQueueWriteTexture executes
     * BEFORE the frame's command buffer, so a same-id re-upload issued after
     * an earlier draw in the SAME frame would retroactively swap that draw's
     * texels (the old destroy+recreate path pinned the old texture via the
     * bind group and was safe by construction). Currently unreachable — the
     * TMEM cache hits on unchanged content and eviction deletes rather than
     * recycles ids — but any future caching change that re-uploads a drawn-
     * this-frame id must go back to recreate (or defer the write). */
    if (e->tex != NULL && e->view != NULL && e->w == width && e->h == height) {
        dst.texture = e->tex;
        wgpuQueueWriteTexture(s_queue, &dst, rgba32_buf, bytes, &layout, &ext);
        return true;
    }

    /* Dimensions changed (or first upload into this id): drop the old GPU
     * resources and (re)create the texture at the new size. */
    if (e->view != NULL) {
        wgpu_bg_cache_invalidate_view_indexed(e);   /* PERF-019/WEB-068: purge stale cache refs (reverse index) */
        wgpuTextureViewRelease(e->view); e->view = NULL;
    }
    if (e->tex != NULL)  { wgpuTextureRelease(e->tex);      e->tex = NULL; }

    WGPUTextureDescriptor td = {0};
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.size.width = (uint32_t)width;
    td.size.height = (uint32_t)height;
    td.size.depthOrArrayLayers = 1;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    e->tex = wgpuDeviceCreateTexture(s_device, &td);
    if (e->tex == NULL) {
        e->w = e->h = 0;
        return false;
    }
    dst.texture = e->tex;
    wgpuQueueWriteTexture(s_queue, &dst, rgba32_buf, bytes, &layout, &ext);

    e->view = wgpuTextureCreateView(e->tex, NULL);
    e->w = width;
    e->h = height;
    return e->view != NULL;
}

static WGPUSampler wgpu_get_sampler(bool linear_filter, uint32_t cms, uint32_t cmt) {
    for (int i = 0; i < s_sampler_n; ++i) {
        if (s_samplers[i].linear == (int)linear_filter &&
            s_samplers[i].cms == cms && s_samplers[i].cmt == cmt) {
            return s_samplers[i].sampler;
        }
    }
    WGPUSamplerDescriptor sd = {0};
    sd.addressModeU = wgpu_cm_to_address(cms);
    sd.addressModeV = wgpu_cm_to_address(cmt);
    sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = linear_filter ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
    sd.minFilter = linear_filter ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
    sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;   /* single-level like the GL path */
    sd.lodMinClamp = 0.0f;
    sd.lodMaxClamp = 0.0f;
    sd.maxAnisotropy = 1;
    /* Video.AnisotropicFiltering (remaster): resolve grazing-angle texture streak.
     * WebGPU requires all-Linear filters when maxAnisotropy>1, so only the linear
     * samplers are upgraded; nearest/point materials stay exactly as the N64 path.
     * gfx_pc.c gives 3-point (bilerp) materials a linear sampler when aniso is on,
     * and the WGSL generator emits hardware textureSample for them so this engages. */
    if (linear_filter && g_pcTextureAnisotropy > 1) {
        int aniso = g_pcTextureAnisotropy > 16 ? 16 : g_pcTextureAnisotropy;
        sd.mipmapFilter = WGPUMipmapFilterMode_Linear;
        sd.maxAnisotropy = (uint16_t)aniso;
    }
    WGPUSampler s = wgpuDeviceCreateSampler(s_device, &sd);
    if (s == NULL) {
        return NULL;
    }
    if (s_sampler_n < (int)(sizeof(s_samplers) / sizeof(s_samplers[0]))) {
        s_samplers[s_sampler_n].linear = (int)linear_filter;
        s_samplers[s_sampler_n].cms = cms;
        s_samplers[s_sampler_n].cmt = cmt;
        s_samplers[s_sampler_n].sampler = s;
        s_sampler_n++;
    } else {
        /* Cache full (the N64 uses only a handful of filter/wrap combos, so this
         * is not expected): release rather than leak, and reuse slot 0's sampler
         * for this call so the draw still binds something valid. */
        static bool warned = false;
        if (!warned) { fprintf(stderr, "[webgpu] sampler cache full (%d)\n", s_sampler_n); warned = true; }
        wgpuSamplerRelease(s);
        return s_samplers[0].sampler;
    }
    return s;
}

static void wgpu_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    if (tile < 0 || tile > 1 || !s_ready) {
        return;
    }
    s_bound_sampler[tile] = wgpu_get_sampler(linear_filter, cms, cmt);
}

/* ------------------------------------------------------------------------
 * Vtable: pipeline state (stubs — baked into pipelines in Tasks 3/4)
 * ---------------------------------------------------------------------- */
static void wgpu_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare,
                                bool depth_source_prim, uint16_t zmode) {
    (void)depth_source_prim;
    s_depth_test = depth_test;
    s_depth_update = depth_update;
    s_depth_compare = depth_compare;
    s_zmode = zmode;
}
static void wgpu_set_viewport(int x, int y, int width, int height) {
    s_vp_x = x; s_vp_y = y; s_vp_w = width; s_vp_h = height;
}
static void wgpu_set_scissor(int x, int y, int width, int height) {
    s_sc_x = x; s_sc_y = y; s_sc_w = width; s_sc_h = height; s_sc_set = true;
}
static void wgpu_set_blend_mode(enum GfxBlendMode mode) { s_cur_blend = mode; }

/* Lazily create the draw fallbacks (1x1 white texture, nearest sampler) + the
 * per-frame bump vertex buffer (declared with the frame state above). Each draw
 * appends its buf_vbo at a fresh offset so all draws in the frame's single
 * render pass read distinct data (queue writes are ordered before submit). */
static void wgpu_ensure_draw_resources(void) {
    if (s_white_view == NULL) {
        WGPUTextureDescriptor td = {0};
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D;
        td.size.width = 1; td.size.height = 1; td.size.depthOrArrayLayers = 1;
        td.format = WGPUTextureFormat_RGBA8Unorm;
        td.mipLevelCount = 1; td.sampleCount = 1;
        s_white_tex = wgpuDeviceCreateTexture(s_device, &td);
        if (s_white_tex != NULL) {
            uint8_t white[4] = {255, 255, 255, 255};
            WGPUTexelCopyTextureInfo dst = {0};
            dst.texture = s_white_tex; dst.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyBufferLayout lay = {0};
            lay.bytesPerRow = 4; lay.rowsPerImage = 1;
            WGPUExtent3D ext = {1, 1, 1};
            wgpuQueueWriteTexture(s_queue, &dst, white, 4, &lay, &ext);
            s_white_view = wgpuTextureCreateView(s_white_tex, NULL);
        }
    }
    if (s_default_sampler == NULL) {
        WGPUSamplerDescriptor sd = {0};
        sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_Repeat;
        sd.magFilter = sd.minFilter = WGPUFilterMode_Nearest;
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        sd.maxAnisotropy = 1;
        s_default_sampler = wgpuDeviceCreateSampler(s_device, &sd);
    }
    if (s_vbuf == NULL) {
        WGPUBufferDescriptor bd = {0};
        bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        bd.size = WGPU_VBUF_CAP;
        s_vbuf = wgpuDeviceCreateBuffer(s_device, &bd);
        /* WEB-023: shadow the vertex buffer CPU-side (calloc → padding gaps defined).
         * NULL is tolerated — wgpu_vbuf_stage falls back to per-batch writeBuffer. */
        if (s_vbuf != NULL && s_vbuf_shadow == NULL) {
            s_vbuf_shadow = (uint8_t *)calloc(1, WGPU_VBUF_CAP);
        }
    }
}

/* WEB-023: stage a batch's vertex bytes into the CPU shadow (uploaded once in
 * wgpu_end_frame). Falls back to an immediate per-batch queue write when the shadow
 * malloc failed. voff/bytes are already validated against WGPU_VBUF_CAP by the
 * caller's bump-allocator guard, so the memcpy is in-bounds. */
static void wgpu_vbuf_stage(uint32_t voff, const void *src, uint32_t bytes) {
    if (s_vbuf_shadow != NULL) {
        memcpy(s_vbuf_shadow + voff, src, bytes);
    } else {
        wgpuQueueWriteBuffer(s_queue, s_vbuf, voff, src, bytes);
    }
}

/* RDP memory-blend support: split the open scene pass and copy the scene target
 * into the snapshot texture so the next draw's shader can sample the current
 * "memory color" (WebGPU cannot read the render target inside its own pass).
 * The resumed pass Loads both color and depth — no state is lost; viewport,
 * scissor, pipeline and bind group are re-applied per draw anyway. This is the
 * per-batch analogue of gfx_opengl.c's glCopyTexSubImage2D snapshot (one copy
 * per same-material XLU batch, PERFORMANCE_PLAN.md M1). */
/* Compute the batch's screen-space bounding rect (top-down texture coords,
 * padded for the ±0.5px coverage taps) from the interleaved VBO's clip
 * positions. Returns false when any vertex is un-projectable (w<=0) — the
 * caller then copies the whole target. Ports the intent of gfx_opengl.c's
 * gfx_opengl_compute_batch_snapshot_rect: bound the snapshot copy to the
 * pixels the batch can actually sample instead of the full scene target
 * (a full copy at fullscreen retina is tens of MB per glass/fence batch —
 * a measured 1%-low contributor). */
static bool wgpu_batch_snapshot_rect(const float *buf_vbo, size_t buf_vbo_len,
                                     int stride_floats,
                                     uint32_t *out_x, uint32_t *out_y,
                                     uint32_t *out_w, uint32_t *out_h) {
    if (buf_vbo == NULL || stride_floats < 4 || buf_vbo_len < (size_t)stride_floats) {
        return false;
    }
    float min_px = 1e30f, max_px = -1e30f;
    float min_gl = 1e30f, max_gl = -1e30f;   /* bottom-up pixel y */
    for (size_t off = 0; off + 4 <= buf_vbo_len; off += (size_t)stride_floats) {
        float w = buf_vbo[off + 3];
        if (!(w > 0.0f)) {
            return false;   /* behind the eye — bail to the full-target copy */
        }
        float ndc_x = buf_vbo[off + 0] / w;
        float ndc_y = buf_vbo[off + 1] / w;
        float px = (float)s_vp_x + (ndc_x + 1.0f) * 0.5f * (float)s_vp_w;
        float py = (float)s_vp_y + (ndc_y + 1.0f) * 0.5f * (float)s_vp_h;
        if (px < min_px) min_px = px;
        if (px > max_px) max_px = px;
        if (py < min_gl) min_gl = py;
        if (py > max_gl) max_gl = py;
    }
    if (min_px > max_px || min_gl > max_gl) {
        return false;
    }
    /* Pad for the 8-tap coverage offsets + rounding, flip to top-down, clamp. */
    int x0 = (int)min_px - 2;
    int x1 = (int)max_px + 3;
    int y0 = (int)s_scene_h - ((int)max_gl + 3);   /* top-down top edge */
    int y1 = (int)s_scene_h - ((int)min_gl - 2);   /* top-down bottom edge */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)s_scene_w) x1 = (int)s_scene_w;
    if (y1 > (int)s_scene_h) y1 = (int)s_scene_h;
    if (x1 <= x0 || y1 <= y0) {
        return false;   /* fully clipped — nothing the shader can sample */
    }
    *out_x = (uint32_t)x0;
    *out_y = (uint32_t)y0;
    *out_w = (uint32_t)(x1 - x0);
    *out_h = (uint32_t)(y1 - y0);
    return true;
}

static bool wgpu_snapshot_scene_for_memory_blend(const float *buf_vbo,
                                                 size_t buf_vbo_len,
                                                 int stride_floats) {
    if (s_encoder == NULL || s_pass == NULL || s_scene_tex == NULL ||
        s_scene_w == 0 || s_scene_h == 0) {
        return false;
    }
    if (s_snap_view == NULL || s_snap_w != s_scene_w || s_snap_h != s_scene_h) {
        if (s_snap_view != NULL) {
            wgpu_bg_cache_invalidate_view(s_snap_view);   /* WEB-068: snapshot lives in key slot 5 */
            wgpuTextureViewRelease(s_snap_view); s_snap_view = NULL;
        }
        if (s_snap_tex != NULL)  { wgpuTextureRelease(s_snap_tex);      s_snap_tex = NULL; }
        WGPUTextureDescriptor td = {0};
        td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
        td.dimension = WGPUTextureDimension_2D;
        td.size.width = s_scene_w; td.size.height = s_scene_h;
        td.size.depthOrArrayLayers = 1;
        td.format = s_surface_format;   /* must match the scene for T2T copy */
        td.mipLevelCount = 1; td.sampleCount = 1;
        s_snap_tex = wgpuDeviceCreateTexture(s_device, &td);
        s_snap_view = s_snap_tex ? wgpuTextureCreateView(s_snap_tex, NULL) : NULL;
        s_snap_w = s_scene_w; s_snap_h = s_scene_h;
    }
    if (s_snap_view == NULL) {
        return false;
    }
    if (s_snap_sampler == NULL) {
        WGPUSamplerDescriptor sd = {0};
        sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_ClampToEdge;
        sd.magFilter = sd.minFilter = WGPUFilterMode_Nearest;   /* GL snapshot is GL_NEAREST */
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        sd.maxAnisotropy = 1;
        s_snap_sampler = wgpuDeviceCreateSampler(s_device, &sd);
        if (s_snap_sampler == NULL) {
            return false;
        }
    }

    wgpuRenderPassEncoderEnd(s_pass);
    wgpuRenderPassEncoderRelease(s_pass);
    s_pass = NULL;

    /* Copy only the batch's padded screen rect when computable (same origin in
     * src and dst keeps the snapshot 1:1 with the target, so the shader's
     * fragcoord-based memoryUv stays valid); whole target as the fallback. */
    uint32_t rx = 0, ry = 0, rw = s_scene_w, rh = s_scene_h;
    (void)wgpu_batch_snapshot_rect(buf_vbo, buf_vbo_len, stride_floats,
                                   &rx, &ry, &rw, &rh);
    WGPUTexelCopyTextureInfo cs = {0};
    cs.texture = s_scene_tex; cs.aspect = WGPUTextureAspect_All;
    cs.origin.x = rx; cs.origin.y = ry;
    WGPUTexelCopyTextureInfo cd = {0};
    cd.texture = s_snap_tex; cd.aspect = WGPUTextureAspect_All;
    cd.origin.x = rx; cd.origin.y = ry;
    WGPUExtent3D ext = { rw, rh, 1 };
    wgpuCommandEncoderCopyTextureToTexture(s_encoder, &cs, &cd, &ext);

    WGPURenderPassColorAttachment att = {0};
    att.view = s_scene_view;
    att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    att.loadOp = WGPULoadOp_Load;
    att.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDepthStencilAttachment depth = {0};
    depth.view = s_depth_view;
    depth.depthLoadOp = WGPULoadOp_Load;
    depth.depthStoreOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor rp = {0};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &att;
    rp.depthStencilAttachment = &depth;
    s_pass = wgpuCommandEncoderBeginRenderPass(s_encoder, &rp);
    /* WEB-023-lite: a NEW pass encoder inherits none of the previous pass's
     * viewport/scissor — the next draw must re-apply, so clear the dedup flags. */
    wgpu_reset_pass_dynamic_state();
    return s_pass != NULL;
}

/* Create a lazily-allocated 16-byte uniform buffer (or return the existing one).
 * Shared by the ring and overflow slots. */
static WGPUBuffer wgpu_diag_ubo_alloc(WGPUBuffer existing) {
    if (existing != NULL) {
        return existing;
    }
    WGPUBufferDescriptor bd = {0};
    bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    bd.size = 16;
    return wgpuDeviceCreateBuffer(s_device, &bd);
}

/* Fetch (or write) a 16-byte viewport UBO for the coverage-wrap shader. The
 * value is the GL-convention viewport exactly as gfx_opengl.c uploads it
 * (uDiagViewport = glGetIntegerv(GL_VIEWPORT) = bottom-up origin).
 *
 * WEB-051: every distinct value this frame gets its own buffer. The common path
 * (<= WGPU_DIAG_UBO_RING distinct viewports; usually 1) is allocation-free. On
 * overflow we GROW a heap array of extra buffers instead of overwriting an
 * occupied slot — wgpuQueueWriteBuffer runs before the command buffer, so
 * reusing a slot already referenced by an earlier draw would retroactively
 * rewrite that draw's viewport. */
static WGPUBuffer wgpu_diag_viewport_ubo(void) {
    float vp[4] = { (float)s_vp_x, (float)s_vp_y, (float)s_vp_w, (float)s_vp_h };
    /* Dedup against everything written this frame (ring first, then overflow). */
    for (int i = 0; i < s_diag_ubo_used; i++) {
        if (i < WGPU_DIAG_UBO_RING) {
            if (s_diag_ubo[i] != NULL && memcmp(s_diag_ubo_val[i], vp, sizeof(vp)) == 0) {
                return s_diag_ubo[i];
            }
        } else {
            int j = i - WGPU_DIAG_UBO_RING;
            if (s_diag_ubo_ext[j].buf != NULL &&
                memcmp(s_diag_ubo_ext[j].val, vp, sizeof(vp)) == 0) {
                return s_diag_ubo_ext[j].buf;
            }
        }
    }

    WGPUBuffer *pbuf;
    float      *pval;
    if (s_diag_ubo_used < WGPU_DIAG_UBO_RING) {
        pbuf = &s_diag_ubo[s_diag_ubo_used];
        pval = s_diag_ubo_val[s_diag_ubo_used];
    } else {
        int j = s_diag_ubo_used - WGPU_DIAG_UBO_RING;
        if (j >= s_diag_ubo_ext_cap) {
            int ncap = s_diag_ubo_ext_cap ? s_diag_ubo_ext_cap * 2 : WGPU_DIAG_UBO_RING;
            struct WgpuDiagUbo *n =
                (struct WgpuDiagUbo *)realloc(s_diag_ubo_ext, (size_t)ncap * sizeof(*n));
            if (n == NULL) {
                return NULL;   /* grow failed: skip the viewport UBO this draw */
            }
            memset(n + s_diag_ubo_ext_cap, 0,
                   (size_t)(ncap - s_diag_ubo_ext_cap) * sizeof(*n));
            s_diag_ubo_ext = n;
            s_diag_ubo_ext_cap = ncap;
        }
        pbuf = &s_diag_ubo_ext[j].buf;
        pval = s_diag_ubo_ext[j].val;
    }

    *pbuf = wgpu_diag_ubo_alloc(*pbuf);
    if (*pbuf == NULL) {
        return NULL;
    }
    wgpuQueueWriteBuffer(s_queue, *pbuf, 0, vp, sizeof(vp));
    memcpy(pval, vp, sizeof(vp));
    s_diag_ubo_used++;
    return *pbuf;
}

/* Clip a rect (x,y,w,h) to [0,maxw] x [0,maxh], zeroing degenerate extents.
 * Order matters: shift for a negative origin first, bail to empty if the origin
 * is at/past the far edge, THEN clip the extent to the bound, THEN a final
 * non-negative clamp — so a containment adjustment can never re-introduce a
 * negative extent that would cast to a ~4-billion uint32 in Set{Viewport,Scissor}. */
static void wgpu_clamp_rect(int *x, int *y, int *w, int *h, int maxw, int maxh) {
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x >= maxw || *y >= maxh) { *w = 0; *h = 0; return; }
    if (*x + *w > maxw) *w = maxw - *x;
    if (*y + *h > maxh) *h = maxh - *y;
    if (*w < 0) *w = 0;
    if (*h < 0) *h = 0;
}

static void wgpu_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    if (!s_frame_open || s_cur_shader == NULL || s_cur_shader->module == NULL ||
        buf_vbo == NULL || buf_vbo_len == 0 || buf_vbo_num_tris == 0) {
        return;
    }
    wgpu_ensure_draw_resources();
    if (s_vbuf == NULL) {
        return;
    }
    WGPURenderPipeline pipe = wgpu_pipeline_for(s_cur_shader, s_cur_blend);
    if (pipe == NULL) {
        return;
    }

    /* RDP memory-blend draw (glass / fence class): snapshot the scene so the
     * shader can sample the current framebuffer as the N64 memory color. If the
     * snapshot cannot be produced, skip the draw — an opaque mis-blend is worse
     * than a dropped XLU batch. */
    bool rdp_mem_draw = s_cur_shader->info.diag_rdp_memory_blend ||
                        s_cur_shader->info.diag_rdp_cvg_memory_blend;
    WGPUBuffer diag_ubo = NULL;
    if (rdp_mem_draw) {
        if (!wgpu_snapshot_scene_for_memory_blend(buf_vbo, buf_vbo_len,
                                                  s_cur_shader->info.num_floats)) {
            return;
        }
        if (s_cur_shader->info.diag_rdp_cvg_memory_blend) {
            diag_ubo = wgpu_diag_viewport_ubo();
            if (diag_ubo == NULL) {
                return;
            }
        }
    }

    /* Bump-allocate this batch's vertex data. */
    uint32_t bytes = (uint32_t)(buf_vbo_len * sizeof(float));
    if ((uint64_t)s_vbuf_off + bytes > WGPU_VBUF_CAP) {
        static bool warned = false;
        if (!warned) { fprintf(stderr, "[webgpu] per-frame vertex buffer full — dropping draws\n"); warned = true; }
        return;
    }
    uint32_t voff = s_vbuf_off;
    wgpu_vbuf_stage(voff, buf_vbo, bytes);   /* WEB-023: stage; one upload in end_frame */
    s_vbuf_off += (bytes + 3u) & ~3u;   /* keep 4-byte alignment for the next offset */

    /* Bind group: each used texture's live view + sampler, falling back to a 1x1
     * white texture / nearest sampler when unset. Looked up in the 512-entry,
     * 4-way set-associative bind-group cache (s_bg_cache_tab, defined above at
     * the "Persistent draw bind-group cache" comment): a materials-worth of
     * concurrent (view, sampler) combos all stay resident across frames, so
     * this is a cache probe, not an allocation, on the common repeat-material
     * path. The key is on POINTER VALUES — safe only because every view/bgl
     * release purges matching entries first (wgpu_bg_cache_invalidate_view /
     * _bgl, the WEB-068 fix): a NEW object allocated at a RECYCLED handle
     * address would otherwise false-match a stale entry and sample the old
     * texture (the menu-glyph corruption class). */
    WGPUBindGroup bg = NULL;
    if (s_cur_shader->bgl != NULL) {
        WGPUTextureView v0 = s_white_view, v1 = s_white_view;
        WGPUSampler     m0 = s_default_sampler, m1 = s_default_sampler;
        /* PERF-019: track the texture entries whose views become v0/v1, so a newly
         * built bind group can register its cache slot in their reverse index. NULL
         * when the tile falls back to s_white_view (never released → no index needed). */
        struct WgpuTexEntry *e0 = NULL, *e1 = NULL;
        if (s_cur_shader->info.used_textures[0]) {
            struct WgpuTexEntry *e = wgpu_tex_lookup(s_bound_tex[0]);
            if (e != NULL && e->view != NULL) { v0 = e->view; e0 = e; }
            if (s_bound_sampler[0] != NULL) m0 = s_bound_sampler[0];
        }
        if (s_cur_shader->info.used_textures[1]) {
            struct WgpuTexEntry *e = wgpu_tex_lookup(s_bound_tex[1]);
            if (e != NULL && e->view != NULL) { v1 = e->view; e1 = e; }
            if (s_bound_sampler[1] != NULL) m1 = s_bound_sampler[1];
        }
        const void *key[7] = { s_cur_shader->bgl, v0, m0, v1, m1,
                               rdp_mem_draw ? (const void *)s_snap_view : NULL,
                               (const void *)diag_ubo };
        uint32_t set_base = (wgpu_bg_key_hash(key) & (WGPU_BG_CACHE / WGPU_BG_WAYS - 1)) * WGPU_BG_WAYS;
        for (int w = 0; w < WGPU_BG_WAYS; w++) {
            struct WgpuBgEntry *e = &s_bg_cache_tab[set_base + w];
            if (e->bg != NULL && memcmp(key, e->key, sizeof(key)) == 0) {
                bg = e->bg;   /* hit: reuse, no alloc this draw */
                break;
            }
        }
        if (bg == NULL) {
            WGPUBindGroupEntry ents[8];
            int ne = 0;
            for (int t = 0; t < 2; t++) {
                if (!s_cur_shader->info.used_textures[t]) continue;
                WGPUBindGroupEntry te = {0}; te.binding = (uint32_t)(t * 2);
                te.textureView = t == 0 ? v0 : v1; ents[ne++] = te;
                WGPUBindGroupEntry se = {0}; se.binding = (uint32_t)(t * 2 + 1);
                se.sampler = t == 0 ? m0 : m1; ents[ne++] = se;
            }
            if (rdp_mem_draw) {
                WGPUBindGroupEntry te = {0}; te.binding = 4;
                te.textureView = s_snap_view; ents[ne++] = te;
                WGPUBindGroupEntry se = {0}; se.binding = 5;
                se.sampler = s_snap_sampler; ents[ne++] = se;
            }
            if (diag_ubo != NULL) {
                WGPUBindGroupEntry ue = {0}; ue.binding = 6;
                ue.buffer = diag_ubo; ue.size = 16; ents[ne++] = ue;
            }
            /* WEB-027: the shared per-frame noise uniform (binding 7). The bgl
             * carries this entry only for noise combiners, so a noise shader with
             * no textures still has a non-NULL bgl and reaches this build. The UBO
             * is one global buffer, so it adds no discriminating state to the cache
             * key (the bgl pointer already distinguishes noise shaders). */
            if (s_cur_shader->info.uses_noise) {
                /* Review hardening: a noise BGL REQUIRES entry 7 — building the
                 * group without it yields an ERROR OBJECT (not NULL) that the
                 * cache would retain and every later draw would trip over.
                 * Mirror the diag-UBO pattern: no UBO (16-byte alloc failed ≈
                 * device lost) → drop the draw instead of poisoning the cache. */
                if (s_noise_ubo == NULL) {
                    return;
                }
                WGPUBindGroupEntry ue = {0}; ue.binding = 7;
                ue.buffer = s_noise_ubo; ue.size = 16; ents[ne++] = ue;
            }
            WGPUBindGroupDescriptor bd = {0};
            bd.layout = s_cur_shader->bgl;
            bd.entryCount = (size_t)ne;
            bd.entries = ents;
            bg = wgpuDeviceCreateBindGroup(s_device, &bd);
            if (bg != NULL) {
                /* Insert into the set: first empty way, else round-robin
                 * eviction (the in-flight pass holds its own ref on any
                 * evicted group until submit, so release here is safe). */
                struct WgpuBgEntry *victim = NULL;
                for (int w = 0; w < WGPU_BG_WAYS; w++) {
                    if (s_bg_cache_tab[set_base + w].bg == NULL) {
                        victim = &s_bg_cache_tab[set_base + w];
                        break;
                    }
                }
                if (victim == NULL) {
                    victim = &s_bg_cache_tab[set_base + (s_bg_cache_way & (WGPU_BG_WAYS - 1))];
                    s_bg_cache_way++;
                    wgpuBindGroupRelease(victim->bg);
                }
                memcpy(victim->key, key, sizeof(victim->key));
                victim->bg = bg;   /* the cache owns the ref */
                /* PERF-019: register this slot in the reverse index of the textures
                 * whose views it references (v0/v1), so releasing either view later
                 * invalidates just this slot instead of sweeping all 512 entries.
                 * The evicted slot's stale reverse-index entries (if any) are filtered
                 * by re-verification at release time, so no cleanup is needed here. */
                uint32_t slot_idx = (uint32_t)(victim - s_bg_cache_tab);
                wgpu_tex_bg_ref_add(e0, slot_idx);
                if (e1 != e0) {
                    wgpu_tex_bg_ref_add(e1, slot_idx);   /* skip a duplicate when both tiles share a texture */
                }
            }
        }
    }

    /* Viewport + scissor, Y-flipped to WebGPU's top-left origin (GL/gfx_pc emit
     * bottom-left; mirrors mtl_draw_triangles' originY = fb_h - (y + h)). BOTH
     * must be clamped to the scene bounds: WebGPU *validates* setViewport/
     * setScissorRect containment and a single out-of-range rect invalidates the
     * whole command buffer (black frame), whereas GL/Metal silently clip — so the
     * game may legitimately hand us a viewport/scissor extending past the target
     * (widescreen / split-screen). `wgpu_clamp_rect` clips a Y-flipped rect to
     * [0,W]x[0,H], zeroing degenerate extents. */
    {
        int vx = s_vp_x, vy = (int)s_scene_h - (s_vp_y + s_vp_h), vw = s_vp_w, vh = s_vp_h;
        /* DAM-R1 instrumentation: clamping an out-of-range viewport ALTERS the
         * viewport transform (GL keeps the transform and clips pixels), shifting
         * geometry near the trimmed edge. Log every rect the clamp changes. */
        {
            static int s_vp_trace = -1;
            if (s_vp_trace < 0) {
                s_vp_trace = getenv("GE007_WEBGPU_TRACE_VIEWPORT") != NULL ? 1 : 0;
            }
            if (s_vp_trace &&
                (vx < 0 || vy < 0 ||
                 vx + vw > (int)s_scene_w || vy + vh > (int)s_scene_h)) {
                static int s_vp_trace_count = 0;
                if (s_vp_trace_count < 64) {
                    s_vp_trace_count++;
                    fprintf(stderr,
                            "[WGPU-VP] out-of-range viewport pre-clamp: raw=(%d,%d %dx%d) "
                            "flipped=(%d,%d %dx%d) scene=%ux%u\n",
                            s_vp_x, s_vp_y, s_vp_w, s_vp_h,
                            vx, vy, vw, vh, s_scene_w, s_scene_h);
                }
            }
        }
        wgpu_clamp_rect(&vx, &vy, &vw, &vh, (int)s_scene_w, (int)s_scene_h);
        if (vw > 0 && vh > 0) {
            /* WEB-023-lite: skip the Set when the (clamped, Y-flipped) rect is
             * unchanged from the last one applied to this pass encoder. */
            if (!s_vp_applied || vx != s_vp_ax || vy != s_vp_ay ||
                vw != s_vp_aw || vh != s_vp_ah) {
                wgpuRenderPassEncoderSetViewport(s_pass, (float)vx, (float)vy,
                                                 (float)vw, (float)vh, 0.0f, 1.0f);
                s_vp_applied = true;
                s_vp_ax = vx; s_vp_ay = vy; s_vp_aw = vw; s_vp_ah = vh;
            }
        }
    }
    {
        int sx = s_sc_set ? s_sc_x : 0;
        int sw = s_sc_set ? s_sc_w : (int)s_scene_w;
        int sh = s_sc_set ? s_sc_h : (int)s_scene_h;
        int sy = s_sc_set ? ((int)s_scene_h - (s_sc_y + s_sc_h)) : 0;
        wgpu_clamp_rect(&sx, &sy, &sw, &sh, (int)s_scene_w, (int)s_scene_h);
        /* WEB-023-lite: skip the redundant SetScissorRect (gfx_pc re-emits the
         * same rect every draw). Trackers are reset at each pass begin. */
        if (!s_sc_applied || sx != s_sc_ax || sy != s_sc_ay ||
            sw != s_sc_aw || sh != s_sc_ah) {
            wgpuRenderPassEncoderSetScissorRect(s_pass, (uint32_t)sx, (uint32_t)sy,
                                                (uint32_t)sw, (uint32_t)sh);
            s_sc_applied = true;
            s_sc_ax = sx; s_sc_ay = sy; s_sc_aw = sw; s_sc_ah = sh;
        }
    }

    /* PERF-014: skip the SetPipeline/SetBindGroup when unchanged from the last
     * draw applied to this pass encoder. gfx_pc re-issues full material setup per
     * draw group, so consecutive draws frequently repeat the same pipeline+bind
     * group; on web each redundant Set is a wasm↔JS crossing. The trackers are
     * reset to NULL at every s_pass begin (wgpu_reset_pass_dynamic_state), so the
     * first draw of a pass always issues both. `pipe` is non-NULL here — the
     * PERF-005 early-out above (wgpu_pipeline_for == NULL → return) ran before this
     * point, so a pending/failed pipeline never reaches, nor poisons, the tracker. */
    if (pipe != s_pipe_applied) {
        wgpuRenderPassEncoderSetPipeline(s_pass, pipe);
        s_pipe_applied = pipe;
    }
    if (bg != NULL && bg != s_bg_applied) {
        wgpuRenderPassEncoderSetBindGroup(s_pass, 0, bg, 0, NULL);
        s_bg_applied = bg;
    }
    wgpuRenderPassEncoderSetVertexBuffer(s_pass, 0, s_vbuf, voff, bytes);
    wgpuRenderPassEncoderDraw(s_pass, (uint32_t)(3 * buf_vbo_num_tris), 1, 0, 0);
    /* bg is owned by the bind-group cache (retained across draws); the pass holds
     * its own reference until submit, so we never release it here. */
}

/* ------------------------------------------------------------------------
 * Minimap / radar overlay (T4b) — a 2D screen-space pass into the scene target
 * after the main geometry, mirroring the GL (minimap_overlay.c direct draws) and
 * Metal (gfx_metal_draw_minimap_overlay) paths. Vertices are MinimapOverlayVertex
 * {x,y (pixels), r,g,b,a} (stride 24); the shader maps pixels -> NDC with a
 * screen-size uniform. Called from wgpu_end_frame after the main render pass.
 * ---------------------------------------------------------------------- */
static WGPURenderPipeline s_mm_pipe = NULL;
static WGPUBindGroupLayout s_mm_bgl = NULL;
static WGPUBindGroup s_mm_bg = NULL;
static WGPUBuffer s_mm_ubuf = NULL;

static const char *kMinimapWGSL =
    "struct MMIn { @location(0) pos : vec2<f32>, @location(1) color : vec4<f32> };\n"
    "struct MMOut { @builtin(position) clip : vec4<f32>, @location(0) color : vec4<f32> };\n"
    "struct MMU { screen : vec2<f32>, pad : vec2<f32> };\n"
    "@group(0) @binding(0) var<uniform> mmu : MMU;\n"
    "@vertex fn vs_main(in : MMIn) -> MMOut {\n"
    "  var o : MMOut;\n"
    "  let ndc = vec2<f32>((in.pos.x / mmu.screen.x) * 2.0 - 1.0, 1.0 - (in.pos.y / mmu.screen.y) * 2.0);\n"
    "  o.clip = vec4<f32>(ndc, 0.0, 1.0);\n"
    "  o.color = in.color;\n"
    "  return o;\n}\n"
    "@fragment fn fs_main(in : MMOut) -> @location(0) vec4<f32> { return in.color; }\n";

static bool wgpu_ensure_minimap(void) {
    if (s_mm_pipe != NULL) {
        return true;
    }
    if (!s_ready) {
        return false;
    }
    WGPUShaderSourceWGSL src = {0};
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code = wgpu_sv(kMinimapWGSL);
    WGPUShaderModuleDescriptor smd = {0};
    smd.nextInChain = (WGPUChainedStruct *)&src;
    WGPUShaderModule mod = wgpuDeviceCreateShaderModule(s_device, &smd);
    if (mod == NULL) {
        return false;
    }

    WGPUBindGroupLayoutEntry ue = {0};
    ue.binding = 0;
    ue.visibility = WGPUShaderStage_Vertex;
    ue.buffer.type = WGPUBufferBindingType_Uniform;
    ue.buffer.minBindingSize = 16;
    WGPUBindGroupLayoutDescriptor bgld = {0};
    bgld.entryCount = 1;
    bgld.entries = &ue;
    s_mm_bgl = wgpuDeviceCreateBindGroupLayout(s_device, &bgld);

    WGPUBufferDescriptor ubd = {0};
    ubd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    ubd.size = 16;
    s_mm_ubuf = wgpuDeviceCreateBuffer(s_device, &ubd);

    WGPUBindGroupEntry bge = {0};
    bge.binding = 0;
    bge.buffer = s_mm_ubuf;
    bge.size = 16;
    WGPUBindGroupDescriptor bgd = {0};
    bgd.layout = s_mm_bgl;
    bgd.entryCount = 1;
    bgd.entries = &bge;
    s_mm_bg = wgpuDeviceCreateBindGroup(s_device, &bgd);

    WGPUPipelineLayoutDescriptor pld = {0};
    pld.bindGroupLayoutCount = 1;
    pld.bindGroupLayouts = &s_mm_bgl;
    WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(s_device, &pld);

    WGPUVertexAttribute attrs[2] = {0};
    attrs[0].format = WGPUVertexFormat_Float32x2; attrs[0].offset = 0;  attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x4; attrs[1].offset = 8;  attrs[1].shaderLocation = 1;
    WGPUVertexBufferLayout vbl = {0};
    vbl.stepMode = WGPUVertexStepMode_Vertex;
    vbl.arrayStride = 24;   /* MinimapOverlayVertex: 6 floats */
    vbl.attributeCount = 2;
    vbl.attributes = attrs;

    WGPUBlendState blend = {0};   /* standard alpha (the overlay is translucent) */
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    WGPUColorTargetState color = {0};
    color.format = s_surface_format;
    color.writeMask = WGPUColorWriteMask_All;
    color.blend = &blend;

    WGPUFragmentState fs = {0};
    fs.module = mod; fs.entryPoint = wgpu_sv("fs_main");
    fs.targetCount = 1; fs.targets = &color;
    WGPURenderPipelineDescriptor pd = {0};
    pd.layout = pl;
    pd.vertex.module = mod; pd.vertex.entryPoint = wgpu_sv("vs_main");
    pd.vertex.bufferCount = 1; pd.vertex.buffers = &vbl;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.primitive.unclippedDepth = s_unclipped_depth_supported ? WGPU_TRUE : WGPU_FALSE;
    pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFFu;
    pd.fragment = &fs;   /* no depth-stencil: 2D overlay pass has no depth attachment */
    s_mm_pipe = wgpuDeviceCreateRenderPipeline(s_device, &pd);

    wgpuPipelineLayoutRelease(pl);
    wgpuShaderModuleRelease(mod);
    return s_mm_pipe != NULL;
}

/* Called by minimap_overlay.c's flush. Returns nonzero on success (matching the
 * Metal hook's convention). Draws the queued overlay vertices into the scene
 * target in a fresh load-op render pass, using the still-open frame encoder. */
int gfx_webgpu_draw_minimap_overlay(const void *vertices, size_t vertex_count,
                                    int fb_width, int fb_height,
                                    int scissor_enabled, int scissor_x, int scissor_y,
                                    int scissor_w, int scissor_h) {
    if (!s_ready || s_encoder == NULL || s_scene_view == NULL || s_vbuf == NULL ||
        vertices == NULL || vertex_count == 0 || fb_width <= 0 || fb_height <= 0) {
        return 0;
    }
    if (!wgpu_ensure_minimap()) {
        return 0;
    }
    /* WEB-014: honor the minimap clip rect. GL and Metal both apply it; WebGPU
     * previously voided all five params, so minimap geometry the layout clips
     * (lines/blips while moving or rotating) could draw outside the minimap
     * window over the game view — on the default backend everywhere. Y-flip to
     * WebGPU's top-left origin like Metal (sy = fb_h - (y+h)), clamp to the
     * present-target pixel bounds (s_scene_w x s_scene_h), and skip a
     * fully-clipped draw. Computed BEFORE the vbuf write so a clipped-away frame
     * costs nothing (mirrors gfx_metal's early return). */
    int mm_sc_x = 0, mm_sc_y = 0, mm_sc_w = 0, mm_sc_h = 0;
    if (scissor_enabled) {
        mm_sc_x = scissor_x;
        mm_sc_y = fb_height - (scissor_y + scissor_h);
        mm_sc_w = scissor_w;
        mm_sc_h = scissor_h;
        wgpu_clamp_rect(&mm_sc_x, &mm_sc_y, &mm_sc_w, &mm_sc_h,
                        (int)s_scene_w, (int)s_scene_h);
        if (mm_sc_w <= 0 || mm_sc_h <= 0) {
            return 1;   /* wholly clipped — nothing to draw, not a failure */
        }
    }
    /* Draw onto the present target (post-FX result when the filter ran, else the
     * raw scene) so the minimap sits on top of the graded frame — not tonemapped —
     * matching GL/Metal. s_present_target_view is set in wgpu_end_frame before this
     * hook fires; fall back to the scene view defensively. */
    WGPUTextureView mm_target = s_present_target_view ? s_present_target_view : s_scene_view;
    uint32_t bytes = (uint32_t)(vertex_count * 24u);
    if ((uint64_t)s_vbuf_off + bytes > WGPU_VBUF_CAP) {
        return 0;
    }
    uint32_t voff = s_vbuf_off;
    wgpu_vbuf_stage(voff, vertices, bytes);   /* WEB-023: stage; one upload in end_frame */
    s_vbuf_off += (bytes + 3u) & ~3u;

    float u[4] = { (float)fb_width, (float)fb_height, 0.0f, 0.0f };
    wgpuQueueWriteBuffer(s_queue, s_mm_ubuf, 0, u, sizeof(u));

    WGPURenderPassColorAttachment att = {0};
    att.view = mm_target;
    att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    att.loadOp = WGPULoadOp_Load;    /* preserve the (post-FX) frame underneath */
    att.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor rp = {0};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &att;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(s_encoder, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, s_mm_pipe);
    if (scissor_enabled) {   /* WEB-014: clip to the minimap window */
        wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)mm_sc_x, (uint32_t)mm_sc_y,
                                            (uint32_t)mm_sc_w, (uint32_t)mm_sc_h);
    }
    wgpuRenderPassEncoderSetBindGroup(pass, 0, s_mm_bg, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, s_vbuf, voff, bytes);
    wgpuRenderPassEncoderDraw(pass, (uint32_t)vertex_count, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return 1;
}

/* Submit the in-progress scene and immediately resume it with Load semantics.
 *
 * The ordinary WebGPU path intentionally records one command buffer for the
 * whole frame.  Draw-boundary diagnostics in gfx_pc.c are different: they call
 * read_framebuffer_rgb before and after a selected draw and require those reads
 * to observe the current command-stream position.  Without a partial submit,
 * readback can only see the previous completed frame, so every WebGPU
 * [TRI-PIXEL]/[SETTEX-PIXEL] row falsely reports pre == post.
 *
 * This slow path is reached only when a readback is actually requested.  The
 * normal gameplay path still performs the single end-of-frame vertex upload
 * and submit.  Vertex data must be uploaded before finishing this encoder:
 * WEB-023 normally defers that upload until end_frame, but the draws being
 * submitted here already reference the staged offsets. */
static bool wgpu_submit_live_scene_for_readback(void) {
    if (!s_frame_open || s_encoder == NULL || s_pass == NULL ||
        s_scene_view == NULL || s_depth_view == NULL) {
        return false;
    }

    wgpuRenderPassEncoderEnd(s_pass);
    wgpuRenderPassEncoderRelease(s_pass);
    s_pass = NULL;

    if (s_vbuf != NULL && s_vbuf_shadow != NULL && s_vbuf_off > 0) {
        wgpuQueueWriteBuffer(s_queue, s_vbuf, 0, s_vbuf_shadow, s_vbuf_off);
    }

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(s_encoder, NULL);
    if (cmd == NULL) {
        wgpuCommandEncoderRelease(s_encoder);
        s_encoder = NULL;
        s_frame_open = false;
        return false;
    }
    wgpuQueueSubmit(s_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(s_encoder);

    s_encoder = wgpuDeviceCreateCommandEncoder(s_device, NULL);
    if (s_encoder == NULL) {
        s_frame_open = false;
        return false;
    }

    WGPURenderPassColorAttachment att = {0};
    att.view = s_scene_view;
    att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    att.loadOp = WGPULoadOp_Load;
    att.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDepthStencilAttachment depth = {0};
    depth.view = s_depth_view;
    depth.depthLoadOp = WGPULoadOp_Load;
    depth.depthStoreOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor rp = {0};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &att;
    rp.depthStencilAttachment = &depth;
    s_pass = wgpuCommandEncoderBeginRenderPass(s_encoder, &rp);
    if (s_pass == NULL) {
        wgpuCommandEncoderRelease(s_encoder);
        s_encoder = NULL;
        s_frame_open = false;
        return false;
    }

    /* A new pass inherits no dynamic state.  gfx_pc.c re-applies the current
     * viewport/scissor/pipeline before the next draw. */
    wgpu_reset_pass_dynamic_state();
    return true;
}

/* Task 5: read back the last-rendered offscreen scene as GL-convention
 * bottom-left RGB (so platformSaveScreenshot + the parity/oracle tooling work on
 * WebGPU identically to GL/Metal). Copies the whole BGRA8 scene into a mappable
 * buffer, then extracts the requested rect with a vertical flip + BGRA->RGB.
 * Synchronous (submit + poll-map) — only screenshot/parity/diagnostic paths call
 * it.  During an open frame, partially submit first so draw-boundary probes see
 * the live scene rather than s_present_target_tex from the previous frame. */
static bool wgpu_read_framebuffer_rgb(int x, int y, int width, int height, uint8_t *rgb_out) {
    /* PERF-008: this session performs readbacks — pin every subsequent frame to the
     * offscreen present path (which retains a readable s_present_target_tex). Sticky,
     * so it protects any readback caller whose arming signal wgpu_readback_possible()
     * did not pre-enumerate; the fire during gfx_run_dl trips before this frame's own
     * end_frame, so even the current frame presents offscreen. */
    s_readback_latched = true;
    bool live_scene = s_frame_open;
    if (live_scene && !wgpu_submit_live_scene_for_readback()) {
        return false;
    }
    /* During a frame, the freshly submitted raw scene is authoritative.  After
     * end_frame, preserve AUDIT-0003 behavior and read the post-FX/minimap target. */
    WGPUTexture rb_tex = live_scene
        ? s_scene_tex
        : (s_present_target_tex ? s_present_target_tex : s_scene_tex);
    if (!s_ready || rb_tex == NULL || rgb_out == NULL ||
        width <= 0 || height <= 0 || s_scene_w == 0 || s_scene_h == 0) {
        return false;
    }
    uint32_t bpr = ((s_scene_w * 4u + 255u) / 256u) * 256u;   /* 256-align */
    size_t buf_size = (size_t)bpr * s_scene_h;
    WGPUBufferDescriptor bd = {0};
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bd.size = buf_size;
    WGPUBuffer buf = wgpuDeviceCreateBuffer(s_device, &bd);
    if (buf == NULL) {
        return false;
    }

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(s_device, NULL);
    WGPUTexelCopyTextureInfo src = {0};
    src.texture = rb_tex; src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst = {0};
    dst.buffer = buf;
    dst.layout.bytesPerRow = bpr;
    dst.layout.rowsPerImage = s_scene_h;
    WGPUExtent3D ext = { s_scene_w, s_scene_h, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, NULL);
    wgpuQueueSubmit(s_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);

    /* WEB-026: static so a timed-out map's late callback lands on live storage,
     * not this frame's dead stack. Synchronous single-threaded call; reset here. */
    static WgpuMapReq mr;
    mr = (WgpuMapReq){0};
    WGPUBufferMapCallbackInfo ci = {0};
    ci.mode = WGPUCallbackMode_AllowProcessEvents;
    ci.callback = on_map;
    ci.userdata1 = &mr;
    wgpuBufferMapAsync(buf, WGPUMapMode_Read, 0, buf_size, ci);
    /* WEB-004: pass s_instance (not NULL) so the browser pump can drive
     * wgpuInstanceProcessEvents — the mapAsync callback ONLY fires during
     * ProcessEvents, so a NULL instance froze the tab for minutes on web. On
     * native the WAIT macro prefers the (non-NULL) device and calls
     * wgpuDevicePoll exactly as before — byte-identical. */
    WGPU_COMPAT_WAIT(mr.done, s_instance, s_device, 100000);
    if (!mr.done || mr.status != WGPUMapAsyncStatus_Success) {
        wgpuBufferRelease(buf);
        return false;
    }
    const uint8_t *px = (const uint8_t *)wgpuBufferGetConstMappedRange(buf, 0, buf_size);
    if (px == NULL) {
        wgpuBufferUnmap(buf);
        wgpuBufferRelease(buf);
        return false;
    }
    /* rgb_out is width*height RGB, bottom-left origin (GL convention). The scene
     * is top-left, so scene row (H-1 - (y+row)) maps to output row `row`. */
    for (int row = 0; row < height; row++) {
        int scene_y = (int)s_scene_h - 1 - (y + row);
        uint8_t *out = rgb_out + (size_t)row * width * 3;
        if (scene_y < 0 || scene_y >= (int)s_scene_h) {
            memset(out, 0, (size_t)width * 3);
            continue;
        }
        const uint8_t *srow = px + (size_t)scene_y * bpr;
        const bool bgra = wgpu_format_is_bgra(s_surface_format);
        for (int col = 0; col < width; col++) {
            int sx = x + col;
            if (sx < 0 || sx >= (int)s_scene_w) { out[col*3] = out[col*3+1] = out[col*3+2] = 0; continue; }
            const uint8_t *p = srow + (size_t)sx * 4;   /* BGRA8 (or RGBA8) */
            out[col*3+0] = bgra ? p[2] : p[0];   /* R */
            out[col*3+1] = p[1];                 /* G */
            out[col*3+2] = bgra ? p[0] : p[2];   /* B */
        }
    }
    wgpuBufferUnmap(buf);
    wgpuBufferRelease(buf);
    return true;
}

/* ------------------------------------------------------------------------
 * Optional: draw_modern_mesh (Task 6 — scene decor, G_MODERNMESH).
 *
 * A full-fidelity mesh (float32 pos/nrm/uv + rgba8, u32 indices, RGBA8 texture)
 * drawn into the live scene pass, transformed by the interpreter's MP matrix
 * with the N64 fog curve. Ports gfx_metal.mm's decor shader + cache. Renders
 * scene decoration, which is a no-op on the GL default (AUDIT-0001); needs
 * Video.SceneDecor=1. Per-mesh GPU resources are cached by mesh_id (ids are
 * monotonic/never reused, so a full evict at capacity is safe).
 * ---------------------------------------------------------------------- */
static const char *kModernWGSL =
    "struct DVin { @location(0) pos : vec3<f32>, @location(1) nrm : vec3<f32>, @location(2) uv : vec2<f32>, @location(3) col : vec4<f32> };\n"
    "struct DU { mvp : mat4x4<f32>, fog : vec4<f32>, fogMul : f32, fogOffset : f32, fogOn : f32, pad : f32 };\n"
    "@group(0) @binding(0) var<uniform> u : DU;\n"
    "@group(0) @binding(1) var dtex : texture_2d<f32>;\n"
    "@group(0) @binding(2) var dsmp : sampler;\n"
    "struct DOut { @builtin(position) position : vec4<f32>, @location(0) uv : vec2<f32>, @location(1) col : vec4<f32>, @location(2) fogA : f32 };\n"
    "@vertex fn vs_main(in : DVin) -> DOut {\n"
    "  var clip = u.mvp * vec4<f32>(in.pos, 1.0);\n"
    "  var fogA = 0.0;\n"
    "  if (u.fogOn > 0.5) {\n"
    "    var ww = clip.w;\n"
    "    if (abs(ww) < 0.001) { ww = 0.001; }\n"
    "    let winv = 1.0 / ww;\n"
    "    let coord = select(clip.z * winv, clip.z * 32767.0, winv < 0.0);\n"
    "    fogA = clamp(coord * u.fogMul + u.fogOffset, 0.0, 255.0) / 255.0;\n"
    "  }\n"
    "  var o : DOut;\n"
    "  clip.z = (clip.z + clip.w) * 0.5;\n"   /* GL-clip -> WebGPU 0..1, like Metal */
    "  o.position = clip; o.uv = in.uv; o.col = in.col; o.fogA = fogA;\n"
    "  return o;\n}\n"
    "fn decorShade(o : DOut, t : vec4<f32>) -> vec4<f32> {\n"
    "  var c = t.rgb * o.col.rgb;\n"
    "  c = mix(c, vec3<f32>(0.88, 0.91, 0.96), o.col.a);\n"   /* snow cover in vertex alpha */
    "  c = mix(c, u.fog.rgb, o.fogA);\n"
    "  return vec4<f32>(c, t.a);\n}\n"
    "@fragment fn fs_opaque(in : DOut) -> @location(0) vec4<f32> {\n"
    "  let c = decorShade(in, textureSample(dtex, dsmp, in.uv));\n"
    "  return vec4<f32>(c.rgb, 1.0);\n}\n"
    "@fragment fn fs_cutout(in : DOut) -> @location(0) vec4<f32> {\n"
    "  let t = textureSample(dtex, dsmp, in.uv);\n"
    "  if (t.a < 0.45) { discard; }\n"
    "  let c = decorShade(in, t);\n"
    "  return vec4<f32>(c.rgb, 1.0);\n}\n";

static WGPUShaderModule    s_modern_mod = NULL;
static WGPUBindGroupLayout s_modern_bgl = NULL;
static WGPUPipelineLayout  s_modern_pl = NULL;
static WGPURenderPipeline  s_modern_pipe[2] = {NULL, NULL};   /* [cutout] */
static WGPUSampler         s_modern_sampler = NULL;

struct WgpuModernEntry {
    uint32_t mesh_id;
    WGPUBuffer vbuf, ibuf;
    WGPUTexture tex;
    WGPUTextureView view;
    uint32_t idx_count;
    /* WEB-052: bind group cached across frames (references the shared dynamic-
     * offset UBO ring + this mesh's texture/sampler). bg_gen stamps the ring
     * buffer generation it was built against, so a ring grow (buffer recreate)
     * forces a rebuild. */
    WGPUBindGroup bg;
    uint32_t bg_gen;
};
static struct WgpuModernEntry s_modern_cache[64];
static int s_modern_count = 0;

static WGPURenderPipeline wgpu_modern_pipe(int cutout) {
    if (s_modern_pipe[cutout] != NULL) {
        return s_modern_pipe[cutout];
    }
    if (s_modern_mod == NULL) {
        WGPUShaderSourceWGSL src = {0};
        src.chain.sType = WGPUSType_ShaderSourceWGSL;
        src.code = wgpu_sv(kModernWGSL);
        WGPUShaderModuleDescriptor smd = {0};
        smd.nextInChain = (WGPUChainedStruct *)&src;
        s_modern_mod = wgpuDeviceCreateShaderModule(s_device, &smd);
        if (s_modern_mod == NULL) return NULL;

        WGPUBindGroupLayoutEntry e[3] = {0};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = 96;
        e[0].buffer.hasDynamicOffset = true;   /* WEB-052: ring slot via dynamic offset */
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].texture.sampleType = WGPUTextureSampleType_Float; e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment;
        e[2].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor bgld = {0};
        bgld.entryCount = 3; bgld.entries = e;
        s_modern_bgl = wgpuDeviceCreateBindGroupLayout(s_device, &bgld);
        WGPUPipelineLayoutDescriptor pld = {0};
        pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &s_modern_bgl;
        s_modern_pl = wgpuDeviceCreatePipelineLayout(s_device, &pld);
    }

    WGPUVertexAttribute a[4] = {0};
    a[0].format = WGPUVertexFormat_Float32x3; a[0].offset = 0;  a[0].shaderLocation = 0;
    a[1].format = WGPUVertexFormat_Float32x3; a[1].offset = 12; a[1].shaderLocation = 1;
    a[2].format = WGPUVertexFormat_Float32x2; a[2].offset = 24; a[2].shaderLocation = 2;
    a[3].format = WGPUVertexFormat_Unorm8x4;  a[3].offset = 32; a[3].shaderLocation = 3;
    WGPUVertexBufferLayout vbl = {0};
    vbl.stepMode = WGPUVertexStepMode_Vertex;
    vbl.arrayStride = 36;
    vbl.attributeCount = 4;
    vbl.attributes = a;

    WGPUColorTargetState color = {0};
    color.format = s_surface_format;
    color.writeMask = WGPUColorWriteMask_All;   /* opaque (frag outputs a=1) */
    WGPUFragmentState fs = {0};
    fs.module = s_modern_mod;
    fs.entryPoint = wgpu_sv(cutout ? "fs_cutout" : "fs_opaque");
    fs.targetCount = 1; fs.targets = &color;

    WGPUDepthStencilState ds = {0};
    ds.format = WGPU_DEPTH_FORMAT;
    ds.depthCompare = WGPUCompareFunction_LessEqual;
    ds.depthWriteEnabled = WGPUOptionalBool_True;
    ds.stencilFront.compare = WGPUCompareFunction_Always;
    ds.stencilFront.failOp = WGPUStencilOperation_Keep;
    ds.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    ds.stencilFront.passOp = WGPUStencilOperation_Keep;
    ds.stencilBack = ds.stencilFront;

    WGPURenderPipelineDescriptor pd = {0};
    pd.layout = s_modern_pl;
    pd.vertex.module = s_modern_mod; pd.vertex.entryPoint = wgpu_sv("vs_main");
    pd.vertex.bufferCount = 1; pd.vertex.buffers = &vbl;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;   /* cutout cards are two-sided */
    pd.depthStencil = &ds;
    pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFFu;
    pd.fragment = &fs;
    s_modern_pipe[cutout] = wgpuDeviceCreateRenderPipeline(s_device, &pd);
    return s_modern_pipe[cutout];
}

static struct WgpuModernEntry *wgpu_modern_resources(struct GfxModernMesh *mesh) {
    for (int i = 0; i < s_modern_count; i++) {
        if (s_modern_cache[i].mesh_id == mesh->mesh_id) return &s_modern_cache[i];
    }
    if (s_modern_count >= (int)(sizeof(s_modern_cache) / sizeof(s_modern_cache[0]))) {
        /* Level churn: ids never repeat, so releasing everything is safe. */
        for (int i = 0; i < s_modern_count; i++) {
            if (s_modern_cache[i].bg)   wgpuBindGroupRelease(s_modern_cache[i].bg);
            if (s_modern_cache[i].view) wgpuTextureViewRelease(s_modern_cache[i].view);
            if (s_modern_cache[i].tex)  wgpuTextureRelease(s_modern_cache[i].tex);
            if (s_modern_cache[i].vbuf) wgpuBufferRelease(s_modern_cache[i].vbuf);
            if (s_modern_cache[i].ibuf) wgpuBufferRelease(s_modern_cache[i].ibuf);
        }
        s_modern_count = 0;
    }

    uint32_t vbytes = mesh->vtx_count * 36u;
    uint32_t ibytes = mesh->idx_count * 4u;
    WGPUBufferDescriptor vd = {0};
    vd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; vd.size = vbytes;
    WGPUBuffer vb = wgpuDeviceCreateBuffer(s_device, &vd);
    WGPUBufferDescriptor id = {0};
    id.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst; id.size = ibytes;
    WGPUBuffer ib = wgpuDeviceCreateBuffer(s_device, &id);
    if (vb == NULL || ib == NULL) { if (vb) wgpuBufferRelease(vb); if (ib) wgpuBufferRelease(ib); return NULL; }
    wgpuQueueWriteBuffer(s_queue, vb, 0, mesh->vtx, vbytes);
    wgpuQueueWriteBuffer(s_queue, ib, 0, mesh->idx, ibytes);

    WGPUTextureDescriptor td = {0};
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.size.width = (uint32_t)mesh->tex_w; td.size.height = (uint32_t)mesh->tex_h; td.size.depthOrArrayLayers = 1;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1; td.sampleCount = 1;   /* single-level (no mip gen) */
    WGPUTexture tex = wgpuDeviceCreateTexture(s_device, &td);
    if (tex == NULL) { wgpuBufferRelease(vb); wgpuBufferRelease(ib); return NULL; }
    WGPUTexelCopyTextureInfo dst = {0};
    dst.texture = tex; dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout lay = {0};
    lay.bytesPerRow = (uint32_t)mesh->tex_w * 4u; lay.rowsPerImage = (uint32_t)mesh->tex_h;
    WGPUExtent3D ext = { (uint32_t)mesh->tex_w, (uint32_t)mesh->tex_h, 1 };
    wgpuQueueWriteTexture(s_queue, &dst, mesh->tex_rgba, (size_t)mesh->tex_w * mesh->tex_h * 4u, &lay, &ext);

    struct WgpuModernEntry *e = &s_modern_cache[s_modern_count++];
    e->mesh_id = mesh->mesh_id;
    e->vbuf = vb; e->ibuf = ib; e->tex = tex;
    e->view = wgpuTextureCreateView(tex, NULL);
    e->idx_count = mesh->idx_count;
    e->bg = NULL; e->bg_gen = 0;   /* WEB-052: built lazily on first draw */
    return e;
}

/* WEB-052: ensure the ring UBO holds at least `need_slots` 256-byte slots. Grows
 * (recreates) the buffer at need; a grow bumps the generation so cached per-mesh
 * bind groups (which reference the old buffer) rebuild against the new one. Never
 * shrinks the buffer within a frame — a slot already written this frame stays
 * referenced by an earlier draw's dynamic-offset bind. */
static bool wgpu_modern_ubo_reserve(int need_slots) {
    if (s_modern_ubo != NULL && need_slots <= s_modern_ubo_cap) {
        return true;
    }
    int ncap = s_modern_ubo_cap ? s_modern_ubo_cap : WGPU_MODERN_UBO_INIT;
    while (ncap < need_slots) {
        ncap *= 2;
    }
    WGPUBufferDescriptor bd = {0};
    bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    bd.size = (uint64_t)ncap * WGPU_MODERN_UBO_ALIGN;
    WGPUBuffer nb = wgpuDeviceCreateBuffer(s_device, &bd);
    if (nb == NULL) {
        return false;
    }
    /* Release the app-side handle to the old buffer: any bind group/encoder from
     * earlier this frame retains its own strong ref until submit, so this is safe. */
    if (s_modern_ubo != NULL) {
        wgpuBufferRelease(s_modern_ubo);
    }
    s_modern_ubo = nb;
    s_modern_ubo_cap = ncap;
    s_modern_ubo_gen++;
    return true;
}

static void wgpu_draw_modern_mesh(struct GfxModernMesh *mesh, const float mvp[4][4],
                                  const float fog_color[3], float fog_mul,
                                  float fog_offset, int fog_enabled) {
    if (!s_ready || !s_frame_open || s_pass == NULL || mesh == NULL ||
        mesh->vtx == NULL || mesh->idx == NULL || mesh->tex_rgba == NULL ||
        mesh->tex_w <= 0 || mesh->tex_h <= 0) {
        return;
    }
    int cutout = mesh->cutout ? 1 : 0;
    WGPURenderPipeline pipe = wgpu_modern_pipe(cutout);
    if (pipe == NULL) return;
    struct WgpuModernEntry *res = wgpu_modern_resources(mesh);
    if (res == NULL) return;

    if (s_modern_sampler == NULL) {
        WGPUSamplerDescriptor sd = {0};
        sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_Repeat;
        sd.magFilter = sd.minFilter = WGPUFilterMode_Linear;
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;   /* single-level */
        sd.maxAnisotropy = 1;
        s_modern_sampler = wgpuDeviceCreateSampler(s_device, &sd);
    }

    /* Uniform (96 bytes): row-major MP memcpy'd into the mat4x4 (columns == MP
     * rows, so u.mvp * v == the CPU row-vector T&L), fog params. */
    float u[24];
    memset(u, 0, sizeof(u));
    memcpy(u, mvp, 16 * sizeof(float));
    u[16] = fog_color[0]; u[17] = fog_color[1]; u[18] = fog_color[2]; u[19] = 0.0f;
    u[20] = fog_mul; u[21] = fog_offset; u[22] = fog_enabled ? 1.0f : 0.0f; u[23] = 0.0f;

    /* WEB-052: claim a FRESH ring slot (never a slot already written this frame),
     * write the uniform there, and bind it via a dynamic offset — instead of
     * allocating a one-off UBO + bind group per draw. */
    if (!wgpu_modern_ubo_reserve(s_modern_ubo_used + 1)) {
        return;
    }
    int slot = s_modern_ubo_used++;
    uint32_t dyn_off = (uint32_t)slot * WGPU_MODERN_UBO_ALIGN;
    wgpuQueueWriteBuffer(s_queue, s_modern_ubo, dyn_off, u, sizeof(u));

    /* Per-mesh bind group, cached across frames. Rebuild it only when absent or
     * built against a stale ring buffer (a grow bumped s_modern_ubo_gen). Its
     * uniform binding covers ONE slot at offset 0; the actual slot is selected by
     * the dynamic offset passed to SetBindGroup below. */
    if (res->bg == NULL || res->bg_gen != s_modern_ubo_gen) {
        if (res->bg != NULL) { wgpuBindGroupRelease(res->bg); res->bg = NULL; }
        WGPUBindGroupEntry be[3] = {0};
        be[0].binding = 0; be[0].buffer = s_modern_ubo; be[0].offset = 0; be[0].size = sizeof(u);
        be[1].binding = 1; be[1].textureView = res->view;
        be[2].binding = 2; be[2].sampler = s_modern_sampler;
        WGPUBindGroupDescriptor bgd = {0};
        bgd.layout = s_modern_bgl; bgd.entryCount = 3; bgd.entries = be;
        res->bg = wgpuDeviceCreateBindGroup(s_device, &bgd);
        res->bg_gen = s_modern_ubo_gen;
        if (res->bg == NULL) {
            return;
        }
    }

    wgpuRenderPassEncoderSetPipeline(s_pass, pipe);
    wgpuRenderPassEncoderSetBindGroup(s_pass, 0, res->bg, 1, &dyn_off);
    /* PERF-014: this draw wrote s_pass's pipeline/bind group directly, bypassing
     * wgpu_draw_triangles' dedup while sharing the same scene pass. Keep the
     * trackers honest so the next wgpu_draw_triangles cannot wrongly skip a needed
     * re-bind: `pipe` IS now the bound pipeline (record it, so a following triangle
     * draw with the same pipeline can still skip); res->bg was bound WITH a dynamic
     * offset, so force the next triangle draw to re-issue its group(0) bind (NULL
     * sentinel) rather than record a handle a different dynamic offset could alias. */
    s_pipe_applied = pipe;
    s_bg_applied   = NULL;
    wgpuRenderPassEncoderSetVertexBuffer(s_pass, 0, res->vbuf, 0, mesh->vtx_count * 36u);
    wgpuRenderPassEncoderSetIndexBuffer(s_pass, res->ibuf, WGPUIndexFormat_Uint32, 0, mesh->idx_count * 4u);
    wgpuRenderPassEncoderDrawIndexed(s_pass, res->idx_count, 1, 0, 0, 0);
    /* res->bg is owned by the mesh cache; the ring UBO persists — nothing to
     * release here (the encoder retains referenced resources until submit). */
}

/* WebGPU clip space is 0..1 (like Metal/D3D, unlike GL's -1..1). Reported to the
 * CPU clipper (gfx_pc.c GFX_CLIP_Z_SCALE); paired with g_depth_clamp_enabled set
 * in gfx_init() for the WebGPU path. */
static bool wgpu_z_is_from_0_to_1(void) { return true; }

/* ------------------------------------------------------------------------
 * Non-vtable couplings (called directly from gfx_pc.c, backend-aware)
 * ---------------------------------------------------------------------- */
void gfx_webgpu_set_clear_color(float r, float g, float b) {
    s_clear_r = (double)r;
    s_clear_g = (double)g;
    s_clear_b = (double)b;
}

int gfx_webgpu_max_offscreen_dim(void) {
    /* WEB-015: the device's granted maxTextureDimension2D (adapter max, up from
     * the 8192 WebGPU default), or the 8192 floor when bring-up was skipped. */
    return (int)s_max_tex_dim;
}

bool gfx_webgpu_unclipped_depth_supported(void) {
    /* PVD-001 (DAM_RENDER_DEEP_DIVE_2026-07-18 §D): whether the 3D pipelines were
     * built with unclippedDepth (i.e. DepthClipControl was granted, so the GPU
     * depth-CLAMPS like GL/Metal).  gfx_init reads this to decide whether the CPU
     * clipper must take over depth-plane clipping on featureless adapters. */
    return s_unclipped_depth_supported;
}

bool gfx_webgpu_get_framebuffer_size(int *width, int *height) {
    if (width == NULL || height == NULL || s_scene_w == 0 || s_scene_h == 0) {
        return false;
    }
    *width = (int)s_scene_w;
    *height = (int)s_scene_h;
    return true;
}

/* ------------------------------------------------------------------------
 * The vtable — same field order as gfx_opengl_api / gfx_metal_api.
 * ---------------------------------------------------------------------- */
struct GfxRenderingAPI gfx_webgpu_api = {
    wgpu_z_is_from_0_to_1,
    wgpu_unload_shader,
    wgpu_load_shader,
    wgpu_create_and_load_new_shader,
    wgpu_lookup_shader,
    wgpu_shader_get_info,
    wgpu_new_texture,
    wgpu_delete_texture,
    wgpu_select_texture,
    wgpu_upload_texture,
    wgpu_set_sampler_parameters,
    wgpu_set_depth_mode,
    wgpu_set_viewport,
    wgpu_set_scissor,
    wgpu_set_blend_mode,
    wgpu_draw_triangles,
    wgpu_read_framebuffer_rgb,
    wgpu_init,
    wgpu_on_resize,
    wgpu_start_frame,
    wgpu_end_frame,
    wgpu_finish_render,
    wgpu_draw_modern_mesh,   /* Task 6: scene decor (G_MODERNMESH) */
};

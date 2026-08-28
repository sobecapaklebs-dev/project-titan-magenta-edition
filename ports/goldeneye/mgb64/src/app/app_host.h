// app_host.h — portable app-shell host: owns the SDL window + render context + ImGui.
//
// The AppHost is the cross-platform equivalent of the macOS Swift AppDelegate:
// it creates the window and rendering context, drives ImGui, and hands the
// window (+ device/surface) to the engine when the user starts a game.
//
// It picks the backend at init() by gfx_backend_use_webgpu(): the default is
// WebGPU (window + wgpu device/surface, launcher UI via gfx_webgpu_imgui), and
// GE007_RENDERER=gl falls back to a GL window + context + imgui_impl_opengl3.
// Either way the engine adopts the same objects (platformSetHostWindow +
// platformSetHostWebGpu), so the launcher and game render into one window.
#ifndef MGB64_APP_HOST_H
#define MGB64_APP_HOST_H

#include <SDL.h>

#include <string>

class AppHost {
public:
    // Create the window + GL context + ImGui. Returns false on failure.
    bool init(const char *title, int width, int height);

    // RX.2: size the launcher window per UI.LauncherFullscreen. `mode` is the raw
    // enum value (0=auto, 1=on, 2=off). Auto fills the screen on small/high-DPI
    // handheld panels (the launcher would otherwise "float" on a 1920x1200 7-inch
    // display) and leaves a resizable window on desktop monitors so the dev
    // workflow is unchanged. Safe to call once after init() + config load.
    // Returns true if the window was switched to borderless fullscreen-desktop.
    bool applyLauncherFullscreen(int mode);

    // Begin an ImGui frame and clear the framebuffer.
    void beginFrame();

    // Render ImGui draw data and present the frame. If captureBmpPath is
    // non-null, the finished frame is saved as a 24-bit BMP before the swap
    // (used by the headless smoke / design review / CI).
    // Returns false only when a capture was requested (captureBmpPath != null)
    // and its BMP was not fully written (AUDIT-0046). True otherwise.
    bool endFrame(const char *captureBmpPath = nullptr);

    // Framebuffer/logical ratio (2.0 on Retina). Valid after init().
    float framebufferScale() const;

    // Pump SDL events into ImGui. Returns true when the user requested quit
    // (window close / Cmd-Q).
    bool pumpAndShouldQuit();

    // RX.4: the path of a file the user dragged onto the window since the last
    // call (SDL_DROPFILE), or "" if none. Returns + clears it, so each drop is
    // consumed once. The launcher routes it into the ROM panel.
    std::string takeDroppedFile();

    // Tear down ImGui + GL + SDL.
    void shutdown();

    SDL_Window   *window()    const { return window_; }
    SDL_GLContext glContext() const { return gl_; }
    int drawableWidth()  const;
    int drawableHeight() const;

    // True when the app shell is running on WebGPU (vs. the GL fallback). Set at
    // init(). The engine adopts GL or WebGPU accordingly.
    bool usingWebGpu() const { return useWebGpu_; }

    // WebGPU host objects (opaque WGPU* as void*, 0 when on GL), for the game to
    // adopt via platformSetHostWebGpu(). Valid after a successful WebGPU init().
    void *wgpuInstance() const { return wgpuInstance_; }
    void *wgpuAdapter()  const { return wgpuAdapter_; }
    void *wgpuDevice()   const { return wgpuDevice_; }
    void *wgpuQueue()    const { return wgpuQueue_; }
    void *wgpuSurface()  const { return wgpuSurface_; }
    int   wgpuFormat()   const { return wgpuFormat_; }

private:
    // Backend-specific init helpers (init() dispatches to one).
    bool initWebGpu(const char *title, int width, int height);
    bool initGL(const char *title, int width, int height);
    // Drawable (pixel) size, backend-aware (GL vs Metal/native drawable).
    void drawableSize(int *w, int *h) const;
    // (Re)configure the WebGPU surface to the given pixel size. No-op on GL.
    void configureWgpuSurface(int w, int h);
    // (Re)create the offscreen scene target at the given pixel size if needed.
    void ensureWgpuSceneTarget(int w, int h);
    // Backend-specific frame present (endFrame dispatches to one).
    bool endFrameWebGpu(const char *captureBmpPath);
    bool endFrameGL(const char *captureBmpPath);

    SDL_Window   *window_ = nullptr;
    SDL_GLContext gl_     = nullptr;
    bool imguiReady_      = false;
    bool sdlOwned_        = false;  // did we SDL_Init (vs. reuse an existing init)?
    std::string droppedFile_;      // last SDL_DROPFILE path, consumed by takeDroppedFile()

    // WebGPU path (selected at init by gfx_backend_use_webgpu()).
    bool  useWebGpu_    = false;
    void *metalView_    = nullptr;  // SDL_MetalView backing the surface layer (macOS)
    void *wgpuInstance_ = nullptr;
    void *wgpuAdapter_  = nullptr;
    void *wgpuDevice_   = nullptr;
    void *wgpuQueue_    = nullptr;
    void *wgpuSurface_  = nullptr;
    int   wgpuFormat_   = 0;
    unsigned cfgW_ = 0, cfgH_ = 0;  // last-configured surface pixel size
    // Offscreen scene target: the launcher UI renders here (window-independent),
    // then is blitted to the surface for present and read back for capture — the
    // same decoupling gfx_webgpu.c uses so a hidden/occluded window still renders.
    void *sceneTex_  = nullptr;
    void *sceneView_ = nullptr;
    unsigned sceneW_ = 0, sceneH_ = 0;
};

#endif  // MGB64_APP_HOST_H

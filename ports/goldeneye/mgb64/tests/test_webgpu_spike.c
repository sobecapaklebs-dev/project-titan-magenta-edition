/*
 * test_webgpu_spike.c — headless, self-validating proof that the WebGPU
 * (wgpu-native) backend works end to end on this platform.
 *
 * Milestones (each an assertion, no display needed):
 *   A. Bring up WebGPU: instance -> adapter -> device (proves the pinned prebuilt
 *      links and the native backend — Metal on macOS — initializes).
 *   B. Compile a WGSL shader AT RUNTIME (the capability that made WebGPU the right
 *      fit for Fast3D's runtime-generated combiner shaders, vs bgfx's offline model).
 *   C. Render a triangle to an offscreen RGBA8 target, read the pixels back, and
 *      ASSERT the triangle is present (center = triangle color, corner = clear).
 *
 * This is the de-risking spike from the renderer backend strategy: passing on
 * macOS + Windows + a handheld proves WebGPU viable before the full migration.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>  /* wgpuDevicePoll: drive GPU completion for the readback */

static int g_fails = 0;
#define CHECK(name, cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", (name)); ++g_fails; } \
    else         { fprintf(stderr, "ok:   %s\n", (name)); } \
} while (0)

static WGPUStringView sv(const char *s) { WGPUStringView v; v.data = s; v.length = strlen(s); return v; }

/* --- async request helpers (wgpu-native fires these during processEvents) ----- */
typedef struct { WGPUAdapter adapter; int done; WGPURequestAdapterStatus status; } AdapterReq;
static void on_adapter(WGPURequestAdapterStatus s, WGPUAdapter a, WGPUStringView m, void *u1, void *u2) {
    (void)m; (void)u2; AdapterReq *r = (AdapterReq *)u1; r->status = s; r->adapter = a; r->done = 1;
}
typedef struct { WGPUDevice device; int done; WGPURequestDeviceStatus status; } DeviceReq;
static void on_device(WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView m, void *u1, void *u2) {
    (void)m; (void)u2; DeviceReq *r = (DeviceReq *)u1; r->status = s; r->device = d; r->done = 1;
}
typedef struct { int done; WGPUMapAsyncStatus status; } MapReq;
static void on_map(WGPUMapAsyncStatus s, WGPUStringView m, void *u1, void *u2) {
    (void)m; (void)u2; MapReq *r = (MapReq *)u1; r->status = s; r->done = 1;
}

/* The triangle: compiled from WGSL at runtime (Milestone B). No vertex buffer —
 * positions come from the vertex index, so this exercises the shader path only. */
static const char *kWGSL =
    "@vertex fn vs_main(@builtin(vertex_index) i : u32) -> @builtin(position) vec4f {\n"
    "  var p = array<vec2f, 3>(vec2f(0.0, 0.7), vec2f(-0.7, -0.7), vec2f(0.7, -0.7));\n"
    "  return vec4f(p[i], 0.0, 1.0);\n"
    "}\n"
    "@fragment fn fs_main() -> @location(0) vec4f {\n"
    "  return vec4f(0.15, 0.85, 0.35, 1.0);\n"  /* green */
    "}\n";

int main(void) {
    const uint32_t W = 64, H = 64;
    const uint32_t BYTES_PER_ROW = W * 4;   /* 256 — already 256-aligned for W=64 */
    const size_t   BUF_SIZE = (size_t)BYTES_PER_ROW * H;

    /* ---- Milestone A: instance -> adapter -> device ---------------------- */
    WGPUInstance instance = wgpuCreateInstance(NULL);
    CHECK("create instance", instance != NULL);
    if (!instance) return 1;

    AdapterReq areq = {0};
    WGPURequestAdapterCallbackInfo acb = {0};
    acb.mode = WGPUCallbackMode_AllowProcessEvents; acb.callback = on_adapter; acb.userdata1 = &areq;
    WGPURequestAdapterOptions aopts = {0};
    wgpuInstanceRequestAdapter(instance, &aopts, acb);
    for (int i = 0; !areq.done && i < 1000; ++i) wgpuInstanceProcessEvents(instance);
    CHECK("request adapter", areq.done && areq.status == WGPURequestAdapterStatus_Success && areq.adapter);
    if (!areq.adapter) return 1;

    WGPUAdapterInfo info = {0};
    wgpuAdapterGetInfo(areq.adapter, &info);
    fprintf(stderr, "[webgpu-spike] adapter backend=%d device=%.*s\n",
            (int)info.backendType, (int)info.device.length, info.device.data ? info.device.data : "");

    DeviceReq dreq = {0};
    WGPURequestDeviceCallbackInfo dcb = {0};
    dcb.mode = WGPUCallbackMode_AllowProcessEvents; dcb.callback = on_device; dcb.userdata1 = &dreq;
    WGPUDeviceDescriptor ddesc = {0};
    wgpuAdapterRequestDevice(areq.adapter, &ddesc, dcb);
    for (int i = 0; !dreq.done && i < 1000; ++i) wgpuInstanceProcessEvents(instance);
    CHECK("request device", dreq.done && dreq.status == WGPURequestDeviceStatus_Success && dreq.device);
    if (!dreq.device) return 1;
    WGPUDevice device = dreq.device;
    WGPUQueue queue = wgpuDeviceGetQueue(device);

    /* ---- Milestone B: compile WGSL at runtime ---------------------------- */
    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = sv(kWGSL);
    WGPUShaderModuleDescriptor smd = {0};
    smd.nextInChain = (WGPUChainedStruct *)&wgsl;
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &smd);
    CHECK("compile WGSL at runtime", shader != NULL);
    if (!shader) return 1;

    /* ---- Milestone C: render a triangle offscreen ------------------------ */
    WGPUTextureDescriptor td = {0};
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    td.dimension = WGPUTextureDimension_2D;
    td.size.width = W; td.size.height = H; td.size.depthOrArrayLayers = 1;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture tex = wgpuDeviceCreateTexture(device, &td);
    WGPUTextureView view = wgpuTextureCreateView(tex, NULL);

    WGPUColorTargetState colorTarget = {0};
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs = {0};
    fs.module = shader; fs.entryPoint = sv("fs_main");
    fs.targetCount = 1; fs.targets = &colorTarget;
    WGPURenderPipelineDescriptor pd = {0};
    pd.vertex.module = shader; pd.vertex.entryPoint = sv("vs_main");
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFFu;
    pd.fragment = &fs;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &pd);
    CHECK("create render pipeline", pipeline != NULL);
    if (!pipeline) return 1;

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, NULL);
    WGPURenderPassColorAttachment att = {0};
    att.view = view;
    att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;  /* required for a 2D color target */
    att.loadOp = WGPULoadOp_Clear; att.storeOp = WGPUStoreOp_Store;
    att.clearValue.r = 0.05; att.clearValue.g = 0.05; att.clearValue.b = 0.07; att.clearValue.a = 1.0;
    WGPURenderPassDescriptor rp = {0};
    rp.colorAttachmentCount = 1; rp.colorAttachments = &att;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    /* copy the rendered target into a mappable buffer */
    WGPUBufferDescriptor bd = {0};
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bd.size = BUF_SIZE;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(device, &bd);
    WGPUTexelCopyTextureInfo src = {0};
    src.texture = tex; src.mipLevel = 0; src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst = {0};
    dst.buffer = readback;
    dst.layout.offset = 0; dst.layout.bytesPerRow = BYTES_PER_ROW; dst.layout.rowsPerImage = H;
    WGPUExtent3D ext = { W, H, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, NULL);
    wgpuQueueSubmit(queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);

    /* map + read back */
    MapReq mreq = {0};
    WGPUBufferMapCallbackInfo mcb = {0};
    mcb.mode = WGPUCallbackMode_AllowProcessEvents; mcb.callback = on_map; mcb.userdata1 = &mreq;
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, BUF_SIZE, mcb);
    for (int i = 0; !mreq.done && i < 1000; ++i) wgpuDevicePoll(device, true /*wait*/, NULL);
    CHECK("map readback", mreq.done && mreq.status == WGPUMapAsyncStatus_Success);

    const uint8_t *px = (const uint8_t *)wgpuBufferGetConstMappedRange(readback, 0, BUF_SIZE);
    CHECK("mapped range", px != NULL);
    if (px) {
        /* center pixel is inside the up-pointing triangle -> green */
        const uint8_t *c = px + (size_t)(H / 2) * BYTES_PER_ROW + (size_t)(W / 2) * 4;
        /* top-left corner is outside the triangle -> clear (dark) */
        const uint8_t *k = px + (size_t)2 * BYTES_PER_ROW + (size_t)2 * 4;
        fprintf(stderr, "[webgpu-spike] center rgba=(%d,%d,%d,%d)  corner rgba=(%d,%d,%d,%d)\n",
                c[0], c[1], c[2], c[3], k[0], k[1], k[2], k[3]);
        CHECK("triangle center is green", c[1] > 150 && c[1] > c[0] && c[1] > c[2]);
        CHECK("corner is the clear color", k[0] < 60 && k[1] < 60 && k[2] < 90);
    }

    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(tex);
    wgpuRenderPipelineRelease(pipeline);
    wgpuShaderModuleRelease(shader);
    wgpuQueueRelease(queue);
    wgpuAdapterInfoFreeMembers(info);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(areq.adapter);
    wgpuInstanceRelease(instance);

    if (g_fails == 0) { fprintf(stderr, "PASS: webgpu spike (A init + B runtime-WGSL + C triangle readback)\n"); return 0; }
    fprintf(stderr, "%d failure(s)\n", g_fails);
    return 1;
}

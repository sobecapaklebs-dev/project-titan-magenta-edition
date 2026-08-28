// Spike B: RDP/XLU framebuffer-snapshot feasibility on Metal.
// Proves: render a known color into a color-attachment texture, blit-copy it to
// a second (sampled) texture with a forced encoder break, then SAMPLE that copy
// in a fragment shader writing to a third texture, and read it back. This is the
// mechanism the port's per-batch XLU snapshot + RDP-memory blend needs.
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <stdio.h>

static const char *kMSL =
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"vertex float4 vmain(uint vid [[vertex_id]]) {\n"
"  float2 p[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };\n"
"  return float4(p[vid], 0, 1);\n"
"}\n"
"fragment float4 fmain(float4 pos [[position]], texture2d<float> snap [[texture(0)]]) {\n"
"  constexpr sampler s(filter::nearest);\n"
"  return snap.sample(s, float2(0.5, 0.5));\n"   // sample the copied snapshot
"}\n";

int main() {
  @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) { printf("SPIKE B: FAIL — no Metal device\n"); return 1; }
    id<MTLCommandQueue> q = [dev newCommandQueue];
    const int W = 8, H = 8;

    MTLTextureDescriptor *rt = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:W height:H mipmapped:NO];
    rt.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    id<MTLTexture> texA = [dev newTextureWithDescriptor:rt];  // "scene framebuffer" color attachment
    id<MTLTexture> texB = [dev newTextureWithDescriptor:rt];  // snapshot copy (sampled)
    id<MTLTexture> texC = [dev newTextureWithDescriptor:rt];  // final output (read back)

    // Build the sample-the-snapshot pipeline.
    NSError *err = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:[NSString stringWithUTF8String:kMSL] options:nil error:&err];
    if (!lib) { printf("SPIKE B: FAIL — MSL compile: %s\n", err.localizedDescription.UTF8String); return 1; }
    MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = [lib newFunctionWithName:@"vmain"];
    pd.fragmentFunction = [lib newFunctionWithName:@"fmain"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    id<MTLRenderPipelineState> pso = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!pso) { printf("SPIKE B: FAIL — pipeline: %s\n", err.localizedDescription.UTF8String); return 1; }

    id<MTLCommandBuffer> cb = [q commandBuffer];

    // 1. Render a known color into texA (the "scene" being snapshotted).
    const double KR = 0.5, KG = 0.25, KB = 0.75;
    MTLRenderPassDescriptor *rpA = [MTLRenderPassDescriptor renderPassDescriptor];
    rpA.colorAttachments[0].texture = texA;
    rpA.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpA.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpA.colorAttachments[0].clearColor = MTLClearColorMake(KR, KG, KB, 1.0);
    [[cb renderCommandEncoderWithDescriptor:rpA] endEncoding];

    // 2. XLU SNAPSHOT: blit-copy the color attachment -> sampled texture (encoder break).
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:texA sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
               sourceSize:MTLSizeMake(W,H,1)
                toTexture:texB destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
    [blit endEncoding];

    // 3. Fragment pass SAMPLING the snapshot -> texC.
    MTLRenderPassDescriptor *rpC = [MTLRenderPassDescriptor renderPassDescriptor];
    rpC.colorAttachments[0].texture = texC;
    rpC.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpC.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpC.colorAttachments[0].clearColor = MTLClearColorMake(0,0,0,1);
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rpC];
    [enc setRenderPipelineState:pso];
    [enc setFragmentTexture:texB atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];

    [cb commit];
    [cb waitUntilCompleted];

    // 4. Read texC back and check it equals the known color.
    uint8_t px[4] = {0};
    [texC getBytes:px bytesPerRow:W*4 fromRegion:MTLRegionMake2D(W/2,H/2,1,1) mipmapLevel:0];
    int er = (int)(KR*255+0.5), eg = (int)(KG*255+0.5), eb = (int)(KB*255+0.5);
    printf("SPIKE B: sampled snapshot = (%d,%d,%d), expected ~(%d,%d,%d)\n", px[0],px[1],px[2], er,eg,eb);
    int ok = (abs(px[0]-er)<=2 && abs(px[1]-eg)<=2 && abs(px[2]-eb)<=2);
    printf("SPIKE B: %s — blit color-attachment -> sampled texture -> fragment sample round-trip %s\n",
           ok ? "GO" : "NO-GO", ok ? "works" : "FAILED");
    return ok ? 0 : 2;
  }
}

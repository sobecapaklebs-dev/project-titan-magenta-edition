#include "gfx_titan.h"
#include "gfx_cc.h"
#include "../../../../titan_ge_runtime.h"

#include <math.h>
#include <string.h>

#define TITAN_SHADER_CAPACITY 256u
#define TITAN_TEXTURE_CAPACITY 2048u

struct ShaderProgram {
    uint64_t id0;
    uint32_t id1;
    struct CCFeatures features;
};

static TitanGePlatform *s_platform;
static struct ShaderProgram s_shaders[TITAN_SHADER_CAPACITY];
static uint32_t s_shader_count;
static struct ShaderProgram *s_shader;
static uint32_t s_texture_handles[TITAN_TEXTURE_CAPACITY];
static uint8_t s_texture_live[TITAN_TEXTURE_CAPACITY];
static uint32_t s_next_texture = 1u;
static uint32_t s_selected_texture[2];
static int s_upload_texture_unit;
static TitanGameRenderState s_render_state;
static uint8_t s_clear_red, s_clear_green, s_clear_blue;
static uint8_t s_trace_first_upload;
static uint8_t s_trace_first_draw;
static uint8_t s_trace_first_begin;
static uint8_t s_trace_first_end;
static uint8_t s_trace_first_upload_detail;
static uint8_t s_trace_first_textured_draw;
static uint8_t s_trace_postupload_end;
static uint8_t s_p136_force_untextured_logged;
static uint8_t s_p137_neutral_state_logged;
static uint8_t s_p138_restore_viewport_scissor_logged;
static uint8_t s_p139_real_depth_logged;
static uint8_t s_p140_no_depth_write_logged;
static uint8_t s_p141_ztest_always_logged;
static uint8_t s_p143_full_depth_logged;
static uint8_t s_p144_real_blend_logged;
static uint8_t s_p145_real_vertex_rgba_logged;
static uint8_t s_p148_texture_upload_bypass_logged;
static uint8_t s_p149_real_textures_logged;

/* P10.47: preserve Fast3D batches instead of crossing the Titan Game ABI once
 * per triangle.  32 triangles = 96 vertices = 4608 bytes of BSS, avoiding
 * transient heap/stack pressure while collapsing up to 32 API calls into one. */
#define TITANEYE_MATERIAL_BATCH_TRIANGLES 32u
#define TITANEYE_MATERIAL_BATCH_VERTICES (TITANEYE_MATERIAL_BATCH_TRIANGLES * 3u)
static TitanGameMaterialVertex s_titaneye_material_batch[
    TITANEYE_MATERIAL_BATCH_VERTICES
];
static uint8_t titan_channel(float value);

static void titan_reset_render_state(void)
{
    memset(&s_render_state, 0, sizeof(s_render_state));
    s_render_state.struct_size = sizeof(s_render_state);
    s_render_state.viewport_width = TITAN_GAME_DEFAULT_WIDTH;
    s_render_state.viewport_height = TITAN_GAME_DEFAULT_HEIGHT;
    s_render_state.scissor_width = TITAN_GAME_DEFAULT_WIDTH;
    s_render_state.scissor_height = TITAN_GAME_DEFAULT_HEIGHT;
    s_render_state.depth_test = 1u;
    s_render_state.depth_write = 1u;
    s_render_state.depth_compare = TITAN_GAME_DEPTH_GEQUAL;
    s_render_state.combine_mode = TITAN_GAME_COMBINE_TEX0_MODULATE;
    s_render_state.sampler[0].linear_filter = 1u;
    s_render_state.sampler[1].linear_filter = 1u;
}

void gfx_titan_bind(TitanGePlatform *platform) { s_platform = platform; }
void gfx_titan_set_clear_color(float red, float green, float blue)
{
    s_clear_red = titan_channel(red);
    s_clear_green = titan_channel(green);
    s_clear_blue = titan_channel(blue);
}
static bool titan_z01(void) { return false; }
static void titan_unload(struct ShaderProgram *program) { (void)program; }
static void titan_load(struct ShaderProgram *program) { s_shader = program; }

static struct ShaderProgram *titan_lookup(uint64_t id0, uint32_t id1)
{
    uint32_t i;
    for (i = 0u; i < s_shader_count; i++)
        if (s_shaders[i].id0 == id0 && s_shaders[i].id1 == id1)
            return &s_shaders[i];
    return 0;
}

static struct ShaderProgram *titan_create(uint64_t id0, uint32_t id1)
{
    struct ShaderProgram *program = titan_lookup(id0, id1);
    if (program == 0)
    {
        if (s_shader_count >= TITAN_SHADER_CAPACITY) return 0;
        program = &s_shaders[s_shader_count++];
        memset(program, 0, sizeof(*program));
        program->id0 = id0;
        program->id1 = id1;
        gfx_cc_get_features(id0, id1, &program->features);
    }
    s_shader = program;
    return program;
}

static void titan_shader_info(
    struct ShaderProgram *program, uint8_t *inputs, bool textures[2]
)
{
    *inputs = program != 0 ? (uint8_t)program->features.num_inputs : 0u;
    textures[0] = program != 0 && program->features.used_textures[0];
    textures[1] = program != 0 && program->features.used_textures[1];
}

static uint32_t titan_new_texture(void)
{
    uint32_t attempts;
    for (attempts = 1u; attempts < TITAN_TEXTURE_CAPACITY; attempts++)
    {
        uint32_t texture = s_next_texture;
        s_next_texture++;
        if (s_next_texture >= TITAN_TEXTURE_CAPACITY) s_next_texture = 1u;
        if (!s_texture_live[texture])
        {
            s_texture_live[texture] = 1u;
            return texture;
        }
    }
    return 0u;
}

static void titan_release_backend_texture(uint32_t texture)
{
    if (s_platform != 0 && texture < TITAN_TEXTURE_CAPACITY &&
        s_texture_handles[texture] != 0u)
    {
        (void)TitanGePlatform_ReleaseTexture(
            s_platform, s_texture_handles[texture]
        );
        s_texture_handles[texture] = 0u;
    }
}

static void titan_delete_texture(uint32_t texture)
{
    int unit;
    if (texture == 0u || texture >= TITAN_TEXTURE_CAPACITY ||
        !s_texture_live[texture])
        return;
    titan_release_backend_texture(texture);
    s_texture_live[texture] = 0u;
    for (unit = 0; unit < 2; unit++)
        if (s_selected_texture[unit] == texture)
            s_selected_texture[unit] = 0u;
}

static void titan_select_texture(int tile, uint32_t texture)
{
    if (tile >= 0 && tile < 2 &&
        (texture == 0u ||
         (texture < TITAN_TEXTURE_CAPACITY && s_texture_live[texture])))
    {
        s_selected_texture[tile] = texture;
        s_upload_texture_unit = tile;
    }
}

static bool titan_upload_texture(const uint8_t *rgba, int width, int height)
{
    uint32_t logical = s_selected_texture[s_upload_texture_unit];
    uint32_t old_handle;
    uint32_t handle;

    if (s_platform == 0 || rgba == 0 || logical == 0u ||
        logical >= TITAN_TEXTURE_CAPACITY || !s_texture_live[logical] ||
        width <= 0 || height <= 0)
        return false;

    /* P10.58 lifetime bridge: hand the current backend handle to the kernel.
     * TitanVideo_GameUploadTextureRgba32() already fingerprints input and can
     * return the existing PikaVR handle on an exact content hit.  The old code
     * released it before asking, guaranteeing reuse could never happen. */
    old_handle = s_texture_handles[logical];
    handle = old_handle;

    if (!s_trace_first_upload)
        TitanGeRuntime_Trace("20_FIRST_TEXTURE_UPLOAD_BEFORE");

    if (!TitanGePlatform_UploadTextureRgba32(
            s_platform, rgba, (uint32_t)width, (uint32_t)height, &handle))
    {
        if (!s_trace_first_upload)
        {
            TitanGeRuntime_Trace("E_FIRST_TEXTURE_UPLOAD");
            s_trace_first_upload = 1u;
        }
        /* Upload failure is non-destructive: keep the prior live backing. */
        return false;
    }

    if (!s_trace_first_upload)
    {
        TitanGeRuntime_Trace("21_FIRST_TEXTURE_UPLOAD_AFTER");
        s_trace_first_upload = 1u;
    }

    if (!s_trace_first_upload_detail)
        TitanGeRuntime_Trace("26_FIRST_TEXTURE_HANDLE_STORE_BEFORE");

    /* A content hit returns old_handle and needs no release.  A real update
     * allocates a new backing first; retire the old one only after success. */
    if (old_handle != 0u && handle != old_handle)
        (void)TitanGePlatform_ReleaseTexture(s_platform, old_handle);

    s_texture_handles[logical] = handle;

    if (!s_trace_first_upload_detail)
    {
        TitanGeRuntime_Trace("27_FIRST_TEXTURE_HANDLE_STORE_AFTER");
        TitanGeRuntime_Trace("28_FIRST_TEXTURE_UPLOAD_RETURN");
        s_trace_first_upload_detail = 1u;
    }

    return true;
}

static uint8_t titan_wrap(uint32_t mode)
{
    if ((mode & 2u) != 0u) return TITAN_GAME_WRAP_CLAMP;
    if ((mode & 1u) != 0u) return TITAN_GAME_WRAP_MIRROR;
    return TITAN_GAME_WRAP_REPEAT;
}

bool gfx_titan_upload_texture_i8(
    const uint8_t *pixels, int width, int height, int stride
)
{
    uint32_t logical = s_selected_texture[s_upload_texture_unit];
    uint32_t old_handle;
    uint32_t handle;

    if (s_platform == 0 || pixels == 0 || logical == 0u ||
        logical >= TITAN_TEXTURE_CAPACITY || !s_texture_live[logical] ||
        width <= 0 || height <= 0 || stride < width)
        return false;

    old_handle = s_texture_handles[logical];
    handle = old_handle;
    if (!TitanGePlatform_UploadTextureI8(
            s_platform, pixels, (uint32_t)width, (uint32_t)height,
            (uint32_t)stride, &handle))
        return false;

    if (old_handle != 0u && old_handle != handle)
        (void)TitanGePlatform_ReleaseTexture(s_platform, old_handle);
    s_texture_handles[logical] = handle;
    return true;
}

static void titan_sampler(int sampler, bool linear, uint32_t cms, uint32_t cmt)
{
    if (sampler < 0 || sampler > 1) return;
    s_render_state.sampler[sampler].linear_filter = linear ? 1u : 0u;
    s_render_state.sampler[sampler].wrap_s = titan_wrap(cms);
    s_render_state.sampler[sampler].wrap_t = titan_wrap(cmt);
}
static void titan_depth(bool test, bool update, bool compare, bool prim, uint16_t zmode)
{
    (void)prim;
    (void)zmode;
    s_render_state.depth_test = test ? 1u : 0u;
    s_render_state.depth_write = update ? 1u : 0u;
    s_render_state.depth_compare = compare
        ? TITAN_GAME_DEPTH_GEQUAL : TITAN_GAME_DEPTH_ALWAYS;
}
static void titan_viewport(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) return;
    s_render_state.viewport_x = x;
    s_render_state.viewport_y = (int32_t)TITAN_GAME_DEFAULT_HEIGHT - y - height;
    s_render_state.viewport_width = (uint32_t)width;
    s_render_state.viewport_height = (uint32_t)height;
}
static void titan_scissor(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) return;
    s_render_state.scissor_x = x;
    s_render_state.scissor_y = (int32_t)TITAN_GAME_DEFAULT_HEIGHT - y - height;
    s_render_state.scissor_width = (uint32_t)width;
    s_render_state.scissor_height = (uint32_t)height;
}
static void titan_blend(enum GfxBlendMode mode)
{
    if (mode == GFX_BLEND_MODULATE)
        s_render_state.blend_mode = TITAN_GAME_BLEND_MODULATE;
    else if (mode == GFX_BLEND_DISABLED)
        s_render_state.blend_mode = TITAN_GAME_BLEND_OPAQUE;
    else
        s_render_state.blend_mode = TITAN_GAME_BLEND_ALPHA;
}

static uint8_t titan_channel(float value)
{
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    return (uint8_t)(value * 255.0f + 0.5f);
}

static float titan_clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float titan_combiner_source(
    uint8_t source,
    const float *inputs,
    size_t input_stride,
    int component,
    float combined
)
{
    if (source == SHADER_0) return 0.0f;
    if (source == SHADER_1) return 1.0f;
    if (source == SHADER_COMBINED) return combined;
    if (source == SHADER_TEXEL0 || source == SHADER_TEXEL1 ||
        source == SHADER_TEXEL0A || source == SHADER_TEXEL1A)
        return 1.0f;
    if (source >= SHADER_INPUT_1 && source <= SHADER_INPUT_7)
    {
        size_t index = (size_t)(source - SHADER_INPUT_1);
        return inputs[index * input_stride + (size_t)component];
    }
    return source == SHADER_NOISE ? 0.5f : 0.0f;
}

static float titan_combiner_channel(
    const struct CCFeatures *features,
    const float *inputs,
    size_t input_stride,
    int channel,
    int component
)
{
    float combined = 0.0f;
    int last_cycle = features->opt_2cyc ? 1 : 0;
    int cycle;
    for (cycle = 0; cycle <= last_cycle; cycle++)
    {
        const uint8_t *c = features->c[cycle][channel];
        float a = titan_combiner_source(c[0],inputs,input_stride,component,combined);
        float b = titan_combiner_source(c[1],inputs,input_stride,component,combined);
        float m = titan_combiner_source(c[2],inputs,input_stride,component,combined);
        float d = titan_combiner_source(c[3],inputs,input_stride,component,combined);
        combined = (a - b) * m + d;
    }
    return titan_clamp01(combined);
}

static float titan_mask_coordinate(
    float coordinate,
    float period,
    uint8_t wrap
)
{
    if (period > 0.0f)
    {
        float cycle = floorf(coordinate / period);
        coordinate -= cycle * period;
        if (wrap == TITAN_GAME_WRAP_MIRROR &&
            (((int)cycle) & 1) != 0)
            coordinate = period - coordinate;
    }
    return coordinate;
}

static void titan_draw(float vbo[], size_t length, size_t triangles)
{
    struct CCFeatures *features;
    size_t stride, texture_offset[2] = {0u,0u};
    size_t fog_offset = 0u, input_offset, triangle, vertex;
    uint32_t texture_handle[2] = {0u,0u};
    if (s_platform == 0 || s_platform->api == 0 || s_shader == 0 ||
        s_platform->api->video_draw_clip_triangles == 0 || triangles == 0u)
        return;
    features = &s_shader->features;
    stride = 4u;
    if (features->diag_rdp_cvg_memory_blend) stride += 6u;
    if (features->opt_world_pos) stride += 3u;
    if (features->opt_dfdx_light) stride += 3u;
    for (int unit = 0; unit < 2; unit++)
    {
        if (!features->used_textures[unit]) continue;
        texture_offset[unit] = stride;
        stride += 2u;
        if (features->clamp[unit][0]) stride++;
        if (features->clamp[unit][1]) stride++;
        if (features->tile_mask[unit][0]) stride++;
        if (features->tile_mask[unit][1]) stride++;
    }
    if (features->opt_fog) { fog_offset = stride; stride += 4u; }
    input_offset = stride;
    stride += (size_t)features->num_inputs *
        (features->opt_alpha ? 4u : 3u);
    if (length < triangles * 3u * stride) return;
    for (int unit = 0; unit < 2; unit++)
        if (features->used_textures[unit] &&
            s_selected_texture[unit] < TITAN_TEXTURE_CAPACITY)
            texture_handle[unit] = s_texture_handles[s_selected_texture[unit]];

    if (!s_trace_first_textured_draw &&
        (texture_handle[0] != 0u || texture_handle[1] != 0u))
        TitanGeRuntime_Trace("40_FIRST_TEXTURED_DRAW_ENTER");

    /* P10.36 binary visual A/B:
     * Preserve GoldenEye's real vertex/triangle/depth/cull pipeline, but remove
     * every texture/sampler/combiner/fog/alpha-test contribution at the final
     * Titan material boundary.  P123 already proved the generic known-texture
     * backend on hardware; this pass asks whether the *GoldenEye geometry*
     * itself is coherent when texture state is absent.
     *
     * If geometry becomes coherent, the remaining corruption is upstream in
     * GoldenEye texture identity/UV/tile/sampler/combiner state.  If it remains
     * mosaic, the fault is in vertex/triangle/transform/depth/cull data rather
     * than texture sampling. */
    /* P10.49: real GoldenEye texture handles are active again.
     * P148 proved that suppressing backend texture upload produced no material
     * performance gain, so keeping textures invisible no longer buys useful
     * diagnostic information. */
    /* P10.37 state-vs-vertex A/B.
     *
     * P10.36 removed texture/sampler/combiner/fog/alpha-test and the real
     * GoldenEye picture remained a chaotic alternating mosaic.  This pass
     * removes the remaining per-draw presentation state while preserving the
     * exact GoldenEye clip-space x/y/z/w values and triangle stream.
     *
     * If geometry becomes coherent here, the corruption is caused by viewport,
     * scissor, depth or blend state.  If it remains mosaic, those state lanes
     * are exonerated and the fault is in clip-space vertex/stride/transform/
     * triangle data before the Titan Game runtime maps vertices to GS space. */
    /* P10.38 binary split:
     * Restore GoldenEye's real viewport + scissor exactly as produced by
     * gfx_pc, while keeping the P137 depth/blend neutralization.
     *
     * - If the mosaic returns, the fault is in viewport/scissor.
     * - If the flat coherent gray/white result remains, viewport/scissor are
     *   exonerated and the culprit is depth and/or blend state. */
    /* P10.39 binary split:
     * P10.38 restored real viewport/scissor and the chaotic mosaic did NOT
     * return.  Therefore viewport/scissor are exonerated.
     *
     * Restore GoldenEye's real depth state exactly as supplied by titan_depth()
     * while keeping blend forced OPAQUE.  Textures/combiner/fog/alpha remain
     * neutralized from P136/P137.
     *
     * - mosaic returns  => depth state/mapping is the culprit.
     * - gray/white stays => depth is exonerated; blend becomes the culprit. */
    /* P10.40 depth write-vs-test split.
     *
     * P10.39 restored GoldenEye's real depth state with blend still forced
     * OPAQUE and the chaotic alternating mosaic returned.  Therefore the
     * remaining fault is in the depth lane.
     *
     * Keep GoldenEye's real depth_test + depth_compare, but force Z writes
     * OFF.  This separates Z-buffer writes/VRAM layout from Z testing/value
     * comparison:
     *
     * - mosaic disappears => Z writes are corrupting presentation; inspect
     *   ZBUF address/PSM/mask/VRAM overlap next.
     * - mosaic remains    => writes are exonerated; inspect Z value mapping,
     *   compare direction and Z-test semantics next. */
    /* P10.41 depth-test engine vs compare/value split.
     *
     * P10.40 kept GoldenEye's real Z-test + real compare with Z writes OFF,
     * and the chaotic mosaic still remained.  Therefore Z writes / Z-buffer
     * corruption are exonerated.
     *
     * Keep GoldenEye's real depth_test enable state, keep Z writes OFF, but
     * force the comparison function to ALWAYS.  This makes an enabled Z-test
     * semantically equivalent to "pass every fragment" while still exercising
     * the Titan/GS depth-test state lane.
     *
     * - mosaic disappears => the bug is in real compare semantics and/or the
     *   incoming Z value mapping used by those comparisons.
     * - mosaic remains => simply enabling the Z-test lane is enough to corrupt
     *   presentation; inspect TEST/ZTE/ZBUF configuration independent of Z
     *   values and comparison direction. */
    /* P10.42 hardware proof for the kernel Z-clear fix.
     *
     * Restore GoldenEye's REAL depth comparison again, but keep depth writes
     * disabled and blend opaque.  With a correct per-frame Z=0 clear, GEQUAL
     * against a write-disabled zero surface must not produce the old mosaic.
     *
     * - gray/white stays => stale Z clear was the root cause.
     * - mosaic returns   => clear is not reaching/clearing the GS Z surface;
     *   inspect the clear packet/ZBUF format next. */
    /* P10.43 post-fix depth validation.
     *
     * P10.42 proved in hardware that the chaotic mosaic was caused by the
     * game Z clear running under GEQUAL instead of ALWAYS.  Restore GoldenEye's
     * complete real depth state now, including Z writes, while keeping blend
     * OPAQUE and texture/combiner/fog/alpha neutralized.
     *
     * If the screen stays stable gray/white, the entire depth lane
     * (clear + test + compare + write) is validated after the kernel fix.
     * If mosaic returns, there is a second independent Z-write issue. */
    /* P10.44 restore real blend after depth fix.
     *
     * P10.43 validated the complete real depth lane after the kernel Z-clear
     * fix: clear + test + compare + write all remain stable with blend forced
     * OPAQUE.  Restore GoldenEye's real blend mode now, while leaving textures,
     * combiner, fog, alpha-test and the constant neutral vertex color disabled
     * exactly as in the previous A/B passes.
     *
     * - stable gray/white => blend is exonerated.
     * - corruption returns => the remaining bug is in blend mode mapping/state. */
    if (!s_p144_real_blend_logged)
    {
        TitanGeRuntime_Trace("P144_REAL_BLEND_FULL_DEPTH_ACTIVE");
        s_p144_real_blend_logged = 1u;
    }

    if (!s_p145_real_vertex_rgba_logged)
    {
        TitanGeRuntime_Trace("P145_REAL_VERTEX_RGBA_ACTIVE");
        s_p145_real_vertex_rgba_logged = 1u;
    }

    if (!s_p143_full_depth_logged)
    {
        TitanGeRuntime_Trace("P143_FULL_REAL_DEPTH_CLEARFIX_ACTIVE");
        s_p143_full_depth_logged = 1u;
    }

    if (!s_p141_ztest_always_logged)
    {
        TitanGeRuntime_Trace("P142_REAL_ZCOMPARE_NO_ZWRITE_CLEARFIX_ACTIVE");
        s_p141_ztest_always_logged = 1u;
    }

    if (!s_p140_no_depth_write_logged)
    {
        TitanGeRuntime_Trace("P140_REAL_ZTEST_NO_ZWRITE_ACTIVE");
        s_p140_no_depth_write_logged = 1u;
    }

    if (!s_p139_real_depth_logged)
    {
        TitanGeRuntime_Trace("P139_REAL_DEPTH_OPAQUE_BLEND_ACTIVE");
        s_p139_real_depth_logged = 1u;
    }

    if (!s_p138_restore_viewport_scissor_logged)
    {
        TitanGeRuntime_Trace("P138_REAL_VIEWPORT_SCISSOR_NEUTRAL_DEPTH_BLEND");
        s_p138_restore_viewport_scissor_logged = 1u;
    }
    /* P10.49: retire the P136 material neutralization.  Preserve the
     * backend's real material state (TEX0 modulation, sampler state, blend,
     * depth, viewport/scissor) so actual uploaded GoldenEye textures reach GS. */
    if (!s_p136_force_untextured_logged)
    {
        TitanGeRuntime_Trace("P136_FORCE_UNTEXTURED_GEOMETRY_ACTIVE");
        s_p136_force_untextured_logged = 1u;
    }
    if (!s_p137_neutral_state_logged)
    {
        s_p137_neutral_state_logged = 1u;
    }
    if (!s_p149_real_textures_logged)
    {
        TitanGeRuntime_Trace("P149_REAL_TEXTURES_ACTIVE");
        s_p149_real_textures_logged = 1u;
    }
    if (!s_trace_first_draw)
        TitanGeRuntime_Trace("22_FIRST_DRAW_BEFORE");
    if (!s_trace_first_textured_draw &&
        (texture_handle[0] != 0u || texture_handle[1] != 0u))
        TitanGeRuntime_Trace("41_FIRST_TEXTURED_RENDER_STATE_BEFORE");
    if (!TitanGePlatform_SetRenderState(s_platform, &s_render_state))
    {
        if (!s_trace_first_draw)
        {
            TitanGeRuntime_Trace("E_FIRST_RENDER_STATE");
            s_trace_first_draw = 1u;
        }
        return;
    }
    if (!s_trace_first_textured_draw &&
        (texture_handle[0] != 0u || texture_handle[1] != 0u))
        TitanGeRuntime_Trace("42_FIRST_TEXTURED_RENDER_STATE_AFTER");

    {
        size_t triangle_base;

        for (triangle_base = 0u; triangle_base < triangles;)
        {
            size_t batch_triangles = triangles - triangle_base;
            size_t local_triangle;

            if (batch_triangles > TITANEYE_MATERIAL_BATCH_TRIANGLES)
                batch_triangles = TITANEYE_MATERIAL_BATCH_TRIANGLES;

            /* Fog colour is currently neutralized by the P136/P145 visual
             * diagnostic.  Keep the batching code production-safe, though:
             * once real fog is restored, a per-triangle fog-colour state
             * change falls back to a one-triangle batch until that lane is
             * redesigned as vertex-only data. */
            if (features->opt_fog && s_render_state.fog_enabled)
                batch_triangles = 1u;

            for (local_triangle = 0u;
                 local_triangle < batch_triangles;
                 local_triangle++)
            {
                TitanGameMaterialVertex *out =
                    &s_titaneye_material_batch[local_triangle * 3u];

                triangle = triangle_base + local_triangle;

                for (vertex = 0u; vertex < 3u; vertex++)
                {
                    const float *in =
                        &vbo[(triangle * 3u + vertex) * stride];
                    const float *inputs = &in[input_offset];
                    size_t input_stride =
                        features->opt_alpha ? 4u : 3u;
                    uint8_t r, g, b, a;
                    /* P162: Q is derived from clip W by the native VU1
                     * backend (or TitanVideoGame_ResolveQ in the EE fallback).
                     * Do not perform the same reciprocal on the producer EE. */
                    const float reciprocal_w = 0.0f;

                    memset(&out[vertex], 0, sizeof(out[vertex]));
                    out[vertex].x = in[0];
                    out[vertex].y = in[1];
                    out[vertex].z = in[2];
                    out[vertex].w = in[3];

                    for (int unit = 0; unit < 2; unit++)
                    {
                        float u = 0.0f, v = 0.0f;

                        if (features->used_textures[unit])
                        {
                            size_t extra = texture_offset[unit] + 2u;
                            float clamp_s = 1.0f, clamp_t = 1.0f;
                            float mask_s = 0.0f, mask_t = 0.0f;

                            u = in[texture_offset[unit]];
                            v = in[texture_offset[unit] + 1u];

                            if (features->clamp[unit][0])
                                clamp_s = in[extra++];
                            if (features->clamp[unit][1])
                                clamp_t = in[extra++];
                            if (features->tile_mask[unit][0])
                                mask_s = in[extra++];
                            if (features->tile_mask[unit][1])
                                mask_t = in[extra++];

                            if (features->clamp[unit][0])
                                u = clamp_s > 0.0f
                                    ? titan_clamp01(u / clamp_s) * clamp_s
                                    : 0.0f;
                            if (features->clamp[unit][1])
                                v = clamp_t > 0.0f
                                    ? titan_clamp01(v / clamp_t) * clamp_t
                                    : 0.0f;
                            if (features->tile_mask[unit][0])
                                u = titan_mask_coordinate(
                                    u,
                                    mask_s,
                                    s_render_state.sampler[unit].wrap_s
                                );
                            if (features->tile_mask[unit][1])
                                v = titan_mask_coordinate(
                                    v,
                                    mask_t,
                                    s_render_state.sampler[unit].wrap_t
                                );
                        }

                        if (unit == 0)
                        {
                            out[vertex].s0 = u;
                            out[vertex].t0 = v;
                            out[vertex].q0 = reciprocal_w;
                        }
                        else
                        {
                            out[vertex].s1 = u;
                            out[vertex].t1 = v;
                            out[vertex].q1 = reciprocal_w;
                        }
                    }

                    r = titan_channel(titan_combiner_channel(
                        features, inputs, input_stride, 0, 0
                    ));
                    g = titan_channel(titan_combiner_channel(
                        features, inputs, input_stride, 0, 1
                    ));
                    b = titan_channel(titan_combiner_channel(
                        features, inputs, input_stride, 0, 2
                    ));
                    a = features->opt_alpha
                        ? titan_channel(titan_combiner_channel(
                            features, inputs, input_stride, 1, 3
                        ))
                        : 255u;

                    if (features->opt_fog)
                    {
                        s_render_state.fog_rgba =
                            ((uint32_t)titan_channel(in[fog_offset]) << 24u) |
                            ((uint32_t)titan_channel(
                                in[fog_offset + 1u]
                            ) << 16u) |
                            ((uint32_t)titan_channel(
                                in[fog_offset + 2u]
                            ) << 8u) |
                            255u;
                        out[vertex].fog =
                            titan_channel(in[fog_offset + 3u]);
                    }

                    if (features->used_textures[1] &&
                        features->num_inputs > 0)
                        out[vertex].lod_fraction =
                            titan_channel(inputs[0]);

                    /* P10.45: real GoldenEye per-vertex RGBA remains active. */
                    out[vertex].rgba =
                        ((uint32_t)r << 24u) |
                        ((uint32_t)g << 16u) |
                        ((uint32_t)b << 8u) |
                        a;
                }
            }

            if (features->opt_fog && s_render_state.fog_enabled &&
                !TitanGePlatform_SetRenderState(
                    s_platform, &s_render_state
                ))
                return;

            {
                int draw_result;
                unsigned int vertex_count =
                    (unsigned int)(batch_triangles * 3u);

                if (!s_trace_first_textured_draw &&
                    (texture_handle[0] != 0u ||
                     texture_handle[1] != 0u))
                    TitanGeRuntime_Trace(
                        "43_FIRST_TEXTURED_PACKET_BEFORE"
                    );

                draw_result = TitanGePlatform_DrawMaterialTriangles(
                    s_platform,
                    s_titaneye_material_batch,
                    vertex_count,
                    texture_handle[0],
                    texture_handle[1]
                );

                if (!s_trace_first_textured_draw &&
                    (texture_handle[0] != 0u ||
                     texture_handle[1] != 0u))
                {
                    TitanGeRuntime_Trace(
                        draw_result >= 0
                            ? "44_FIRST_TEXTURED_PACKET_AFTER"
                            : "E_FIRST_TEXTURED_PACKET"
                    );
                    s_trace_first_textured_draw = 1u;
                }

                if (!s_trace_first_draw)
                {
                    TitanGeRuntime_Trace(
                        draw_result >= 0
                            ? "23_FIRST_DRAW_AFTER"
                            : "E_FIRST_DRAW"
                    );
                    s_trace_first_draw = 1u;
                }

                if (draw_result < 0)
                    return;
            }

            triangle_base += batch_triangles;
        }
    }
}

static bool titan_read(int x, int y, int w, int h, uint8_t *rgb)
{ (void)x; (void)y; (void)w; (void)h; (void)rgb; return false; }
static void titan_init(void)
{
    memset(s_shaders, 0, sizeof(s_shaders));
    memset(s_texture_handles, 0, sizeof(s_texture_handles));
    memset(s_texture_live, 0, sizeof(s_texture_live));
    memset(s_selected_texture, 0, sizeof(s_selected_texture));
    s_shader_count = 0u;
    s_shader = 0;
    s_next_texture = 1u;
    s_upload_texture_unit = 0;
    s_trace_first_upload = 0u;
    s_trace_first_draw = 0u;
    s_trace_first_begin = 0u;
    s_trace_first_end = 0u;
    s_trace_first_upload_detail = 0u;
    s_trace_first_textured_draw = 0u;
    s_trace_postupload_end = 0u;
    s_p136_force_untextured_logged = 0u;
    s_p137_neutral_state_logged = 0u;
    s_p138_restore_viewport_scissor_logged = 0u;
    s_p139_real_depth_logged = 0u;
    s_p140_no_depth_write_logged = 0u;
    s_p141_ztest_always_logged = 0u;
    s_p143_full_depth_logged = 0u;
    s_p144_real_blend_logged = 0u;
    s_p145_real_vertex_rgba_logged = 0u;
    s_p148_texture_upload_bypass_logged = 0u;
    s_p149_real_textures_logged = 0u;
    titan_reset_render_state();
}
static void titan_resize(void) { }
static void titan_start(void)
{
    int result;
    if (!s_platform) return;
    if (!s_trace_first_begin)
        TitanGeRuntime_Trace("14_FIRST_BEGIN_FRAME_BEFORE");
    result = TitanGePlatform_BeginFrame(
        s_platform, s_clear_red, s_clear_green, s_clear_blue
    );
    if (!s_trace_first_begin)
    {
        TitanGeRuntime_Trace(
            result ? "15_FIRST_BEGIN_FRAME_AFTER" : "E_FIRST_BEGIN_FRAME"
        );
        s_trace_first_begin = 1u;
    }
}
static void titan_end(void)
{
    int result;
    if (!s_platform) return;
    if (!s_trace_first_end)
        TitanGeRuntime_Trace("24_FIRST_END_FRAME_BEFORE");
    if (s_trace_first_upload && !s_trace_postupload_end)
        TitanGeRuntime_Trace("50_FIRST_POSTUPLOAD_END_FRAME_BEFORE");
    result = TitanGePlatform_EndFrame(s_platform);
    if (s_trace_first_upload && !s_trace_postupload_end)
    {
        TitanGeRuntime_Trace(
            result ? "51_FIRST_POSTUPLOAD_END_FRAME_AFTER" : "E_FIRST_POSTUPLOAD_END_FRAME"
        );
        s_trace_postupload_end = 1u;
    }
    if (!s_trace_first_end)
    {
        TitanGeRuntime_Trace(
            result ? "25_FIRST_END_FRAME_AFTER" : "E_FIRST_END_FRAME"
        );
        s_trace_first_end = 1u;
    }
}
static void titan_finish(void) { }

struct GfxRenderingAPI gfx_titan_api = {
    titan_z01, titan_unload, titan_load, titan_create, titan_lookup,
    titan_shader_info, titan_new_texture, titan_delete_texture,
    titan_select_texture, titan_upload_texture, titan_sampler, titan_depth,
    titan_viewport, titan_scissor, titan_blend, titan_draw, titan_read,
    titan_init, titan_resize, titan_start, titan_end, titan_finish, 0
};

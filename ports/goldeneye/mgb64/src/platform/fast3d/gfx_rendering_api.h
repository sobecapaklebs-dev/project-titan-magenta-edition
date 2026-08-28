/**
 * gfx_rendering_api.h — Rendering backend vtable.
 * From Emill/n64-fast3d-engine (n64-fast3d-engine license — modified
 * BSD-2-Clause; see src/platform/fast3d/PROVENANCE.md), unmodified.
 */
#ifndef GFX_RENDERING_API_H
#define GFX_RENDERING_API_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct ShaderProgram;

/* Blend modes for set_blend_mode() */
enum GfxBlendMode {
    GFX_BLEND_DISABLED = 0,  /* opaque — no blending */
    GFX_BLEND_ALPHA    = 1,  /* standard alpha: src*srcA + dst*(1-srcA) */
    GFX_BLEND_MODULATE = 2,  /* multiplicative: src*dst (darkening/shadows) */
    GFX_BLEND_ALPHA_COVERAGE = 3, /* alpha blend plus sample coverage */
    GFX_BLEND_ALPHA_CVG_WRAP_STENCIL = 4, /* stencil coverage wrap */
    GFX_BLEND_ALPHA_RDP_MEMORY = 5, /* shader samples memory color */
    GFX_BLEND_ALPHA_RDP_CVG_MEMORY = 6, /* shader coverage + memory color */
};

/* Modern-mesh draw path (W9 scene decoration): a full-fidelity mesh drawn by
 * the backend itself — float32 vertices, u32 indices, mipmapped RGBA8 texture
 * of arbitrary size — into the SAME color/depth attachments as the N64 scene,
 * transformed on the GPU by the interpreter's current MP matrix. Vertex
 * layout is interleaved, stride 36: pos float3 @0, normal float3 @12,
 * uv float2 @24, rgba u8x4 (normalized) @32. `backend_handle` starts NULL;
 * the backend uploads GPU resources on first draw and caches them there. */
struct GfxModernMesh {
    uint32_t mesh_id;      /* loader-assigned, monotonic, never reused --
                              the backend caches GPU resources by this id
                              (struct addresses recycle across level loads) */
    const float *vtx;      /* vtx_count * 9 floats (36-byte stride) */
    uint32_t vtx_count;
    const uint32_t *idx;
    uint32_t idx_count;
    const uint8_t *tex_rgba; /* tex_w * tex_h * 4 */
    int tex_w, tex_h;
    int cutout;            /* alpha-cutout (discard), drawn two-sided */
    void *backend_handle;
};

struct GfxRenderingAPI {
    bool (*z_is_from_0_to_1)(void);
    void (*unload_shader)(struct ShaderProgram *old_prg);
    void (*load_shader)(struct ShaderProgram *new_prg);
    struct ShaderProgram *(*create_and_load_new_shader)(uint64_t shader_id0, uint32_t shader_id1);
    struct ShaderProgram *(*lookup_shader)(uint64_t shader_id0, uint32_t shader_id1);
    void (*shader_get_info)(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]);
    uint32_t (*new_texture)(void);
    void (*delete_texture)(uint32_t texture_id);
    void (*select_texture)(int tile, uint32_t texture_id);
    bool (*upload_texture)(const uint8_t *rgba32_buf, int width, int height);
    void (*set_sampler_parameters)(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt);
    void (*set_depth_mode)(bool depth_test, bool depth_update, bool depth_compare,
                           bool depth_source_prim, uint16_t zmode);
    void (*set_viewport)(int x, int y, int width, int height);
    void (*set_scissor)(int x, int y, int width, int height);
    void (*set_blend_mode)(enum GfxBlendMode mode);
    void (*draw_triangles)(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris);
    bool (*read_framebuffer_rgb)(int x, int y, int width, int height, uint8_t *rgb_out);
    void (*init)(void);
    void (*on_resize)(void);
    void (*start_frame)(void);
    void (*end_frame)(void);
    void (*finish_render)(void);
    /* OPTIONAL (NULL on backends without it — call sites must guard, like
     * read_framebuffer_rgb): draw a modern mesh at the current DL position.
     * mvp = the interpreter's current MP matrix (row-vector convention);
     * fog_* mirror the N64 per-vertex fog curve (see gfx_pc.c fog math). */
    void (*draw_modern_mesh)(struct GfxModernMesh *mesh, const float mvp[4][4],
                             const float fog_color[3], float fog_mul,
                             float fog_offset, int fog_enabled);
};

#endif

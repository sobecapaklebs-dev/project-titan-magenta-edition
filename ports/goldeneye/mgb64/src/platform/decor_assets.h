/*
 * decor_assets.h -- asset side of the scene-decoration layer (Video.SceneDecor).
 *
 * Loads a per-level decor manifest plus the glTF 2.0 models it references and
 * bakes them into interpreter-ready data: native-endian Vtx batches (<= 30
 * vertices, the RSP vertex-cache budget with headroom), RGBA16 textures for
 * the standard G_SETTIMG/G_LOADBLOCK path, and per-model s16 quantization.
 *
 * RENDER-ONLY by contract: nothing here is reachable from the simulation.
 * The game-side emitter is src/game/decor_native.c; the split keeps cgltf +
 * stb_image (platform deps) out of the decomp game sources.
 *
 * Manifest (<dir>/<levelslug>.decor.txt, committable -- plain text):
 *   # comment
 *   model  <name> <path/to/model.glb>        (path relative to the manifest)
 *   place  <name> <x> <y> <z> <yawDeg> <scale>   (world units, scale = world
 *                                                 height of a 1.0-unit model)
 */
#ifndef _DECOR_ASSETS_H_
#define _DECOR_ASSETS_H_

#include <PR/gbi.h>
#include <stddef.h>
#include <stdint.h>

#include "fast3d/gfx_rendering_api.h" /* struct GfxModernMesh */

#define DECOR_MAX_MODELS 16
#define DECOR_MAX_PRIMS 4
#define DECOR_MAX_INSTANCES 96
/* F3D (not F3DEX) vertex loads encode (n-1)<<4|v0 in ONE byte: the RSP
 * vertex cache tops out at 16 verts per gSPVertex. 30 would silently wrap
 * to n=14 and leave triangles referencing stale cache slots. */
#define DECOR_BATCH_VERTS 16

typedef struct DecorTexture {
    uint16_t *rgba16; /* malloc'd, w*h texels, native byte order */
    int w, h;         /* powers of two, <= 256 */
} DecorTexture;

typedef struct DecorBatch {
    Vtx *verts; /* points into the model's registered vertex block */
    int vcount;
    uint8_t (*tris)[3]; /* indices into this batch's verts */
    int tcount;
} DecorBatch;

typedef struct DecorPrim {
    int tex;    /* index into DecorModel.tex */
    int cutout; /* 1 = alpha-cutout, double-sided (TEX_EDGE class) */
    DecorBatch *batches;
    int nbatches;
} DecorPrim;

typedef struct DecorModel {
    char name[32];
    int modern; /* 1 = drawn via G_MODERNMESH (full fidelity, Metal); the
                   N64-path fields below stay empty. Manifest: `model <name>
                   <glb> modern`. */
    struct GfxModernMesh mmesh[DECOR_MAX_PRIMS]; /* owns vtx/idx/tex blocks */
    int nmmesh;
    DecorPrim prims[DECOR_MAX_PRIMS];
    int nprims;
    DecorTexture tex[DECOR_MAX_PRIMS];
    int ntex;
    float vquant;   /* s16 units per model unit (verts were multiplied by it) */
    Vtx *vtx_block; /* one contiguous allocation, registered as a PC region */
    void *batch_pool_base; /* owned; freed by decorAssetsFree */
    void *tri_pool_base;   /* owned; freed by decorAssetsFree */
    int vtx_total;
    int tri_total;
} DecorModel;

typedef struct DecorInstance {
    int model;
    float pos[3]; /* world units (pad/stan coordinate space) */
    float yaw_deg;
    float scale; /* world height of a 1.0-unit-tall model */
} DecorInstance;

typedef struct DecorLevel {
    DecorModel models[DECOR_MAX_MODELS];
    int nmodels;
    DecorInstance inst[DECOR_MAX_INSTANCES];
    int ninst;
    int tri_total; /* across all instances, for the frame budget check */
} DecorLevel;

/* Load <dir>/<slug>.decor.txt and its models. Returns 1 on success, 0 if the
 * manifest is absent or malformed (a warning is printed once per level; the
 * level then simply renders without decor). Vertex blocks are registered with
 * gfx_register_pc_vertex_region. */
int decorAssetsLoadLevel(const char *dir, const char *slug, DecorLevel *out);

void decorAssetsFree(DecorLevel *lvl);

#endif

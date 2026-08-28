/*
 * decor_assets.c -- manifest + glTF loading for the scene-decoration layer.
 * See decor_assets.h for the contract. cgltf (lib/cgltf, MIT) parses the
 * models; stb_image (lib/stb, compiled in texpack_stb.c) decodes textures.
 */
#include "decor_assets.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "stb_image.h"

extern void gfx_register_pc_vertex_region(void *addr, size_t size);

/* ------------------------------------------------------------------ util */

static void decor_warn(const char *what, const char *detail) {
    fprintf(stderr, "[DECOR] WARN %s: %s\n", what, detail ? detail : "");
}

static int is_pow2(int v) { return v > 0 && (v & (v - 1)) == 0; }

/* ------------------------------------------------------------- textures */

static int decor_load_texture(const char *path, DecorTexture *out) {
    int w, h, n;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    if (px == NULL) {
        decor_warn("texture load failed", path);
        return 0;
    }
    /* LOADBLOCK moves at most 4096 texels (12-bit lrs field): 64x64 RGBA16
     * is the ceiling through the standard texture path. */
    if (!is_pow2(w) || !is_pow2(h) || (w * h) > 4096) {
        decor_warn("texture must be pow2 with <= 4096 texels (e.g. 64x64)", path);
        stbi_image_free(px);
        return 0;
    }
    out->rgba16 = malloc((size_t)w * h * 2);
    if (out->rgba16 == NULL) {
        stbi_image_free(px);
        return 0;
    }
    for (int i = 0; i < w * h; i++) {
        unsigned r = px[i * 4 + 0] >> 3, g = px[i * 4 + 1] >> 3;
        unsigned b = px[i * 4 + 2] >> 3, a = px[i * 4 + 3] >= 128;
        uint16_t texel = (uint16_t)((r << 11) | (g << 6) | (b << 1) | a);
        /* BIG-ENDIAN texels: the standard SETTIMG/LOADBLOCK decode path reads
         * N64 texture memory byte order, unlike the native Vtx/DL words. */
        out->rgba16[i] = (uint16_t)((texel >> 8) | (texel << 8));
    }
    out->w = w;
    out->h = h;
    stbi_image_free(px);
    return 1;
}

/* ---------------------------------------------------- modern-path loading */

static uint32_t s_next_mesh_id = 1; /* monotonic; never reused (backend cache key) */

static int decor_load_modern_prim(const cgltf_primitive *pr,
                                  const char *glb_path,
                                  struct GfxModernMesh *out);

/* ------------------------------------------------------------ accessors */

static int read_floats(const cgltf_accessor *acc, int comps, float *dst,
                       size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (!cgltf_accessor_read_float(acc, i, dst + i * comps, comps)) {
            return 0;
        }
    }
    return 1;
}

/* ----------------------------------------------------------- glb loading */

static int decor_load_modern_prim(const cgltf_primitive *pr,
                                  const char *glb_path,
                                  struct GfxModernMesh *out) {
    const cgltf_accessor *pos = NULL, *nrm = NULL, *uv = NULL, *col = NULL;
    for (size_t a = 0; a < pr->attributes_count; a++) {
        const cgltf_attribute *at = &pr->attributes[a];
        if (at->type == cgltf_attribute_type_position) pos = at->data;
        if (at->type == cgltf_attribute_type_normal) nrm = at->data;
        if (at->type == cgltf_attribute_type_texcoord) uv = at->data;
        if (at->type == cgltf_attribute_type_color) col = at->data;
    }
    if (pos == NULL || pr->indices == NULL) {
        decor_warn("modern primitive missing POSITION/indices", glb_path);
        return 0;
    }

    /* texture first (any size up to 4096 per side; RGBA8 straight bytes) */
    const cgltf_material *mat = pr->material;
    if (mat == NULL ||
        mat->pbr_metallic_roughness.base_color_texture.texture == NULL) {
        decor_warn("modern primitive has no baseColor texture", glb_path);
        return 0;
    }
    const cgltf_image *img =
        mat->pbr_metallic_roughness.base_color_texture.texture->image;
    if (img == NULL || img->uri == NULL) {
        decor_warn("modern primitive texture has no uri", glb_path);
        return 0;
    }
    char tpath[1024];
    {
        const char *slash = strrchr(glb_path, '/');
        int dirlen = slash ? (int)(slash - glb_path) + 1 : 0;
        snprintf(tpath, sizeof(tpath), "%.*s%s", dirlen, glb_path, img->uri);
    }
    int tw, th, tn;
    unsigned char *px = stbi_load(tpath, &tw, &th, &tn, 4);
    if (px == NULL || tw < 1 || th < 1 || tw > 4096 || th > 4096) {
        decor_warn("modern texture load failed or oversized", tpath);
        if (px) stbi_image_free(px);
        return 0;
    }

    size_t vc = pos->count;
    size_t ic = pr->indices->count;
    uint8_t *vblob = malloc(vc * 36);
    uint32_t *iblob = malloc(ic * sizeof(uint32_t));
    uint8_t *tblob = malloc((size_t)tw * th * 4);
    if (vblob == NULL || iblob == NULL || tblob == NULL) {
        free(vblob);
        free(iblob);
        free(tblob);
        stbi_image_free(px);
        return 0;
    }
    memcpy(tblob, px, (size_t)tw * th * 4);
    stbi_image_free(px);

    for (size_t i = 0; i < vc; i++) {
        float *f = (float *)(vblob + i * 36);
        uint8_t *c8 = vblob + i * 36 + 32;
        float tmp[4] = {0, 0, 0, 1};
        cgltf_accessor_read_float(pos, i, f, 3);
        if (nrm) {
            cgltf_accessor_read_float(nrm, i, f + 3, 3);
        } else {
            f[3] = 0.0f;
            f[4] = 1.0f;
            f[5] = 0.0f;
        }
        if (uv) {
            cgltf_accessor_read_float(uv, i, f + 6, 2);
        } else {
            f[6] = f[7] = 0.0f;
        }
        if (col) {
            cgltf_accessor_read_float(col, i, tmp, 4);
        }
        c8[0] = (uint8_t)(tmp[0] * 255.0f + 0.5f);
        c8[1] = (uint8_t)(tmp[1] * 255.0f + 0.5f);
        c8[2] = (uint8_t)(tmp[2] * 255.0f + 0.5f);
        c8[3] = (uint8_t)(tmp[3] * 255.0f + 0.5f);
    }
    for (size_t i = 0; i < ic; i++) {
        iblob[i] = (uint32_t)cgltf_accessor_read_index(pr->indices, i);
    }

    memset(out, 0, sizeof(*out));
    out->mesh_id = s_next_mesh_id++;
    out->vtx = (const float *)vblob;
    out->vtx_count = (uint32_t)vc;
    out->idx = iblob;
    out->idx_count = (uint32_t)ic;
    out->tex_rgba = tblob;
    out->tex_w = tw;
    out->tex_h = th;
    out->cutout = (mat->alpha_mode == cgltf_alpha_mode_mask);
    return 1;
}

static int decor_load_model(const char *glb_path, DecorModel *m) {
    cgltf_options opt = {0};
    cgltf_data *data = NULL;
    if (cgltf_parse_file(&opt, glb_path, &data) != cgltf_result_success ||
        cgltf_load_buffers(&opt, data, glb_path) != cgltf_result_success) {
        decor_warn("glb parse failed", glb_path);
        if (data) cgltf_free(data);
        return 0;
    }
    if (data->meshes_count < 1) {
        decor_warn("glb has no mesh", glb_path);
        cgltf_free(data);
        return 0;
    }
    const cgltf_mesh *mesh = &data->meshes[0];
    int nprims = (int)mesh->primitives_count;
    if (nprims > DECOR_MAX_PRIMS) nprims = DECOR_MAX_PRIMS;

    if (m->modern) {
        /* Full-fidelity path (G_MODERNMESH): float verts + big mipmapped
         * textures, no s16 quantization -- vquant 1 keeps the instance
         * matrix in plain model units. */
        m->vquant = 1.0f;
        m->nmmesh = 0;
        for (int p = 0; p < nprims; p++) {
            if (decor_load_modern_prim(&mesh->primitives[p], glb_path,
                                       &m->mmesh[m->nmmesh])) {
                m->tri_total += (int)m->mmesh[m->nmmesh].idx_count / 3;
                m->vtx_total += (int)m->mmesh[m->nmmesh].vtx_count;
                m->nmmesh++;
            }
        }
        cgltf_free(data);
        if (m->nmmesh == 0) {
            decor_warn("modern model has no drawable primitives", glb_path);
            return 0;
        }
        return 1;
    }

    /* pass 1: totals + extent for s16 quantization */
    float ext = 0.0f;
    int vtx_total = 0, tri_total = 0;
    for (int p = 0; p < nprims; p++) {
        const cgltf_primitive *pr = &mesh->primitives[p];
        const cgltf_accessor *pos = NULL;
        for (size_t a = 0; a < pr->attributes_count; a++) {
            if (pr->attributes[a].type == cgltf_attribute_type_position) {
                pos = pr->attributes[a].data;
            }
        }
        if (pos == NULL || pr->indices == NULL) {
            decor_warn("primitive missing POSITION/indices", glb_path);
            cgltf_free(data);
            return 0;
        }
        vtx_total += (int)pos->count;
        tri_total += (int)pr->indices->count / 3;
        for (int k = 0; k < 3; k++) {
            float lo = fabsf(pos->min[k]), hi = fabsf(pos->max[k]);
            if (lo > ext) ext = lo;
            if (hi > ext) ext = hi;
        }
    }
    if (ext <= 0.0f) ext = 1.0f;
    m->vquant = 30000.0f / ext;
    if (getenv("GE007_TRACE_DECOR")) {
        fprintf(stderr, "[DECOR-LOAD] %s ext=%f vquant=%f vtx=%d tri=%d\n",
                glb_path, ext, m->vquant, vtx_total, tri_total);
    }

    /* batching worst case: every batch splits at DECOR_BATCH_VERTS */
    int max_batches = tri_total / (DECOR_BATCH_VERTS / 3) + nprims + 1;
    m->vtx_block = calloc((size_t)tri_total * 3, sizeof(Vtx));
    DecorBatch *batch_pool = calloc((size_t)max_batches, sizeof(DecorBatch));
    uint8_t(*tri_pool)[3] = calloc((size_t)tri_total, sizeof(*tri_pool));
    if (m->vtx_block == NULL || batch_pool == NULL || tri_pool == NULL) {
        free(m->vtx_block);
        free(batch_pool);
        free(tri_pool);
        cgltf_free(data);
        return 0;
    }

    m->batch_pool_base = batch_pool;
    m->tri_pool_base = tri_pool;
    int vtx_used = 0, tri_used = 0, batch_used = 0;
    m->nprims = 0;
    m->ntex = 0;

    for (int p = 0; p < nprims; p++) {
        const cgltf_primitive *pr = &mesh->primitives[p];
        const cgltf_accessor *pos = NULL, *nrm = NULL, *uv = NULL, *col = NULL;
        for (size_t a = 0; a < pr->attributes_count; a++) {
            const cgltf_attribute *at = &pr->attributes[a];
            if (at->type == cgltf_attribute_type_position) pos = at->data;
            if (at->type == cgltf_attribute_type_normal) nrm = at->data;
            if (at->type == cgltf_attribute_type_texcoord) uv = at->data;
            if (at->type == cgltf_attribute_type_color) col = at->data;
        }
        size_t vc = pos->count;
        if (vc > 65536) {
            decor_warn("primitive exceeds 64k vertices; skipped", glb_path);
            continue;
        }
        float *fpos = malloc(vc * 3 * sizeof(float));
        float *fuv = calloc(vc * 2, sizeof(float));
        float *fcol = NULL;
        if (fpos == NULL || fuv == NULL || !read_floats(pos, 3, fpos, vc) ||
            (uv && !read_floats(uv, 2, fuv, vc))) {
            decor_warn("attribute read failed", glb_path);
            free(fpos);
            free(fuv);
            cgltf_free(data);
            return 0;
        }
        if (col) {
            fcol = malloc(vc * 4 * sizeof(float));
            if (fcol == NULL || !read_floats(col, 4, fcol, vc)) {
                free(fcol);
                fcol = NULL;
            }
        }
        (void)nrm; /* lighting is baked into COLOR_0 by the generator */

        /* texture */
        int tex_idx = -1;
        int cutout = 0;
        const cgltf_material *mat = pr->material;
        if (mat && mat->pbr_metallic_roughness.base_color_texture.texture &&
            m->ntex < DECOR_MAX_PRIMS) {
            const cgltf_image *img =
                mat->pbr_metallic_roughness.base_color_texture.texture->image;
            if (img && img->uri) {
                char tpath[1024];
                const char *slash = strrchr(glb_path, '/');
                int dirlen = slash ? (int)(slash - glb_path) + 1 : 0;
                snprintf(tpath, sizeof(tpath), "%.*s%s", dirlen, glb_path,
                         img->uri);
                if (decor_load_texture(tpath, &m->tex[m->ntex])) {
                    tex_idx = m->ntex++;
                }
            }
            cutout = (mat->alpha_mode == cgltf_alpha_mode_mask);
        }
        if (tex_idx < 0) {
            decor_warn("primitive has no usable texture; skipped", glb_path);
            free(fpos);
            free(fuv);
            free(fcol);
            continue;
        }

        DecorPrim *dp = &m->prims[m->nprims++];
        dp->tex = tex_idx;
        dp->cutout = cutout;
        dp->batches = &batch_pool[batch_used];
        dp->nbatches = 0;

        /* greedy vertex-cache batching: remap global->local, split at the
         * cache budget. Vertices are duplicated per batch (tiny models). */
        int tw = m->tex[tex_idx].w, th = m->tex[tex_idx].h;
        size_t ntri = pr->indices->count / 3;
        int local_of[65536];
        DecorBatch *b = NULL;
        for (size_t t = 0; t < ntri; t++) {
            cgltf_size gi[3];
            for (int k = 0; k < 3; k++) {
                gi[k] = cgltf_accessor_read_index(pr->indices, t * 3 + k);
            }
            int need = 0;
            if (b != NULL) {
                for (int k = 0; k < 3; k++) {
                    if (local_of[gi[k]] < 0) need++;
                }
            }
            if (b == NULL || b->vcount + need > DECOR_BATCH_VERTS) {
                b = &batch_pool[batch_used++];
                dp->nbatches++;
                b->verts = m->vtx_block + vtx_used;
                b->vcount = 0;
                b->tris = &tri_pool[tri_used];
                b->tcount = 0;
                memset(local_of, -1, sizeof(int) * vc);
            }
            for (int k = 0; k < 3; k++) {
                if (local_of[gi[k]] < 0) {
                    Vtx *v = &b->verts[b->vcount];
                    const float *P = fpos + gi[k] * 3;
                    v->v.ob[0] = (short)lrintf(P[0] * m->vquant);
                    v->v.ob[1] = (short)lrintf(P[1] * m->vquant);
                    v->v.ob[2] = (short)lrintf(P[2] * m->vquant);
                    v->v.flag = 0;
                    v->v.tc[0] = (short)lrintf(fuv[gi[k] * 2 + 0] * tw * 32.0f);
                    v->v.tc[1] = (short)lrintf(fuv[gi[k] * 2 + 1] * th * 32.0f);
                    if (fcol) {
                        const float *C = fcol + gi[k] * 4;
                        v->v.cn[0] = (u8)lrintf(C[0] * 255.0f);
                        v->v.cn[1] = (u8)lrintf(C[1] * 255.0f);
                        v->v.cn[2] = (u8)lrintf(C[2] * 255.0f);
                        v->v.cn[3] = 255;
                    } else {
                        v->v.cn[0] = v->v.cn[1] = v->v.cn[2] = v->v.cn[3] = 255;
                    }
                    local_of[gi[k]] = b->vcount++;
                    vtx_used++;
                }
            }
            uint8_t *tri = tri_pool[tri_used++];
            b->tcount++;
            for (int k = 0; k < 3; k++) tri[k] = (uint8_t)local_of[gi[k]];
        }
        free(fpos);
        free(fuv);
        free(fcol);
    }
    m->vtx_total = vtx_used;
    m->tri_total = tri_used;
    if (getenv("GE007_TRACE_DECOR")) {
        for (int p = 0; p < m->nprims; p++) {
            for (int b = 0; b < m->prims[p].nbatches; b++) {
                DecorBatch *bt = &m->prims[p].batches[b];
                int mx = 0;
                for (int t = 0; t < bt->tcount; t++) {
                    for (int k = 0; k < 3; k++) {
                        if (bt->tris[t][k] > mx) mx = bt->tris[t][k];
                    }
                }
                fprintf(stderr,
                        "[DECOR-BATCH] prim=%d cutout=%d batch=%d verts@+%ld "
                        "vcount=%d tcount=%d max_idx=%d v0_ob=(%d,%d,%d)\n",
                        p, m->prims[p].cutout, b,
                        (long)(bt->verts - m->vtx_block), bt->vcount,
                        bt->tcount, mx, bt->verts[0].v.ob[0],
                        bt->verts[0].v.ob[1], bt->verts[0].v.ob[2]);
            }
        }
    }
    cgltf_free(data);
    if (m->nprims == 0) {
        decor_warn("model has no drawable primitives", glb_path);
        return 0;
    }
    gfx_register_pc_vertex_region(m->vtx_block,
                                  (size_t)tri_total * 3 * sizeof(Vtx));
    return 1;
}

/* ------------------------------------------------------------- manifest */

int decorAssetsLoadLevel(const char *dir, const char *slug, DecorLevel *out) {
    char path[1024];
    memset(out, 0, sizeof(*out));
    snprintf(path, sizeof(path), "%s/%s.decor.txt", dir, slug);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return 0; /* silently: most levels simply have no decor */
    }
    char line[1024];
    int lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char kw[16], name[32], rest[960];
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (sscanf(line, "%15s", kw) != 1) continue;
        if (strcmp(kw, "model") == 0) {
            if (out->nmodels >= DECOR_MAX_MODELS) {
                decor_warn("too many models; extra ignored", path);
                continue;
            }
            char kind[16] = "";
            if (sscanf(line, "%*s %31s %959s %15s", name, rest, kind) < 2) {
                decor_warn("bad model line", line);
                continue;
            }
            char glb[1024];
            snprintf(glb, sizeof(glb), "%s/%s", dir, rest);
            DecorModel *m = &out->models[out->nmodels];
            memset(m, 0, sizeof(*m));
            snprintf(m->name, sizeof(m->name), "%s", name);
            m->modern = (strcmp(kind, "modern") == 0);
            if (decor_load_model(glb, m)) {
                out->nmodels++;
            }
        } else if (strcmp(kw, "place") == 0) {
            float x, y, z, yaw, scale;
            if (sscanf(line, "%*s %31s %f %f %f %f %f", name, &x, &y, &z, &yaw,
                       &scale) != 6) {
                decor_warn("bad place line", line);
                continue;
            }
            int mi = -1;
            for (int i = 0; i < out->nmodels; i++) {
                if (strcmp(out->models[i].name, name) == 0) mi = i;
            }
            if (mi < 0) {
                decor_warn("place references unknown model", name);
                continue;
            }
            if (out->ninst >= DECOR_MAX_INSTANCES) {
                decor_warn("too many instances; extra ignored", path);
                continue;
            }
            DecorInstance *in = &out->inst[out->ninst++];
            in->model = mi;
            in->pos[0] = x;
            in->pos[1] = y;
            in->pos[2] = z;
            in->yaw_deg = yaw;
            in->scale = scale;
            out->tri_total += out->models[mi].tri_total;
        }
    }
    fclose(f);
    fprintf(stderr,
            "[DECOR] %s: %d model(s), %d instance(s), %d triangles/frame\n",
            path, out->nmodels, out->ninst, out->tri_total);
    return out->ninst > 0;
}

void decorAssetsFree(DecorLevel *lvl) {
    for (int i = 0; i < lvl->nmodels; i++) {
        DecorModel *m = &lvl->models[i];
        /* vertex regions stay registered (bounded, 64 max); the blocks are
         * freed only never -- levels reload rarely and regions cannot be
         * unregistered. Keep the block alive to keep the region valid. */
        for (int t = 0; t < m->ntex; t++) {
            free(m->tex[t].rgba16);
        }
        for (int t = 0; t < m->nmmesh; t++) {
            free((void *)m->mmesh[t].vtx);
            free((void *)m->mmesh[t].idx);
            free((void *)m->mmesh[t].tex_rgba);
        }
        free(m->batch_pool_base);
        free(m->tri_pool_base);
    }
    memset(lvl, 0, sizeof(*lvl));
}

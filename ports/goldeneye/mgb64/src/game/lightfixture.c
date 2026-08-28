#include <ultra64.h>
#include "lightfixture.h"
#include "bg.h"
#include "unk_0A1DA0.h"
/* getRoomPositionScaledByIndex is void-returning (unk_0BC530.c); without this
 * header lightfixture.c saw an implicit int-returning declaration — harmless
 * on native (return unused) but a wasm function-signature mismatch. */
#include "unk_0BC530.h"
#include <bondconstants.h>
#include <assets/image_externs.h>
#include <PR/gbi.h>

#define LIGHTFIXTURE_TABLE_MAX 0x64
#define DARKENED_LIGHT_TABLE_MAX 0x200

// bss
//CODE.bss:80082660
s_lightfixture light_fixture_table[LIGHTFIXTURE_TABLE_MAX];
//CODE.bss:80082B10
s16 cur_entry_lightfixture_table;
//CODE.bss:80082B12
s16 index_of_cur_entry_lightfixture_table;
//CODE.bss:80082B14                     .align 3
//CODE.bss:80082B18
struct s_darkened_light darkened_light_table[DARKENED_LIGHT_TABLE_MAX]; // a table containing the vertices of lights that were shot, and therefore, darkened
//CODE.bss:80083318
s32 dword_CODE_bss_80083318;

// data
//D:80046030
s32 cur_entry_darkened_light_table = 0;

s32 D_80046034[] = {0, 0, 0, 0, 0, 0, 0};

static s32 darkened_light_table_contains_vertex(Vtx *vertex, s32 room_index);


void init_lightfixture_tables(void)
{
    s32 i;

    for (i = 0; i < LIGHTFIXTURE_TABLE_MAX; i++)
    {
        light_fixture_table[i].room_index = 0;
    }

    for (i = 0; i < DARKENED_LIGHT_TABLE_MAX; i++)
    {
        darkened_light_table[i].room_index = 0;
    }

    cur_entry_darkened_light_table = 0;
}


s32 get_index_of_current_entry_in_init_lightfixture_table(void)
{
    s32 i;

    for (i = 0; i != LIGHTFIXTURE_TABLE_MAX; i++)
    {
        if (light_fixture_table[i].room_index == 0)
        {
            return i;
        }
    }
    return LIGHTFIXTURE_TABLE_MAX;
}


void add_entry_to_init_lightfixture_table(Gfx *DL)
{
    cur_entry_lightfixture_table = get_index_of_current_entry_in_init_lightfixture_table();
    if (cur_entry_lightfixture_table != LIGHTFIXTURE_TABLE_MAX)
    {
        light_fixture_table[cur_entry_lightfixture_table].room_index = index_of_cur_entry_lightfixture_table;
        light_fixture_table[cur_entry_lightfixture_table].ptr_start_pertinent_DL = DL;
    }
}


void save_ptrDL_enpoint_to_current_init_lightfixture_table(Gfx *dl_end)
{
    if (cur_entry_lightfixture_table != LIGHTFIXTURE_TABLE_MAX)
    {
        light_fixture_table[cur_entry_lightfixture_table].ptr_end_pertinent_DL = dl_end;
    }
}


s32 check_if_imageID_is_light(s32 imageID)
{
    if ((imageID == IMAGE_WALL_LAMP)     ||
        (imageID == IMAGE_203_LIGHT)     ||
        (imageID == IMAGE_205_LIGHT)     ||
        (imageID == IMAGE_252_LIGHT)     ||
        (imageID == IMAGE_PANEL_LAMP)    ||
        (imageID == IMAGE_255_LIGHT)     ||
        (imageID == IMAGE_256_LIGHT)     ||
        (imageID == IMAGE_HANGING_LAMP)  ||
        (imageID == IMAGE_NEON_LAMP)     ||
        (imageID == IMAGE_LINEAR_LAMP))
    {
        // Will darken when shot
        return 1;
    } 
    else
    {
        return 0;
    }
}


#ifdef NATIVE_PORT
/* "Shoot out the lights": darken a light fixture's surface when shot. Default
 * ON (faithfulness restoration, survey class C3) — verified W4.E1.T2: 6,210
 * fixture triangles swept across bunker1/silo/control, vtx_base_offset==0
 * everywhere, 0 DIVERGE. GE007_SHOOT_OUT_LIGHTS=0 is the A/B escape hatch: when
 * off, the parent effect fn and the room-load population pass both early-return,
 * so the tables stay empty and the render is byte-identical to the previous
 * stub. See docs/design/SHOOT_OUT_LIGHTS_PLAN.md. */
static int s_ge007_shoot_lights = -1;
int ge007_shoot_out_lights_enabled(void)
{
    if (s_ge007_shoot_lights < 0) {
        const char *e = getenv("GE007_SHOOT_OUT_LIGHTS");
        s_ge007_shoot_lights = (e != NULL && e[0] == '0') ? 0 : 1;   /* default ON; =0 is the A/B escape hatch */
    }
    return s_ge007_shoot_lights;
}

/* Read a big-endian 32-bit word from a raw N64 display-list byte stream. The
 * room DLs the renderer/hit-test walk are raw big-endian F3DEX bytes, so we
 * cannot use the host-endian Gfx union to parse them. Mirrors bgReadBe32. */
static u32 lf_read_be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

/* Start of the room DL buffer that CONTAINS cmd (primary or secondary), or NULL
 * if cmd lies in neither. Fixture runs are registered from BOTH DLs
 * (lf_populate_room_lightfixtures), and the two buffers are separate
 * allocations, so a backward walk bounded by the PRIMARY start while standing
 * in the SECONDARY buffer either escapes the allocation or bails spuriously
 * depending on heap layout — the walk must be bounded by its own buffer. */
static const u8 *lf_containing_dl_start(const u8 *cmd, s32 room_index)
{
    const u8 *prim = (const u8 *)g_BgRoomInfo[room_index].ptr_expanded_mapping_info;
    const u8 *sec  = (const u8 *)g_BgRoomInfo[room_index].ptr_secondary_expanded_mapping_info;

    if (prim != NULL && cmd >= prim &&
        cmd < prim + g_BgRoomInfo[room_index].usize_primary_DL_binary) {
        return prim;
    }
    if (sec != NULL && cmd >= sec &&
        cmd < sec + g_BgRoomInfo[room_index].usize_secondary_DL_binary) {
        return sec;
    }
    return NULL;
}

Vtx * return_ptr_vertex_of_entry_room(Gfx * gfx, s32 room_index)
{
    /* Walk backward (8-byte DL command stride, NOT sizeof(Gfx)==16 on native) to
     * the governing G_VTX (0x04); its dma.addr is the vertex-pool segment addr.
     * Rebase segment 0x0E (SPSEGMENT_BG_VTX) onto the room's vertex pool, in
     * uintptr_t (the reference's (s32) cast truncates 64-bit pointers). */
    const u8 *cmd = (const u8 *)gfx;
    const u8 *dl_start = lf_containing_dl_start(cmd, room_index);

    if (dl_start == NULL) { return NULL; } /* not in this room's primary/secondary DL */

    while (cmd[0] != 0x04 /* G_VTX */) {
        cmd -= 8;
        if (cmd < dl_start) { return NULL; } /* guard: malformed / no leading G_VTX */
    }

    u32 addr = lf_read_be32(cmd + 4);
    if ((addr & 0xFF000000u) == 0x0E000000u) {
        return (Vtx *)((uintptr_t)g_BgRoomInfo[room_index].ptr_point_index + (addr & 0x00FFFFFFu));
    }
    return (Vtx *)(uintptr_t)addr;
}
#else
Vtx * return_ptr_vertex_of_entry_room(Gfx * gfx, s32 room_index)
{
    Vtx * ret;

    while (gfx->dma.cmd != G_VTX ){ gfx--; }

    ret = gfx->dma.addr;

    // weird memory checking, not sure what's going on here
    if (((s32) ret & 0xFF000000) == 0x0E000000) {
        ret = (s32)g_BgRoomInfo[room_index].ptr_point_index + ((s32) ret & 0xFFFFFF);
    }

    return ret;
}
#endif


#ifdef NATIVE_PORT
void extract_vertex_indices_from_triangle(Gfx* gfx, u32 tri_type, s32* idx1, s32* idx2, s32* idx3)
{
    /* Raw big-endian F3DEX decode. G_TRI1 (0xBF): indices at bytes 5,6,7 each /10
     * (base-GBI DMEM stride). G_TRI4 (0xB1): raw 4-bit indices, NO /10, for
     * sub-triangle k = tri_type-1. Matches the authoritative collision walker
     * (bg.c G_TRI1 ~:11369, G_TRI4 decode). */
    const u8 *cmd = (const u8 *)gfx;
    if (tri_type == 0) {
        *idx1 = cmd[5] / 10;
        *idx2 = cmd[6] / 10;
        *idx3 = cmd[7] / 10;
    } else {
        s32 k  = (s32)tri_type - 1;          /* 0..3 */
        u32 w0 = lf_read_be32(cmd + 0);
        u32 w1 = lf_read_be32(cmd + 4);
        *idx1 = (s32)((w1 >> (8 * k))     & 0xF);
        *idx2 = (s32)((w1 >> (8 * k + 4)) & 0xF);
        *idx3 = (s32)((w0 >> (4 * k))     & 0xF);
    }
}
#else
void extract_vertex_indices_from_triangle(Gfx* gfx, u32 tri_type, s32* idx1, s32* idx2, s32* idx3)
{
    switch (tri_type) {
        case 0:
            *idx1 = (s32) gfx->tri.tri.v[0] / 10;
            *idx2 = (s32) gfx->tri.tri.v[1] / 10;
            *idx3 = (s32) gfx->tri.tri.v[2] / 10;
            break;
        // unsure of how to cleanly access the below versions
        case 1:
            *idx1 = ((u32*)gfx)[1] & 0xF;
            *idx2 = ((((u8*)gfx)[7]) & 0xFFFFFFFFu) >> 4;
            *idx3 = ((s32*)gfx)[0] & 0xF;
            break;
        case 2:
            *idx1 = ((u8*)gfx)[6] & 0xF;
            *idx2 = ((((u16*)gfx)[3]) & 0xFFFFFFFFu) >> 0xC;
            *idx3 = ((((u8*)gfx)[3]) & 0xFFFFFFFFu) >> 4;
            break;
        case 3:
            *idx1 = ((u16*)gfx)[2] & 0xF;
            *idx2 = ((((u8*)gfx)[5]) & 0xFFFFFFFFu) >> 4;
            *idx3 = ((u8*)gfx)[2] & 0xF;
            break;
        case 4:
            *idx1 = ((u8*)gfx)[4] & 0xF;
            *idx2 = ((u32*)gfx)[1] >> 0x1C;
            *idx3 = ((((u16*)gfx)[1]) & 0xFFFFFFFFu) >> 0xC;
            break;
    }
}
#endif /* !NATIVE_PORT */


void extract_vertex_coords_from_triangle(Gfx * gfx, u32 tri_type, s32 room_index, coord16 * out1, coord16 * out2, coord16 * out3)
{
    s32 idx1;
    s32 idx2;
    s32 idx3;
    Vtx * vertices;

    extract_vertex_indices_from_triangle(gfx, tri_type, &idx1, &idx2, &idx3);
    vertices = return_ptr_vertex_of_entry_room(gfx, room_index);
    if (vertices == NULL) { /* walk bailed — zero coords rather than deref */
        out1->AsArray[0] = out1->AsArray[1] = out1->AsArray[2] = 0;
        out2->AsArray[0] = out2->AsArray[1] = out2->AsArray[2] = 0;
        out3->AsArray[0] = out3->AsArray[1] = out3->AsArray[2] = 0;
        return;
    }

    out1->AsArray[0] = (s16) vertices[idx1].v.ob[0];
    out1->AsArray[1] = (s16) vertices[idx1].v.ob[1];
    out1->AsArray[2] = (s16) vertices[idx1].v.ob[2];

    out2->AsArray[0] = (s16) vertices[idx2].v.ob[0];
    out2->AsArray[1] = (s16) vertices[idx2].v.ob[1];
    out2->AsArray[2] = (s16) vertices[idx2].v.ob[2];

    out3->AsArray[0] = (s16) vertices[idx3].v.ob[0];
    out3->AsArray[1] = (s16) vertices[idx3].v.ob[1];
    out3->AsArray[2] = (s16) vertices[idx3].v.ob[2];
}


void redarken_lights_in_room(s32 room_index)
{
    Vtx * vertex;
    s32 i;
    struct s_darkened_light* unk;

    vertex = g_BgRoomInfo[room_index].ptr_point_index;

    for (i = 0; i < DARKENED_LIGHT_TABLE_MAX; i++)
    {
        unk = &darkened_light_table[i];

        if (room_index != unk->room_index) { continue; }

        vertex[unk->vtx_index].v.cn[0] >>= 2;
        vertex[unk->vtx_index].v.cn[1] >>= 2;
        vertex[unk->vtx_index].v.cn[2] >>= 2;
        vertex[unk->vtx_index].v.cn[3] >>= 2;
    }
}


void darken_vertex_in_room(Vtx * vertex, s32 room_index)
{
    s32 vtx_index;

    if (darkened_light_table_contains_vertex(vertex, room_index) != 0) { return; }

    // weird memory stuff going on here
    vtx_index = (s32)(((uintptr_t)vertex - (uintptr_t)g_BgRoomInfo[room_index].ptr_point_index) >> 4);

    darkened_light_table[cur_entry_darkened_light_table].room_index = (u16) room_index;
    darkened_light_table[cur_entry_darkened_light_table].vtx_index = vtx_index;

    vertex->v.cn[0] >>= 2;
    vertex->v.cn[1] >>= 2;
    vertex->v.cn[2] >>= 2;
    vertex->v.cn[3] >>= 2;

    cur_entry_darkened_light_table++;

    if (cur_entry_darkened_light_table >= DARKENED_LIGHT_TABLE_MAX)
    {
        cur_entry_darkened_light_table = 0;
    }
}


static s32 darkened_light_table_contains_vertex(Vtx * vertex, s32 room_index)
{
    uintptr_t vtx_index;
    s32 i;

    // weird memory stuff going on here
    vtx_index = ((uintptr_t)vertex - (uintptr_t)g_BgRoomInfo[room_index].ptr_point_index) >> 4;

    for (i = 0; i < DARKENED_LIGHT_TABLE_MAX; i++)
    {
        if ((room_index == darkened_light_table[i].room_index) && ((s32)vtx_index == darkened_light_table[i].vtx_index))
        {
            return TRUE;
        }
    }

    return FALSE;
}


#ifdef NATIVE_PORT
#include <stdio.h>
#include <stdint.h>
/* GE007_LF_VERIFY=1 — TEMPORARY debug cross-check (W4.E1.T2, plan §8 risk #1).
 * The collision hit test walks to the governing G_VTX and subtracts
 * vtx_base_offset (= coll_dl[1] & 0xF) from EVERY decoded vertex index
 * (bg.c:11461-11463 for G_TRI1, :11581-11583 for G_TRI4) before forming the
 * absolute Vertex* it stores in the hit record. extract_vertex_indices_from_triangle
 * here does NOT subtract it, and both routes rebase onto the SAME base pointer
 * (return_ptr_vertex_of_entry_room and the hit test both walk to the same 0x04
 * G_VTX and add its word4 offset to ptr_point_index; vtx_base == ptr_point_index,
 * bg.c:11341). So the two absolute Vtx* diverge by exactly vtx_base_offset*sizeof(Vtx)
 * whenever the offset is nonzero — which would darken the wrong surface. This logs
 * both pointers + indices so a real aimed shot reveals whether vbo is ever nonzero
 * for a shot fixture. Default-off; costs nothing when the flag is unset. */
static int lf_verify_enabled(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("GE007_LF_VERIFY");
        v = (e != NULL && e[0] == '1') ? 1 : 0;
    }
    return v;
}

static void lf_verify_vertex_base(Gfx *gfx, u32 tri_type, s32 room_index,
                                  Vtx *vertices, s32 idx1, s32 idx2, s32 idx3)
{
    const u8 *cmd = (const u8 *)gfx;
    const u8 *dl_start = lf_containing_dl_start(cmd, room_index);
    const u8 *gvtx = cmd;
    s32 vbo, a[3], i;

    if (vertices == NULL || dl_start == NULL) { return; }
    a[0] = idx1; a[1] = idx2; a[2] = idx3;

    /* Recover vtx_base_offset the same way the hit test does: the governing
     * G_VTX (0x04) is the nearest preceding 8-byte-stride command. */
    while (gvtx[0] != 0x04 /* G_VTX */) {
        gvtx -= 8;
        if (gvtx < dl_start) {
            fprintf(stderr, "[LF-VERIFY] room=%d tri_type=%u no-G_VTX (skip)\n",
                    (int)room_index, (unsigned)tri_type);
            return;
        }
    }
    vbo = gvtx[1] & 0xF;

    for (i = 0; i < 3; i++) {
        Vtx *darken_ptr = &vertices[a[i]];            /* what this file darkens */
        Vtx *hit_ptr    = &vertices[a[i] - vbo];      /* what the hit test targets */
        long delta_slots = (long)(((intptr_t)darken_ptr - (intptr_t)hit_ptr) /
                                  (intptr_t)sizeof(Vtx));
        fprintf(stderr,
            "[LF-VERIFY] room=%d tri_type=%u v%d vbo=%d darkenIdx=%d hitIdx=%d "
            "darkenPtr=%p hitPtr=%p deltaSlots=%ld %s\n",
            (int)room_index, (unsigned)tri_type, (int)i, (int)vbo,
            (int)a[i], (int)(a[i] - vbo),
            (void *)darken_ptr, (void *)hit_ptr, delta_slots,
            (darken_ptr == hit_ptr) ? "EQUAL" : "DIVERGE");
    }
}

/* Static verify sweep (headless, GE007_LF_VERIFY=1): drive the exact darken-path
 * vertex-base computation (return_ptr_vertex_of_entry_room + extract_vertex_indices)
 * over EVERY populated light-fixture triangle in a room and cross-check against the
 * hit-record route. This is the deterministic, no-aiming equivalent of shooting the
 * fixture — a strict superset of "5 shot fixtures" (§8 risk #1). Restricted to
 * PRIMARY-DL spans: return_ptr_vertex_of_entry_room bounds its backward G_VTX walk
 * with the primary DL start (ptr_expanded_mapping_info), so sweeping a secondary-DL
 * span would be an OOB read. Secondary-DL runs are reported separately (see risk #4). */
void lf_verify_sweep_room(s32 room_index)
{
    s32 i, j;

    if (!lf_verify_enabled()) { return; }
    if (room_index <= 0) { return; }

    for (i = 0; i < LIGHTFIXTURE_TABLE_MAX; i++) {
        const u8 *p, *end;

        if (light_fixture_table[i].room_index != room_index) { continue; }
        p   = (const u8 *)light_fixture_table[i].ptr_start_pertinent_DL;
        end = (const u8 *)light_fixture_table[i].ptr_end_pertinent_DL;
        if (p == NULL || end == NULL || end <= p) { continue; }

        /* Both primary AND secondary spans are sweepable now that the backward
         * G_VTX walk is bounded by its own containing buffer (risk #4 closed).
         * A span in neither buffer is malformed — report and skip. */
        if (lf_containing_dl_start(p, room_index) == NULL) {
            fprintf(stderr, "[LF-VERIFY] room=%d SKIP span [%p,%p) outside both DLs\n",
                    (int)room_index, (void *)p, (void *)end);
            continue;
        }

        for (; p + 8 <= end; p += 8) {
            s32 idx1, idx2, idx3;
            Vtx *vertices;
            if (p[0] == 0xBF /* G_TRI1 */) {
                extract_vertex_indices_from_triangle((Gfx *)p, 0U, &idx1, &idx2, &idx3);
                vertices = return_ptr_vertex_of_entry_room((Gfx *)p, room_index);
                lf_verify_vertex_base((Gfx *)p, 0U, room_index, vertices, idx1, idx2, idx3);
            } else if (p[0] == 0xB1 /* G_TRI4 */) {
                for (j = 0; j < 4; j++) {
                    extract_vertex_indices_from_triangle((Gfx *)p, (u32)(j + 1), &idx1, &idx2, &idx3);
                    vertices = return_ptr_vertex_of_entry_room((Gfx *)p, room_index);
                    lf_verify_vertex_base((Gfx *)p, (u32)(j + 1), room_index, vertices, idx1, idx2, idx3);
                }
            }
        }
    }
}
#endif /* NATIVE_PORT */

void darken_triangle_in_room(Gfx *gfx, u32 tri_type, s32 room_index)
{
    Vtx * vertex;
    s32 idx1;
    s32 idx2;
    s32 idx3;
    Vtx * vertices;

    extract_vertex_indices_from_triangle(gfx, tri_type, &idx1, &idx2, &idx3);
    vertices = return_ptr_vertex_of_entry_room(gfx, room_index);
    if (vertices == NULL) { return; } /* walk bailed (no governing G_VTX) — skip, don't deref */

#ifdef NATIVE_PORT
    if (lf_verify_enabled()) {
        lf_verify_vertex_base(gfx, tri_type, room_index, vertices, idx1, idx2, idx3);
    }
#endif

    darken_vertex_in_room(&vertices[idx1], room_index);
    darken_vertex_in_room(&vertices[idx2], room_index);

    vertex = &vertices[idx3];
    darken_vertex_in_room(vertex, room_index);
}


s32 darkened_light_table_contains_triangle(Gfx * gfx, u32 tri_type, s32 room_index)
{
    s32 out3;
    s32 idx1;
    s32 idx2;
    s32 idx3;
    Vtx * vertices;
    s32 out2;
    s32 out1;

    extract_vertex_indices_from_triangle(gfx, tri_type, &idx1, &idx2, &idx3);
    vertices = return_ptr_vertex_of_entry_room(gfx, room_index);
    if (vertices == NULL) { return 0; } /* walk bailed — treat as not-yet-darkened */
    out1 = darkened_light_table_contains_vertex(&vertices[idx2], room_index);
    out2 = darkened_light_table_contains_vertex(&vertices[idx1], room_index);
    out3 = darkened_light_table_contains_vertex(&vertices[idx3], room_index);
    return out3 + out2 + out1;
}


s32 sub_GAME_7F0BBCCC(coord16 * coord, s32 room_index)
{
    s32 var_s0;
    s32 var_s1;
    s32 i;
    s32 var_s2;
    Vtx * vertex;

    i = 0;
    do
    {
        if (room_index == darkened_light_table[i].room_index)
        {
            vertex = &g_BgRoomInfo[room_index].ptr_point_index[darkened_light_table[i].vtx_index];

            var_s0 = vertex->v.ob[0] - coord->AsArray[0];
            var_s1 = vertex->v.ob[1] - coord->AsArray[1];
            var_s2 = vertex->v.ob[2] - coord->AsArray[2];

            if (var_s0 < 0) { var_s0 = -var_s0; }
            if (var_s1 < 0) { var_s1 = -var_s1; }
            if (var_s2 < 0) { var_s2 = -var_s2; }

            if ((var_s0 + var_s1 + var_s2) < (s32) (get_room_data_float1() * 100.0f))
            {
                return 1;
            }
        }
    } while (++i < DARKENED_LIGHT_TABLE_MAX);

    return 0;
}


#ifdef NATIVE_PORT
void sub_GAME_7F0BBE0C(Gfx * gfx, u32 tri_type, s32 room_index)
{
    s16 diff_z_12, diff_z_13, diff_z_23;
    s16 diff_y_12, diff_y_13, diff_y_23;
    s16 diff_x_12, diff_x_13, diff_x_23;
    coord16 coord1, coord2, coord3, coord4, coord5, coord6;
    f32 dist_tween, inv_dist_12, inv_dist_23, inv_dist_13, dist_nn;
    coord3d origin, calc_coord;
    s32 i, j;
    s8 exec, exec2;

    /* Default-off A/B: byte-identical to the previous stub when disabled. */
    if (!ge007_shoot_out_lights_enabled()) { return; }

    for (i = 0; i < LIGHTFIXTURE_TABLE_MAX; i++)
    {
        if (room_index != light_fixture_table[i].room_index) { continue; }
        if (gfx <  light_fixture_table[i].ptr_start_pertinent_DL) { continue; }
        if (gfx >= light_fixture_table[i].ptr_end_pertinent_DL)   { continue; }

        if (darkened_light_table_contains_triangle(gfx, tri_type, light_fixture_table[i].room_index) != 0) { return; }

        darken_triangle_in_room(gfx, tri_type, light_fixture_table[i].room_index);
        extract_vertex_coords_from_triangle(gfx, tri_type, light_fixture_table[i].room_index, &coord1, &coord2, &coord3);

        diff_x_12 = coord1.AsArray[0] - coord2.AsArray[0];
        diff_x_23 = coord1.AsArray[0] - coord3.AsArray[0];
        diff_x_13 = coord2.AsArray[0] - coord3.AsArray[0];

        diff_y_12 = coord1.AsArray[1] - coord2.AsArray[1];
        diff_y_23 = coord1.AsArray[1] - coord3.AsArray[1];
        diff_y_13 = coord2.AsArray[1] - coord3.AsArray[1];

        diff_z_12 = coord1.AsArray[2] - coord2.AsArray[2];
        diff_z_23 = coord1.AsArray[2] - coord3.AsArray[2];
        diff_z_13 = coord2.AsArray[2] - coord3.AsArray[2];

        dist_nn = sqrtf((diff_x_12 * diff_x_12) + (diff_y_12 * diff_y_12) + (diff_z_12 * diff_z_12));
        inv_dist_12 = 10.0f / (get_room_data_float2() * dist_nn);

        dist_nn = sqrtf((diff_x_23 * diff_x_23) + (diff_y_23 * diff_y_23) + (diff_z_23 * diff_z_23));
        inv_dist_23 = 10.0f / (get_room_data_float2() * dist_nn);

        dist_nn = sqrtf((diff_x_13 * diff_x_13) + (diff_y_13 * diff_y_13) + (diff_z_13 * diff_z_13));
        inv_dist_13 = 10.0f / (get_room_data_float2() * dist_nn);

        getRoomPositionScaledByIndex(light_fixture_table[i].room_index, &origin);

        for (dist_tween = 0.0f; dist_tween < 1.0f; dist_tween += inv_dist_12)
        {
            calc_coord.x = ((coord2.AsArray[0] + (diff_x_12 * dist_tween)) * get_room_data_float2()) + origin.f[0];
            calc_coord.y = ((coord2.AsArray[1] + (diff_y_12 * dist_tween)) * get_room_data_float2()) + origin.f[1];
            calc_coord.z = ((coord2.AsArray[2] + (diff_z_12 * dist_tween)) * get_room_data_float2()) + origin.f[2];
            sub_GAME_7F0A2160(&calc_coord, 0.0f, 10.0f);
        }

        for (dist_tween = 0.0f; dist_tween < 1.0f; dist_tween += inv_dist_23)
        {
            calc_coord.x = ((coord3.AsArray[0] + (diff_x_23 * dist_tween)) * get_room_data_float2()) + origin.f[0];
            calc_coord.y = ((coord3.AsArray[1] + (diff_y_23 * dist_tween)) * get_room_data_float2()) + origin.f[1];
            calc_coord.z = ((coord3.AsArray[2] + (diff_z_23 * dist_tween)) * get_room_data_float2()) + origin.f[2];
            sub_GAME_7F0A2160(&calc_coord, 0.0f, 10.0f);
        }

        for (dist_tween = 0.0f; dist_tween < 1.0f; dist_tween += inv_dist_13)
        {
            calc_coord.x = ((coord3.AsArray[0] + (diff_x_13 * dist_tween)) * get_room_data_float2()) + origin.f[0];
            calc_coord.y = ((coord3.AsArray[1] + (diff_y_13 * dist_tween)) * get_room_data_float2()) + origin.f[1];
            calc_coord.z = ((coord3.AsArray[2] + (diff_z_13 * dist_tween)) * get_room_data_float2()) + origin.f[2];
            sub_GAME_7F0A2160(&calc_coord, 0.0f, 10.0f);
        }

        /* Neighbour scan: raw 8-byte DL command stride (NOT gfx2++/sizeof(Gfx)==16),
         * opcode = command byte 0 (0xBF G_TRI1 / 0xB1 G_TRI4). */
        {
            const u8 *p   = (const u8 *)light_fixture_table[i].ptr_start_pertinent_DL;
            const u8 *end = (const u8 *)light_fixture_table[i].ptr_end_pertinent_DL;
            for (; p < end; p += 8)
            {
                if (p[0] == 0xBF /* G_TRI1 */)
                {
                    exec = 0;
                    extract_vertex_coords_from_triangle((Gfx *)p, 0U, light_fixture_table[i].room_index, &coord4, &coord5, &coord6);
                    if (sub_GAME_7F0BBCCC(&coord4, light_fixture_table[i].room_index) != 0)      { exec = 1; }
                    else if (sub_GAME_7F0BBCCC(&coord5, light_fixture_table[i].room_index) != 0) { exec = 1; }
                    else if (sub_GAME_7F0BBCCC(&coord6, light_fixture_table[i].room_index) != 0) { exec = 1; }
                    if (exec != 0) { darken_triangle_in_room((Gfx *)p, 0U, light_fixture_table[i].room_index); }
                }
                else if (p[0] == 0xB1 /* G_TRI4 */)
                {
                    for (j = 0; j < 4; j++)
                    {
                        exec2 = 0;
                        extract_vertex_coords_from_triangle((Gfx *)p, j + 1, light_fixture_table[i].room_index, &coord4, &coord5, &coord6);
                        if (sub_GAME_7F0BBCCC(&coord4, light_fixture_table[i].room_index) != 0)      { exec2 = 1; }
                        else if (sub_GAME_7F0BBCCC(&coord5, light_fixture_table[i].room_index) != 0) { exec2 = 1; }
                        else if (sub_GAME_7F0BBCCC(&coord6, light_fixture_table[i].room_index) != 0) { exec2 = 1; }
                        if (exec2 != 0) { darken_triangle_in_room((Gfx *)p, j + 1, light_fixture_table[i].room_index); }
                    }
                }
            }
        }
        return;
    }
}
#else
void sub_GAME_7F0BBE0C(Gfx * gfx, u32 tri_type, s32 room_index)
{
    s16 diff_z_12;
    coord16 coord1;
    coord16 coord2;
    coord16 coord3;
    coord16 coord4;
    coord16 coord5;
    coord16 coord6;
    s16 diff_x_13;
    s16 diff_x_12;
    Gfx *gfx2;
    f32 dist_tween;
    s32 j;
    s8 exec;
    s8 exec2;
    s16 diff_y_13;
    s16 diff_y_12;
    s16 diff_x_23;
    s16 diff_z_13;
    f32 inv_dist_12;
    f32 inv_dist_23;
    f32 inv_dist_13;
    coord3d origin;
    coord3d calc_coord;
    s32 i;
    s16 diff_z_23;
    f32 dist_nn;
    s16 diff_y_23;

    for (i = 0; i < LIGHTFIXTURE_TABLE_MAX; i++)
    {
        if (room_index != light_fixture_table[i].room_index) { continue; }

        if (gfx < light_fixture_table[i].ptr_start_pertinent_DL) { continue; }
        if (gfx >= light_fixture_table[i].ptr_end_pertinent_DL) { continue; }

        if (darkened_light_table_contains_triangle(gfx, tri_type, light_fixture_table[i].room_index) != 0) { return; }

        darken_triangle_in_room(gfx, tri_type, light_fixture_table[i].room_index);
        extract_vertex_coords_from_triangle(gfx, tri_type, light_fixture_table[i].room_index, &coord1, &coord2, &coord3);

		diff_x_12 = coord1.AsArray[0] - coord2.AsArray[0];
		diff_x_23 = coord1.AsArray[0] - coord3.AsArray[0];
		diff_x_13 = coord2.AsArray[0] - coord3.AsArray[0];

		diff_y_12 = coord1.AsArray[1] - coord2.AsArray[1];
		diff_y_23 = coord1.AsArray[1] - coord3.AsArray[1];
		diff_y_13 = coord2.AsArray[1] - coord3.AsArray[1];

		diff_z_12 = coord1.AsArray[2] - coord2.AsArray[2];
		diff_z_23 = coord1.AsArray[2] - coord3.AsArray[2];
		diff_z_13 = coord2.AsArray[2] - coord3.AsArray[2];

        dist_nn = sqrtf((diff_x_12 * diff_x_12) + (diff_y_12 * diff_y_12) + (diff_z_12 * diff_z_12));
        inv_dist_12 = 10.0f / (get_room_data_float2() * dist_nn);

        dist_nn = sqrtf((diff_x_23 * diff_x_23) + (diff_y_23 * diff_y_23) + (diff_z_23 * diff_z_23));
        inv_dist_23 = 10.0f / (get_room_data_float2() * dist_nn);

        dist_nn = sqrtf((diff_x_13 * diff_x_13) + (diff_y_13 * diff_y_13) + (diff_z_13 * diff_z_13));
        inv_dist_13 = 10.0f / (get_room_data_float2() * dist_nn);

        getRoomPositionScaledByIndex(light_fixture_table[i].room_index, &origin);

        for (dist_tween = 0.0f; dist_tween < 1.0f; dist_tween += inv_dist_12)
        {
            calc_coord.x = ((coord2.AsArray[0] + (diff_x_12 * dist_tween)) * get_room_data_float2()) + origin.f[0];
            calc_coord.y = ((coord2.AsArray[1] + (diff_y_12 * dist_tween)) * get_room_data_float2()) + origin.f[1];
            calc_coord.z = ((coord2.AsArray[2] + (diff_z_12 * dist_tween)) * get_room_data_float2()) + origin.f[2];
            sub_GAME_7F0A2160(&calc_coord, 0.0f, 10.0f);
        }

        for (dist_tween = 0.0f; dist_tween < 1.0f; dist_tween += inv_dist_23)
        {
            calc_coord.x = ((coord3.AsArray[0] + (diff_x_23 * dist_tween)) * get_room_data_float2()) + origin.f[0];
            calc_coord.y = ((coord3.AsArray[1] + (diff_y_23 * dist_tween)) * get_room_data_float2()) + origin.f[1];
            calc_coord.z = ((coord3.AsArray[2] + (diff_z_23 * dist_tween)) * get_room_data_float2()) + origin.f[2];
            sub_GAME_7F0A2160(&calc_coord, 0.0f, 10.0f);
        }

        for (dist_tween = 0.0f; dist_tween < 1.0f; dist_tween += inv_dist_13)
        {
            calc_coord.x = ((coord3.AsArray[0] + (diff_x_13 * dist_tween)) * get_room_data_float2()) + origin.f[0];
            calc_coord.y = ((coord3.AsArray[1] + (diff_y_13 * dist_tween)) * get_room_data_float2()) + origin.f[1];
            calc_coord.z = ((coord3.AsArray[2] + (diff_z_13 * dist_tween)) * get_room_data_float2()) + origin.f[2];
            sub_GAME_7F0A2160(&calc_coord, 0.0f, 10.0f);
        }

        for (gfx2 = light_fixture_table[i].ptr_start_pertinent_DL; gfx2 < light_fixture_table[i].ptr_end_pertinent_DL; gfx2++)
        {
            if (gfx2->dma.cmd == G_TRI1)
            {
                exec = 0;

                extract_vertex_coords_from_triangle(gfx2, 0U, light_fixture_table[i].room_index, &coord4, &coord5, &coord6);

                if (sub_GAME_7F0BBCCC(&coord4, light_fixture_table[i].room_index) != 0)
                {
                    exec = 1;
                }
                else if (sub_GAME_7F0BBCCC(&coord5, light_fixture_table[i].room_index) != 0)
                {
                    exec = 1;
                }
                else if (sub_GAME_7F0BBCCC(&coord6, light_fixture_table[i].room_index) != 0)
                {
                    exec = 1;
                }

                if (exec != 0)
                {
                    darken_triangle_in_room(gfx2, 0U, light_fixture_table[i].room_index);
                }
            }
            else if (gfx2->dma.cmd == -0x4f /* G_TRI2 ? */)
            {
                for (j = 0; j < 4; j++)
                {
                    exec2 = 0;

                    extract_vertex_coords_from_triangle(gfx2, j + 1, light_fixture_table[i].room_index, &coord4, &coord5, &coord6);

                    if (sub_GAME_7F0BBCCC(&coord4, light_fixture_table[i].room_index) != 0)
                    {
                        exec2 = 1;
                    }
                    else if (sub_GAME_7F0BBCCC(&coord5, light_fixture_table[i].room_index) != 0)
                    {
                        exec2 = 1;
                    }
                    else if (sub_GAME_7F0BBCCC(&coord6, light_fixture_table[i].room_index) != 0)
                    {
                        exec2 = 1;
                    }

                    if (exec2 != 0)
                    {
                        darken_triangle_in_room(gfx2, j + 1, light_fixture_table[i].room_index);
                    }
                }
            }
        }
        return;
    }
}
#endif /* !NATIVE_PORT */


void clear_light_fixturetable_in_room(s32 room_index)
{
    s32 i;
    for (i = 0; i < LIGHTFIXTURE_TABLE_MAX; i++)
    {
        if (room_index == light_fixture_table[i].room_index)
        {
            light_fixture_table[i].room_index = 0;
        }
    }
    index_of_cur_entry_lightfixture_table = room_index;
}

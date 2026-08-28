/**
 * port_trace.c — Deterministic state tracing and audio dump for regression testing.
 *
 * When enabled, writes per-frame JSONL records capturing player state,
 * rendering metrics, and audio output. Diffs between runs catch regressions
 * that are invisible in screenshots (spawn Y, facing, collision, audio).
 *
 * Activation:
 *   --trace-state path.jsonl   Per-frame state trace
 *   GE007_AUDIO_DUMP=path.raw  Audio PCM dump (default first 300 frames)
 *   GE007_MUSIC_AUDIO_DUMP=path.raw  Pre-SFX music PCM dump (default first 300 frames)
 *
 * State trace uses logical IDs (room index, pad index) not host pointers,
 * so traces are comparable across builds and address spaces.
 */
#ifdef NATIVE_PORT

#include <ultra64.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <bondconstants.h>
#include <boss.h>
#include "game/bondview.h"
#include "game/bondinv.h"
#include "game/file.h"
#include "game/file2.h"
#include "game/gun.h"
#include "game/player.h"
#include "game/unk_0A1DA0.h"
#include "game/stan.h"
#include "snd.h"
#include "game/front.h"
#include "game/title.h"
#include "fr.h"
#include "game/bg.h"
#include "game/chrai.h"
#include "game/chr.h"
#include "game/chrobjhandler.h"
#include "game/chrlv.h"
#include "game/debugmenu_handler.h"
#include "game/explosions.h"
#include "game/initanitable.h"
#include "game/lvl.h"
#include "game/loadobjectmodel.h"
#include "game/matrixmath.h"
#include "game/mp_music.h"
#include "game/objecthandler.h"
#include "game/objective_status.h"
#include "game/ramromreplay.h"
#include "random.h"

/* ===== Externs ===== */
extern int g_deterministic;
extern const char *g_traceStatePath;
extern s32 mission_brief_index;
extern u64 g_randomSeed;

/* MP state (from lvl.c) */
extern s32 D_80048394;  /* MP elapsed time in ticks */
extern s32 g_MainStageNum;
extern s32 g_StageNum;

/* Crash recovery count (from main_pc.c) */
extern int g_crashRecoveryCount;
extern s32 g_dyn_overflow_count; /* dyn.c: per-frame fail-closed overflow counter */
extern s32 g_hud_image_fault_count; /* gun.c: cumulative missing/invalid HUD image counter (M2.6) */

/* Per-frame room draw count (from bg.c) */
extern s32 g_BgNumberOfRoomsDrawn;
extern s_bound_info dword_CODE_bss_8007FFA0[];

/* Current (0-based) player number, for the per-player "p" trace field. */
extern s32 get_cur_playernum(void);

#ifdef NATIVE_PORT
extern s32 port_save_copy_count;
extern s32 port_save_copy_source;
extern s32 port_save_copy_target;
extern s32 port_save_copy_result;
extern s32 port_save_delete_count;
extern s32 port_save_delete_folder;
extern s32 port_save_delete_result;
#endif

/* Render stats snapshot (from gfx_pc.c) */
extern void gfx_get_frame_stats(int *out_tris, int *out_frame,
                                int *out_fog_r, int *out_fog_g, int *out_fog_b,
                                int *out_fog_mul, int *out_fog_off,
                                unsigned int *out_geometry_mode,
                                int *out_bad_cmds);
extern void gfx_get_frame_resolve_stats(int *out_mtx_fail,
                                        int *out_vtx_fail,
                                        int *out_dl_fail,
                                        int *out_movemem_fail,
                                        int *out_texture_fail,
                                        int *out_settimg_fail,
                                        int *out_dl_non_dl_skip_pc,
                                        int *out_dl_non_dl_skip_n64,
                                        int *out_dl_unregistered_skip);
extern unsigned int gfx_get_segment_mask(void);
extern int g_frame_count_diag;
extern int g_portRoomRenderFallbackFrame;
extern int g_portRoomRenderFallbackRooms;
extern int g_portRoomRenderFallbackTotal;
extern f32 get_room_data_float1(void);

/* Intro-state globals (from bondview.h) */
extern enum CAMERAMODE g_CameraMode;
extern enum CAMERAMODE g_CameraAfterCinema;
extern s32 intro_camera_index;
extern s32 g_bondviewForceDisarm;
extern u8 gunbarrel_mode;
extern s32 intro_eye_counter;
extern u32 intro_state_blood_animation;

/* ===== State Trace ===== */

static int s_traceFd = -1;
static int s_traceFrame = 0;
static int s_traceFileOpened = 0;  /* set to 1 only when we actually opened a file */
static int s_prevMenu = MENU_INVALID;
static int s_assertFileSelectSeen = 0;
static int s_assertStageMenuSelected = 0;
static int s_assertMissionStartAuthentic = 0;
static int s_assertPostMissionTransition = 0;
static int s_bondIntroRenderCount = 0;
static int s_bondIntroLastTraceFrame = -1;
static int s_bondIntroLastChrnum = -1;
static int s_bondIntroLastPass = -1;

static u64 traceIntroHashU32(u64 hash, u32 value) {
    hash ^= (u64)value;
    hash *= 1099511628211ULL;
    return hash;
}

static u32 traceReadU32Bytes(const void *base, size_t offset) {
    u32 value = 0;

    if (base != NULL) {
        memcpy(&value, ((const u8 *)base) + offset, sizeof(value));
    }

    return value;
}

static void traceBuildGlassStateJson(char *out, size_t out_size) {
    int active = 0;
    int first_index = -1;
    const s_shattered_window_piece *first = NULL;
    u64 hash = 1469598103934665603ULL;
    char sample_json[2048];
    size_t sample_used = 0;
    int sample_count = 0;

    if (out_size == 0) {
        return;
    }
    sample_json[0] = '[';
    sample_json[1] = '\0';
    sample_used = 1;

    if (ptr_shattered_window_pieces == NULL ||
        SHATTERED_WINDOW_PIECES_BUFFER_LEN <= 0 ||
        SHATTERED_WINDOW_PIECES_BUFFER_LEN > 512) {
        snprintf(out, out_size,
                 "{\"present\":0,\"buffer_len\":%d,\"next\":%d,\"active\":0,"
                 "\"first\":{\"index\":-1},\"sample\":[],\"hash\":\"0x%016llX\"}",
                 (int)SHATTERED_WINDOW_PIECES_BUFFER_LEN,
                 (int)g_NextShardNum,
                 (unsigned long long)hash);
        return;
    }

    for (int i = 0; i < SHATTERED_WINDOW_PIECES_BUFFER_LEN; i++) {
        const s_shattered_window_piece *piece = &ptr_shattered_window_pieces[i];

        if (piece->piece <= 0) {
            continue;
        }

        if (first == NULL) {
            first = piece;
            first_index = i;
        }
        active++;
        if (sample_count < 4 && sample_used < sizeof(sample_json)) {
            int written = snprintf(sample_json + sample_used,
                                   sizeof(sample_json) - sample_used,
                                   "%s{\"index\":%d,\"piece\":%d,"
                                   "\"pos\":[%.2f,%.2f,%.2f],"
                                   "\"rot\":[%.2f,%.2f,%.2f],"
                                   "\"vel\":[%.2f,%.2f,%.2f],"
                                   "\"angvel\":[%.2f,%.2f,%.2f],"
                                   "\"v0\":[%d,%d,%d],\"v1\":[%d,%d,%d],"
                                   "\"v2\":[%d,%d,%d]}",
                                   sample_count == 0 ? "" : ",",
                                   i,
                                   (int)piece->piece,
                                   piece->x,
                                   piece->y,
                                   piece->z,
                                   piece->field_0x10,
                                   piece->field_0x14,
                                   piece->field_0x18,
                                   piece->field_0x1c,
                                   piece->field_0x20,
                                   piece->field_0x24,
                                   piece->field_0x28,
                                   piece->field_0x2c,
                                   piece->field_0x30,
                                   piece->field_0x38,
                                   piece->field_0x3a,
                                   piece->field_0x3c,
                                   piece->field_0x48,
                                   piece->field_0x4a,
                                   piece->field_0x4c,
                                   piece->field_0x58,
                                   piece->field_0x5a,
                                   piece->field_0x5c);
            if (written > 0) {
                size_t remaining = sizeof(sample_json) - sample_used;
                sample_used += (size_t)written < remaining ? (size_t)written : remaining - 1;
            }
            sample_count++;
        }
        for (size_t offset = 0; offset < sizeof(*piece); offset += sizeof(u32)) {
            hash = traceIntroHashU32(hash, traceReadU32Bytes(piece, offset));
        }
    }
    if (sample_used < sizeof(sample_json) - 1) {
        sample_json[sample_used++] = ']';
        sample_json[sample_used] = '\0';
    } else {
        sample_json[sizeof(sample_json) - 2] = ']';
        sample_json[sizeof(sample_json) - 1] = '\0';
    }

    if (first != NULL) {
        snprintf(out, out_size,
                 "{\"present\":1,\"buffer_len\":%d,\"next\":%d,\"active\":%d,"
                 "\"first\":{\"index\":%d,\"piece\":%d,\"timer\":%d,"
                 "\"pos\":[%.2f,%.2f,%.2f],"
                 "\"age\":%.2f,\"rot_y\":%.2f,\"rot\":%.2f,\"rot_z\":%.2f},"
                 "\"sample\":%s,"
                 "\"hash\":\"0x%016llX\"}",
                 (int)SHATTERED_WINDOW_PIECES_BUFFER_LEN,
                 (int)g_NextShardNum,
                 active,
                 first_index,
                 (int)first->piece,
                 (int)first->piece,
                 first->x,
                 first->y,
                 first->z,
                 first->field_0x14,
                 first->field_0x14,
                 first->field_0x18,
                 first->field_0x18,
                 sample_json,
                 (unsigned long long)hash);
    } else {
        snprintf(out, out_size,
                 "{\"present\":1,\"buffer_len\":%d,\"next\":%d,\"active\":0,"
                 "\"first\":{\"index\":-1},\"sample\":[],\"hash\":\"0x%016llX\"}",
                 (int)SHATTERED_WINDOW_PIECES_BUFFER_LEN,
                 (int)g_NextShardNum,
                 (unsigned long long)hash);
    }
}

static int traceGlassShardEmitEnabled(void) {
    const char *value = getenv("GE007_GLASS_SHARDS");

    return !(value != NULL && value[0] == '0');
}

/* FID-0003: the shard scale is no longer a six-way A/B. It is coupled to the
 * field_10E0 (proj*view) visibility scaling: when field_10E0 carries the level
 * visibility (GE007_FIELD_10E0_SCALED, default on) the emitter applies the
 * inverse-visibility compensation; when field_10E0 is unscaled the shard is
 * drawn unscaled (== retail ASM). This mirror keeps the trace's reported
 * scale_mode truthful. The per-candidate summaries below are retained purely as
 * offline diagnostics for the pixel oracle. */
static int traceGlassShardField10E0Scaled(void) {
    const char *value = getenv("GE007_FIELD_10E0_SCALED");

    return !(value != NULL && value[0] == '0');
}

static float traceMtxFixedElement(const Mtx *matrix, int row, int col) {
    const u32 *words = (const u32 *)matrix;
    u32 pair;
    u32 shift;
    u32 hi_word;
    u32 lo_word;
    s16 hi;
    u16 lo;
    s32 fixed;

    if (matrix == NULL || row < 0 || row >= 4 || col < 0 || col >= 4) {
        return 0.0f;
    }

    pair = (u32)row * 2u + (u32)col / 2u;
    shift = (col & 1) ? 0u : 16u;
    hi_word = words[pair];
    lo_word = words[pair + 8u];
    hi = (s16)((hi_word >> shift) & 0xffffu);
    lo = (u16)((lo_word >> shift) & 0xffffu);
    fixed = ((s32)hi << 16) | (s32)lo;

    return (float)fixed / 65536.0f;
}

static void traceLoadCurrentField10E0(float out[4][4], int *valid, int *is_float) {
    void *matrix = NULL;

    *valid = 0;
    *is_float = 0;
    memset(out, 0, sizeof(float) * 16);

    if (g_CurrentPlayer == NULL || g_CurrentPlayer->field_10E0 == 0) {
        return;
    }

    matrix = (void *)(uintptr_t)g_CurrentPlayer->field_10E0;
    if (bondviewField10E0IsFloat()) {
        const Mtxf *src = (const Mtxf *)matrix;
        *is_float = 1;
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                out[row][col] = src->m[row][col];
            }
        }
    } else {
        const Mtx *src = (const Mtx *)matrix;
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                out[row][col] = traceMtxFixedElement(src, row, col);
            }
        }
    }

    *valid = 1;
}

static void traceTransform4x4(const float matrix[4][4],
                              float x,
                              float y,
                              float z,
                              float w,
                              float out[4]) {
    out[0] = matrix[0][0] * x + matrix[1][0] * y + matrix[2][0] * z + matrix[3][0] * w;
    out[1] = matrix[0][1] * x + matrix[1][1] * y + matrix[2][1] * z + matrix[3][1] * w;
    out[2] = matrix[0][2] * x + matrix[1][2] * y + matrix[2][2] * z + matrix[3][2] * w;
    out[3] = matrix[0][3] * x + matrix[1][3] * y + matrix[2][3] * z + matrix[3][3] * w;
}

static void traceTransformMtxf(const Mtxf *matrix,
                               float x,
                               float y,
                               float z,
                               float out[3]) {
    out[0] = matrix->m[0][0] * x + matrix->m[1][0] * y + matrix->m[2][0] * z + matrix->m[3][0];
    out[1] = matrix->m[0][1] * x + matrix->m[1][1] * y + matrix->m[2][1] * z + matrix->m[3][1];
    out[2] = matrix->m[0][2] * x + matrix->m[1][2] * y + matrix->m[2][2] * z + matrix->m[3][2];
}

enum {
    TRACE_GLASS_PROJECTION_SAMPLE_DEFAULT_MAX = 16,
    TRACE_GLASS_PROJECTION_SAMPLE_ALL_MAX = 512,
    TRACE_GLASS_SCALE_BASIS = 0,
    TRACE_GLASS_SCALE_SQRT_BASIS = 1,
    TRACE_GLASS_SCALE_NO_BASIS = 2,
    TRACE_GLASS_SCALE_FULL_MATRIX = 3,
    TRACE_GLASS_SCALE_INV_VIS_FULL = 4,
    TRACE_GLASS_SCALE_COUNT = 5,
};

typedef struct TraceGlassProjectionSummary {
    int active;
    int projected;
    int onscreen;
    int behind;
    int sample_count;
    int include_sample;
    int sample_limit;
    int sample_truncated;
    float union_min_x;
    float union_min_y;
    float union_max_x;
    float union_max_y;
    float max_area;
    int max_index;
    int max_timer;
    int max_onscreen;
    int max_behind;
    float max_world[3];
    float max_model[3][3];
    float max_screen_bbox[4];
    float max_clip_w[3];
    float max_screen[3][2];
    float max_local_bbox[4];
    float max_local_area;
    char sample_json[49152];
    size_t sample_used;
} TraceGlassProjectionSummary;

static const char *traceGlassProjectionScaleModeName(int mode) {
    switch (mode) {
        case TRACE_GLASS_SCALE_BASIS: return "basis";
        case TRACE_GLASS_SCALE_SQRT_BASIS: return "sqrt_basis";
        case TRACE_GLASS_SCALE_NO_BASIS: return "no_basis";
        case TRACE_GLASS_SCALE_FULL_MATRIX: return "full_matrix";
        case TRACE_GLASS_SCALE_INV_VIS_FULL: return "inv_vis_full";
        default: return "unknown";
    }
}

static void traceGlassProjectionSummaryInit(TraceGlassProjectionSummary *summary,
                                            int include_sample,
                                            int sample_limit) {
    memset(summary, 0, sizeof(*summary));
    summary->include_sample = include_sample;
    summary->sample_limit = sample_limit;
    summary->union_min_x = FLT_MAX;
    summary->union_min_y = FLT_MAX;
    summary->union_max_x = -FLT_MAX;
    summary->union_max_y = -FLT_MAX;
    summary->max_index = -1;
    summary->max_timer = 0;
    summary->sample_json[0] = '[';
    summary->sample_json[1] = '\0';
    summary->sample_used = 1;
}

static void traceGlassProjectionSummaryFinish(TraceGlassProjectionSummary *summary) {
    if (summary->onscreen == 0) {
        summary->union_min_x = 0.0f;
        summary->union_min_y = 0.0f;
        summary->union_max_x = 0.0f;
        summary->union_max_y = 0.0f;
    }
    if (summary->sample_used < sizeof(summary->sample_json) - 1u) {
        summary->sample_json[summary->sample_used++] = ']';
        summary->sample_json[summary->sample_used] = '\0';
    } else {
        summary->sample_json[sizeof(summary->sample_json) - 2u] = ']';
        summary->sample_json[sizeof(summary->sample_json) - 1u] = '\0';
    }
}

static void traceApplyGlassProjectionScaleMode(Mtxf *model,
                                               int mode,
                                               float room_scale,
                                               float vis_scale) {
    if (mode == TRACE_GLASS_SCALE_FULL_MATRIX) {
        matrix_scalar_multiply_3(room_scale, &model->m[0][0]);
    } else if (mode == TRACE_GLASS_SCALE_BASIS) {
        matrix_scalar_multiply_2(room_scale, &model->m[0][0]);
    } else if (mode == TRACE_GLASS_SCALE_SQRT_BASIS && room_scale > 0.0f) {
        matrix_scalar_multiply_2(sqrtf(room_scale), &model->m[0][0]);
    } else if (mode == TRACE_GLASS_SCALE_INV_VIS_FULL && vis_scale > 0.0f) {
        matrix_scalar_multiply_3(1.0f / vis_scale, &model->m[0][0]);
    }
}

static int traceGlassProjectionAllSamples(void) {
    static int enabled = -1;

    if (enabled < 0) {
        const char *value = getenv("GE007_TRACE_GLASS_PROJECTION_ALL");
        enabled = (value != NULL && value[0] != '\0' && value[0] != '0') ? 1 : 0;
    }

    return enabled;
}

static void traceGlassProjectionSummaryAddPiece(TraceGlassProjectionSummary *summary,
                                                const float projection[4][4],
                                                float viewport_left,
                                                float viewport_top,
                                                float viewport_width,
                                                float viewport_height,
                                                const Mtxf *model,
                                                const s16 vertices[3][3],
                                                const s_shattered_window_piece *piece,
                                                int index) {
    float sx[3] = {0.0f, 0.0f, 0.0f};
    float sy[3] = {0.0f, 0.0f, 0.0f};
    float sw[3] = {0.0f, 0.0f, 0.0f};
    float model_points[3][3] = {{0.0f}};
    float min_x = FLT_MAX;
    float min_y = FLT_MAX;
    float max_x = -FLT_MAX;
    float max_y = -FLT_MAX;
    float local_min_x = FLT_MAX;
    float local_min_y = FLT_MAX;
    float local_max_x = -FLT_MAX;
    float local_max_y = -FLT_MAX;
    int valid = 1;
    int piece_behind = 0;
    int piece_onscreen = 0;
    float area;
    float local_area;

    summary->active++;

    for (int vi = 0; vi < 3; vi++) {
        float vx = (float)vertices[vi][0];
        float vy = (float)vertices[vi][1];
        float model_pos[3];
        float clip[4];
        float ndc_x;
        float ndc_y;

        if (vx < local_min_x) local_min_x = vx;
        if (vy < local_min_y) local_min_y = vy;
        if (vx > local_max_x) local_max_x = vx;
        if (vy > local_max_y) local_max_y = vy;

        traceTransformMtxf(model,
                           vx,
                           vy,
                           (float)vertices[vi][2],
                           model_pos);
        model_points[vi][0] = model_pos[0];
        model_points[vi][1] = model_pos[1];
        model_points[vi][2] = model_pos[2];
        traceTransform4x4(projection,
                          model_pos[0],
                          model_pos[1],
                          model_pos[2],
                          1.0f,
                          clip);
        sw[vi] = clip[3];
        if (!__builtin_isfinite(clip[0]) || !__builtin_isfinite(clip[1]) ||
            !__builtin_isfinite(clip[3]) || fabsf(clip[3]) < 0.000001f) {
            valid = 0;
            continue;
        }
        if (clip[3] <= 0.0f) {
            piece_behind = 1;
        }
        ndc_x = clip[0] / clip[3];
        ndc_y = clip[1] / clip[3];
        sx[vi] = viewport_left + (ndc_x * 0.5f + 0.5f) * viewport_width;
        sy[vi] = viewport_top + (0.5f - ndc_y * 0.5f) * viewport_height;
        if (!__builtin_isfinite(sx[vi]) || !__builtin_isfinite(sy[vi])) {
            valid = 0;
            continue;
        }
        if (sx[vi] < min_x) min_x = sx[vi];
        if (sy[vi] < min_y) min_y = sy[vi];
        if (sx[vi] > max_x) max_x = sx[vi];
        if (sy[vi] > max_y) max_y = sy[vi];
    }

    if (!valid || min_x == FLT_MAX || min_y == FLT_MAX || max_x == -FLT_MAX || max_y == -FLT_MAX) {
        return;
    }

    summary->projected++;
    if (piece_behind) {
        summary->behind++;
    }
    area = (max_x - min_x) * (max_y - min_y);
    if (area < 0.0f || !__builtin_isfinite(area)) {
        area = 0.0f;
    }

    local_area = (local_max_x - local_min_x) * (local_max_y - local_min_y);
    if (local_area < 0.0f || !__builtin_isfinite(local_area)) {
        local_area = 0.0f;
    }

    piece_onscreen =
        max_x >= viewport_left &&
        max_y >= viewport_top &&
        min_x <= viewport_left + viewport_width &&
        min_y <= viewport_top + viewport_height;
    if (piece_onscreen) {
        summary->onscreen++;
        if (min_x < summary->union_min_x) summary->union_min_x = min_x;
        if (min_y < summary->union_min_y) summary->union_min_y = min_y;
        if (max_x > summary->union_max_x) summary->union_max_x = max_x;
        if (max_y > summary->union_max_y) summary->union_max_y = max_y;
    }

    if (area > summary->max_area) {
        summary->max_area = area;
        summary->max_index = index;
        summary->max_timer = (int)piece->piece;
        summary->max_onscreen = piece_onscreen;
        summary->max_behind = piece_behind;
        summary->max_world[0] = piece->x;
        summary->max_world[1] = piece->y;
        summary->max_world[2] = piece->z;
        for (int vi = 0; vi < 3; vi++) {
            summary->max_model[vi][0] = model_points[vi][0];
            summary->max_model[vi][1] = model_points[vi][1];
            summary->max_model[vi][2] = model_points[vi][2];
        }
        summary->max_screen_bbox[0] = min_x;
        summary->max_screen_bbox[1] = min_y;
        summary->max_screen_bbox[2] = max_x;
        summary->max_screen_bbox[3] = max_y;
        summary->max_clip_w[0] = sw[0];
        summary->max_clip_w[1] = sw[1];
        summary->max_clip_w[2] = sw[2];
        summary->max_screen[0][0] = sx[0];
        summary->max_screen[0][1] = sy[0];
        summary->max_screen[1][0] = sx[1];
        summary->max_screen[1][1] = sy[1];
        summary->max_screen[2][0] = sx[2];
        summary->max_screen[2][1] = sy[2];
        summary->max_local_bbox[0] = local_min_x;
        summary->max_local_bbox[1] = local_min_y;
        summary->max_local_bbox[2] = local_max_x;
        summary->max_local_bbox[3] = local_max_y;
        summary->max_local_area = local_area;
    }

    if (summary->include_sample &&
        summary->sample_count < summary->sample_limit &&
        summary->sample_used < sizeof(summary->sample_json)) {
        char piece_json[1024];
        int written = snprintf(
            piece_json,
            sizeof(piece_json),
            "%s{\"index\":%d,\"timer\":%d,\"onscreen\":%d,\"behind\":%d,"
            "\"world\":[%.2f,%.2f,%.2f],"
            "\"model\":[[%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f]],"
            "\"local_bbox\":[%.2f,%.2f,%.2f,%.2f],\"local_area\":%.2f,"
            "\"screen_bbox\":[%.2f,%.2f,%.2f,%.2f],"
            "\"screen_area\":%.2f,\"clip_w\":[%.4f,%.4f,%.4f],"
            "\"screen\":[[%.2f,%.2f],[%.2f,%.2f],[%.2f,%.2f]]}",
            summary->sample_count == 0 ? "" : ",",
            index,
            (int)piece->piece,
            piece_onscreen,
            piece_behind,
            piece->x,
            piece->y,
            piece->z,
            model_points[0][0],
            model_points[0][1],
            model_points[0][2],
            model_points[1][0],
            model_points[1][1],
            model_points[1][2],
            model_points[2][0],
            model_points[2][1],
            model_points[2][2],
            local_min_x,
            local_min_y,
            local_max_x,
            local_max_y,
            local_area,
            min_x,
            min_y,
            max_x,
            max_y,
            area,
            sw[0],
            sw[1],
            sw[2],
            sx[0],
            sy[0],
            sx[1],
            sy[1],
            sx[2],
            sy[2]);
        if (written > 0 &&
            (size_t)written < sizeof(piece_json) &&
            summary->sample_used + (size_t)written < sizeof(summary->sample_json) - 1u) {
            memcpy(summary->sample_json + summary->sample_used, piece_json, (size_t)written);
            summary->sample_used += (size_t)written;
            summary->sample_json[summary->sample_used] = '\0';
            summary->sample_count++;
        } else {
            summary->sample_truncated = 1;
        }
    }
}

static void traceGlassProjectionMaxPieceJson(char *out,
                                             size_t out_size,
                                             const TraceGlassProjectionSummary *summary) {
    snprintf(out, out_size,
             "{\"index\":%d,\"timer\":%d,\"onscreen\":%d,\"behind\":%d,"
             "\"world\":[%.2f,%.2f,%.2f],"
             "\"model\":[[%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f]],"
             "\"local_bbox\":[%.2f,%.2f,%.2f,%.2f],\"local_area\":%.2f,"
             "\"screen_bbox\":[%.2f,%.2f,%.2f,%.2f],"
             "\"screen_area\":%.2f,\"clip_w\":[%.4f,%.4f,%.4f],"
             "\"screen\":[[%.2f,%.2f],[%.2f,%.2f],[%.2f,%.2f]]}",
             summary->max_index,
             summary->max_timer,
             summary->max_onscreen,
             summary->max_behind,
             summary->max_world[0],
             summary->max_world[1],
             summary->max_world[2],
             summary->max_model[0][0],
             summary->max_model[0][1],
             summary->max_model[0][2],
             summary->max_model[1][0],
             summary->max_model[1][1],
             summary->max_model[1][2],
             summary->max_model[2][0],
             summary->max_model[2][1],
             summary->max_model[2][2],
             summary->max_local_bbox[0],
             summary->max_local_bbox[1],
             summary->max_local_bbox[2],
             summary->max_local_bbox[3],
             summary->max_local_area,
             summary->max_screen_bbox[0],
             summary->max_screen_bbox[1],
             summary->max_screen_bbox[2],
             summary->max_screen_bbox[3],
             summary->max_area,
             summary->max_clip_w[0],
             summary->max_clip_w[1],
             summary->max_clip_w[2],
             summary->max_screen[0][0],
             summary->max_screen[0][1],
             summary->max_screen[1][0],
             summary->max_screen[1][1],
             summary->max_screen[2][0],
             summary->max_screen[2][1]);
}

static void traceGlassProjectionSummaryJson(char *out,
                                            size_t out_size,
                                            const TraceGlassProjectionSummary *summary,
                                            const char *scale_mode) {
    char max_piece_json[1024];

    traceGlassProjectionMaxPieceJson(max_piece_json, sizeof(max_piece_json), summary);
    snprintf(out, out_size,
             "{\"scale_mode\":\"%s\",\"active\":%d,\"projected\":%d,"
             "\"onscreen\":%d,\"behind\":%d,"
             "\"union_screen_bbox\":[%.2f,%.2f,%.2f,%.2f],"
             "\"max_screen_area\":%.2f,\"max_piece\":%s}",
             scale_mode,
             summary->active,
             summary->projected,
             summary->onscreen,
             summary->behind,
             summary->union_min_x,
             summary->union_min_y,
             summary->union_max_x,
             summary->union_max_y,
             summary->max_area,
             max_piece_json);
}

static void traceBuildGlassProjectionJson(char *out, size_t out_size) {
    float projection[4][4];
    int projection_valid = 0;
    int projection_is_float = 0;
    int emit_enabled = traceGlassShardEmitEnabled();
    int field10e0_scaled = traceGlassShardField10E0Scaled();
    int sample_all = traceGlassProjectionAllSamples();
    int sample_limit = sample_all ? TRACE_GLASS_PROJECTION_SAMPLE_ALL_MAX : TRACE_GLASS_PROJECTION_SAMPLE_DEFAULT_MAX;
    int selected_mode = TRACE_GLASS_SCALE_INV_VIS_FULL;
    float viewport_left = 0.0f;
    float viewport_top = 0.0f;
    float viewport_width = 320.0f;
    float viewport_height = 240.0f;
    float room_scale = 1.0f;
    float vis_scale = 1.0f;
    TraceGlassProjectionSummary summaries[TRACE_GLASS_SCALE_COUNT];
    char selected_max_piece_json[1024];
    char variant_basis_json[2048];
    char variant_sqrt_basis_json[2048];
    char variant_no_basis_json[2048];
    char variant_full_matrix_json[2048];
    char variant_inv_vis_full_json[2048];

    if (out_size == 0) {
        return;
    }

    if (g_CurrentPlayer != NULL) {
        viewport_left = g_CurrentPlayer->c_screenleft;
        viewport_top = g_CurrentPlayer->c_screentop;
        viewport_width = g_CurrentPlayer->c_screenwidth;
        viewport_height = g_CurrentPlayer->c_screenheight;
    }

    room_scale = get_room_data_float1();
    if (!__builtin_isfinite(room_scale) || room_scale == 0.0f) {
        room_scale = 1.0f;
    }
    vis_scale = bgGetLevelVisibilityScale();
    if (!__builtin_isfinite(vis_scale) || vis_scale == 0.0f) {
        vis_scale = 1.0f;
    }

    traceLoadCurrentField10E0(projection, &projection_valid, &projection_is_float);

    /* Coupled to field_10E0: scaled -> inverse-visibility compensation;
     * unscaled -> shard drawn as-is (the retail-ASM path). */
    selected_mode = field10e0_scaled
                        ? TRACE_GLASS_SCALE_INV_VIS_FULL
                        : TRACE_GLASS_SCALE_NO_BASIS;
    for (int mode = 0; mode < TRACE_GLASS_SCALE_COUNT; mode++) {
        traceGlassProjectionSummaryInit(&summaries[mode], mode == selected_mode, sample_limit);
    }

    if (g_CurrentPlayer == NULL ||
        ptr_shattered_window_pieces == NULL ||
        SHATTERED_WINDOW_PIECES_BUFFER_LEN <= 0 ||
        SHATTERED_WINDOW_PIECES_BUFFER_LEN > 512 ||
        !projection_valid) {
        snprintf(out, out_size,
                 "{\"present\":0,\"emit_enabled\":%d,\"projection_valid\":%d,"
                 "\"projection_float\":%d,\"active\":0,\"projected\":0,"
                 "\"onscreen\":0,\"behind\":0,\"sample\":[]}",
                 emit_enabled,
                 projection_valid,
                 projection_is_float);
        return;
    }

    for (int index = 0; index < SHATTERED_WINDOW_PIECES_BUFFER_LEN; index++) {
        const s_shattered_window_piece *piece = &ptr_shattered_window_pieces[index];
        Mtxf base_model;
        const s16 vertices[3][3] = {
            { piece->field_0x38, piece->field_0x3a, piece->field_0x3c },
            { piece->field_0x48, piece->field_0x4a, piece->field_0x4c },
            { piece->field_0x58, piece->field_0x5a, piece->field_0x5c },
        };

        if (piece->piece <= 0) {
            continue;
        }

        matrix_4x4_set_position_and_rotation_around_xyz(
            (coord3d *)&piece->x,
            (coord3d *)&piece->field_0x10,
            &base_model);
        base_model.m[3][0] -= g_CurrentPlayer->current_model_pos.x;
        base_model.m[3][1] -= g_CurrentPlayer->current_model_pos.y;
        base_model.m[3][2] -= g_CurrentPlayer->current_model_pos.z;

        for (int mode = 0; mode < TRACE_GLASS_SCALE_COUNT; mode++) {
            Mtxf model = base_model;
            traceApplyGlassProjectionScaleMode(&model, mode, room_scale, vis_scale);
            traceGlassProjectionSummaryAddPiece(&summaries[mode],
                                                projection,
                                                viewport_left,
                                                viewport_top,
                                                viewport_width,
                                                viewport_height,
                                                &model,
                                                vertices,
                                                piece,
                                                index);
        }
    }

    for (int mode = 0; mode < TRACE_GLASS_SCALE_COUNT; mode++) {
        traceGlassProjectionSummaryFinish(&summaries[mode]);
    }
    traceGlassProjectionSummaryJson(variant_basis_json,
                                    sizeof(variant_basis_json),
                                    &summaries[TRACE_GLASS_SCALE_BASIS],
                                    traceGlassProjectionScaleModeName(TRACE_GLASS_SCALE_BASIS));
    traceGlassProjectionSummaryJson(variant_sqrt_basis_json,
                                    sizeof(variant_sqrt_basis_json),
                                    &summaries[TRACE_GLASS_SCALE_SQRT_BASIS],
                                    traceGlassProjectionScaleModeName(TRACE_GLASS_SCALE_SQRT_BASIS));
    traceGlassProjectionSummaryJson(variant_no_basis_json,
                                    sizeof(variant_no_basis_json),
                                    &summaries[TRACE_GLASS_SCALE_NO_BASIS],
                                    traceGlassProjectionScaleModeName(TRACE_GLASS_SCALE_NO_BASIS));
    traceGlassProjectionSummaryJson(variant_full_matrix_json,
                                    sizeof(variant_full_matrix_json),
                                    &summaries[TRACE_GLASS_SCALE_FULL_MATRIX],
                                    traceGlassProjectionScaleModeName(TRACE_GLASS_SCALE_FULL_MATRIX));
    traceGlassProjectionSummaryJson(variant_inv_vis_full_json,
                                    sizeof(variant_inv_vis_full_json),
                                    &summaries[TRACE_GLASS_SCALE_INV_VIS_FULL],
                                    traceGlassProjectionScaleModeName(TRACE_GLASS_SCALE_INV_VIS_FULL));
    traceGlassProjectionMaxPieceJson(selected_max_piece_json,
                                     sizeof(selected_max_piece_json),
                                     &summaries[selected_mode]);

    snprintf(out, out_size,
             "{\"present\":1,\"source\":\"native_emit_projection\","
             "\"emit_enabled\":%d,\"projection_valid\":%d,\"projection_float\":%d,"
             "\"projection_diag\":[%.6f,%.6f,%.6f,%.6f],"
             "\"projection_row3\":[%.6f,%.6f,%.6f,%.6f],"
             "\"projection_col3\":[%.6f,%.6f,%.6f,%.6f],"
             "\"compress_full_matrix\":%d,\"basis_scale\":%d,\"no_basis_scale\":%d,"
             "\"sqrt_basis\":%d,\"inv_vis_scale\":%d,"
             "\"room_scale\":%.8f,\"vis_scale\":%.8f,"
             "\"scale_mode\":\"%s\","
             "\"viewport\":[%.2f,%.2f,%.2f,%.2f],"
             "\"active\":%d,\"projected\":%d,\"onscreen\":%d,\"behind\":%d,"
             "\"sample_all\":%d,\"sample_limit\":%d,\"sample_count\":%d,\"sample_truncated\":%d,"
             "\"union_screen_bbox\":[%.2f,%.2f,%.2f,%.2f],"
             "\"max_screen_area\":%.2f,"
             "\"max_piece\":%s,"
             "\"variants\":{\"basis\":%s,\"sqrt_basis\":%s,\"no_basis\":%s,\"full_matrix\":%s,\"inv_vis_full\":%s},"
             "\"sample\":%s}",
             emit_enabled,
             projection_valid,
             projection_is_float,
             projection[0][0],
             projection[1][1],
             projection[2][2],
             projection[3][3],
             projection[3][0],
             projection[3][1],
             projection[3][2],
             projection[3][3],
             projection[0][3],
             projection[1][3],
             projection[2][3],
             projection[3][3],
             0,                  /* compress_full_matrix: retired A/B (FID-0003) */
             0,                  /* basis_scale: retired A/B (FID-0003) */
             !field10e0_scaled,  /* no_basis_scale: live when field_10E0 unscaled */
             0,                  /* sqrt_basis: retired A/B (FID-0003) */
             field10e0_scaled,   /* inv_vis_scale: live default (coupled) */
             room_scale,
             vis_scale,
             traceGlassProjectionScaleModeName(selected_mode),
             viewport_left,
             viewport_top,
             viewport_width,
             viewport_height,
             summaries[selected_mode].active,
             summaries[selected_mode].projected,
             summaries[selected_mode].onscreen,
             summaries[selected_mode].behind,
             sample_all,
             sample_limit,
             summaries[selected_mode].sample_count,
             summaries[selected_mode].sample_truncated,
             summaries[selected_mode].union_min_x,
             summaries[selected_mode].union_min_y,
             summaries[selected_mode].union_max_x,
             summaries[selected_mode].union_max_y,
             summaries[selected_mode].max_area,
             selected_max_piece_json,
             variant_basis_json,
             variant_sqrt_basis_json,
             variant_no_basis_json,
             variant_full_matrix_json,
             variant_inv_vis_full_json,
             summaries[selected_mode].sample_json);
}

static int traceObjectShotsTaken(const ObjectRecord *obj) {
    if (obj == NULL) {
        return 0;
    }
    if (!(obj->state & PROPSTATE_DESTROYED)) {
        if (obj->damage == 0.0f) {
            return 0;
        }
        return (int)((obj->maxdamage * 3.0f) / obj->damage);
    }
    return (int)(obj->maxdamage + 4.0f);
}

static int traceObjectDestroyedLevel(const ObjectRecord *obj) {
    if (obj == NULL || !(obj->state & PROPSTATE_DESTROYED)) {
        return 0;
    }
    return ((int)obj->maxdamage >> 2) + 1;
}

static void traceFormatGlassPropObjectJson(char *out,
                                           size_t out_size,
                                           const ObjectRecord *obj,
                                           int setup_index,
                                           float dist_sq) {
    if (out_size == 0) {
        return;
    }
    if (obj == NULL) {
        snprintf(out, out_size, "{\"index\":-1}");
        return;
    }

    snprintf(out,
             out_size,
             "{\"index\":%d,\"type\":%d,\"obj\":%d,\"pad\":%d,"
             "\"state\":%u,\"runtime\":\"0x%08X\",\"remove\":%d,"
             "\"destroyed_level\":%d,\"shots\":%d,\"has_prop\":%d,\"has_model\":%d,"
             "\"damage\":%.4f,\"maxdamage\":%.4f,\"pos\":[%.2f,%.2f,%.2f],"
             "\"dist_sq\":%.2f}",
             setup_index,
             obj->type,
             obj->obj,
             obj->pad,
             (unsigned int)obj->state,
             (unsigned int)obj->runtime_bitflags,
             (obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE) ? 1 : 0,
             traceObjectDestroyedLevel(obj),
             traceObjectShotsTaken(obj),
             obj->prop != NULL ? 1 : 0,
             obj->model != NULL ? 1 : 0,
             obj->damage,
             obj->maxdamage,
             obj->runtime_pos.x,
             obj->runtime_pos.y,
             obj->runtime_pos.z,
             dist_sq);
}

static void traceBuildGlassPropsJson(char *out,
                                     size_t out_size,
                                     int has_player,
                                     float player_x,
                                     float player_y,
                                     float player_z) {
    enum { GLASS_PROP_SAMPLE_MAX = 32 };
    PropDefHeaderRecord *cmd;
    int setup_index = 0;
    int count = 0;
    int live = 0;
    int with_prop = 0;
    int with_model = 0;
    int destroyed = 0;
    int remove = 0;
    int truncated = 0;
    int sample_truncated = 0;
    int sample_count = 0;
    int sample_len = 0;
    int nearest_index = -1;
    int first_removed_index = -1;
    int first_destroyed_index = -1;
    float nearest_dist_sq = FLT_MAX;
    float first_removed_dist_sq = 0.0f;
    float first_destroyed_dist_sq = 0.0f;
    const ObjectRecord *nearest_obj = NULL;
    const ObjectRecord *first_removed_obj = NULL;
    const ObjectRecord *first_destroyed_obj = NULL;
    u64 hash = 1469598103934665603ULL;
    char nearest_json[512];
    char first_removed_json[512];
    char first_destroyed_json[512];
    char sample_json[12288];

    if (out_size == 0) {
        return;
    }

    nearest_json[0] = '\0';
    first_removed_json[0] = '\0';
    first_destroyed_json[0] = '\0';
    sample_json[0] = '[';
    sample_json[1] = '\0';
    sample_len = 1;

    if (g_CurrentSetup.propDefs == NULL) {
        snprintf(out,
                 out_size,
                 "{\"present\":0,\"count\":0,\"live\":0,\"with_prop\":0,\"with_model\":0,"
                 "\"destroyed\":0,\"remove\":0,\"truncated\":0,\"nearest\":{\"index\":-1},"
                 "\"first_removed\":{\"index\":-1},\"first_destroyed\":{\"index\":-1},"
                 "\"sample\":[],\"sample_truncated\":0,\"hash\":\"0x%016llX\"}",
                 (unsigned long long)hash);
        return;
    }

    for (cmd = g_CurrentSetup.propDefs;
         cmd->type != PROPDEF_END && setup_index < 2048;
         cmd += sizepropdef(cmd), setup_index++) {
        const ObjectRecord *obj;
        int obj_destroyed;
        int obj_remove;
        float dx = 0.0f;
        float dy = 0.0f;
        float dz = 0.0f;
        float dist_sq = 0.0f;

        if (cmd->type != PROPDEF_GLASS && cmd->type != PROPDEF_TINTED_GLASS) {
            continue;
        }

        obj = (const ObjectRecord *)cmd;
        obj_destroyed = (obj->state & PROPSTATE_DESTROYED) ? 1 : 0;
        obj_remove = (obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE) ? 1 : 0;

        count++;
        if (obj->prop != NULL) {
            with_prop++;
        }
        if (obj->model != NULL) {
            with_model++;
        }
        if (obj->prop != NULL && !obj_remove) {
            live++;
        }
        if (obj_destroyed) {
            destroyed++;
        }
        if (obj_remove) {
            remove++;
        }

        if (has_player) {
            dx = obj->runtime_pos.x - player_x;
            dy = obj->runtime_pos.y - player_y;
            dz = obj->runtime_pos.z - player_z;
            dist_sq = dx * dx + dy * dy + dz * dz;
        }

        if (nearest_obj == NULL || (has_player && dist_sq < nearest_dist_sq)) {
            nearest_obj = obj;
            nearest_index = setup_index;
            nearest_dist_sq = dist_sq;
        }
        if (obj_remove && first_removed_obj == NULL) {
            first_removed_obj = obj;
            first_removed_index = setup_index;
            first_removed_dist_sq = dist_sq;
        }
        if (obj_destroyed && first_destroyed_obj == NULL) {
            first_destroyed_obj = obj;
            first_destroyed_index = setup_index;
            first_destroyed_dist_sq = dist_sq;
        }
        if (sample_count < GLASS_PROP_SAMPLE_MAX) {
            char object_json[512];
            int written;
            traceFormatGlassPropObjectJson(object_json, sizeof(object_json), obj, setup_index, dist_sq);
            written = snprintf(sample_json + sample_len,
                               sizeof(sample_json) - (size_t)sample_len,
                               "%s%s",
                               sample_count == 0 ? "" : ",",
                               object_json);
            if (written > 0 && written < (int)(sizeof(sample_json) - (size_t)sample_len)) {
                sample_len += written;
                sample_count++;
            } else {
                sample_truncated = 1;
            }
        } else {
            sample_truncated = 1;
        }

        hash = traceIntroHashU32(hash, (u32)setup_index);
        hash = traceIntroHashU32(hash, (u32)obj->type);
        hash = traceIntroHashU32(hash, (u32)obj->obj);
        hash = traceIntroHashU32(hash, (u32)obj->pad);
        hash = traceIntroHashU32(hash, (u32)obj->state);
        hash = traceIntroHashU32(hash, obj->flags);
        hash = traceIntroHashU32(hash, obj->flags2);
        hash = traceIntroHashU32(hash, obj->runtime_bitflags);
        hash = traceIntroHashU32(hash, obj->prop != NULL ? 1U : 0U);
        hash = traceIntroHashU32(hash, obj->model != NULL ? 1U : 0U);
        hash = traceIntroHashU32(hash, traceReadU32Bytes(&obj->runtime_pos.x, 0));
        hash = traceIntroHashU32(hash, traceReadU32Bytes(&obj->runtime_pos.y, 0));
        hash = traceIntroHashU32(hash, traceReadU32Bytes(&obj->runtime_pos.z, 0));
        hash = traceIntroHashU32(hash, traceReadU32Bytes(&obj->maxdamage, 0));
        hash = traceIntroHashU32(hash, traceReadU32Bytes(&obj->damage, 0));
    }

    if (setup_index >= 2048 && cmd->type != PROPDEF_END) {
        truncated = 1;
    }

    traceFormatGlassPropObjectJson(nearest_json,
                                   sizeof(nearest_json),
                                   nearest_obj,
                                   nearest_index,
                                   nearest_obj != NULL ? nearest_dist_sq : 0.0f);
    traceFormatGlassPropObjectJson(first_removed_json,
                                   sizeof(first_removed_json),
                                   first_removed_obj,
                                   first_removed_index,
                                   first_removed_dist_sq);
    traceFormatGlassPropObjectJson(first_destroyed_json,
                                   sizeof(first_destroyed_json),
                                   first_destroyed_obj,
                                   first_destroyed_index,
                                   first_destroyed_dist_sq);
    if (sample_len < (int)sizeof(sample_json) - 1) {
        sample_json[sample_len++] = ']';
        sample_json[sample_len] = '\0';
    } else {
        strcpy(sample_json, "[]");
        sample_truncated = 1;
    }

    snprintf(out,
             out_size,
             "{\"present\":1,\"count\":%d,\"live\":%d,\"with_prop\":%d,\"with_model\":%d,"
             "\"destroyed\":%d,\"remove\":%d,\"truncated\":%d,"
             "\"nearest\":%s,\"first_removed\":%s,\"first_destroyed\":%s,"
             "\"sample\":%s,\"sample_truncated\":%d,\"hash\":\"0x%016llX\"}",
             count,
             live,
             with_prop,
             with_model,
             destroyed,
             remove,
             truncated,
             nearest_json[0] ? nearest_json : "{\"index\":-1}",
             first_removed_json[0] ? first_removed_json : "{\"index\":-1}",
             first_destroyed_json[0] ? first_destroyed_json : "{\"index\":-1}",
             sample_json,
             sample_truncated,
             (unsigned long long)hash);
}

static u16 traceAnimReadU16(const ModelAnimation *anim, size_t offset) {
    u16 value = 0;
    if (anim != NULL) {
        memcpy(&value, ((const u8 *)anim) + offset, sizeof(value));
    }
    return value;
}

static u32 traceAnimReadU32(const ModelAnimation *anim, size_t offset) {
    u32 value = 0;
    if (anim != NULL) {
        memcpy(&value, ((const u8 *)anim) + offset, sizeof(value));
    }
    return value;
}

static int traceModelAnimationTableOffset(const ModelAnimation *anim) {
    uintptr_t base;
    uintptr_t ptr;
    uintptr_t delta;

    if (anim == NULL || ptr_animation_table == NULL) {
        return -1;
    }

    base = (uintptr_t)ptr_animation_table;
    ptr = (uintptr_t)anim;
    if (ptr < base) {
        return -1;
    }

    delta = ptr - base;
    if (delta > 0xffffu) {
        return -1;
    }

    return (int)delta;
}

static u64 traceModelAnimationHeaderHash(const ModelAnimation *anim) {
    u64 hash;

    if (anim == NULL) {
        return 0;
    }

    hash = 1469598103934665603ULL;
    hash = traceIntroHashU32(hash, (u32)traceAnimReadU16(anim, 0x04));
    hash = traceIntroHashU32(hash, (u32)*(((const u8 *)anim) + 0x06));
    hash = traceIntroHashU32(hash, (u32)*(((const u8 *)anim) + 0x07));
    hash = traceIntroHashU32(hash, traceAnimReadU32(anim, 0x08));
    hash = traceIntroHashU32(hash, (u32)traceAnimReadU16(anim, 0x0c));
    hash = traceIntroHashU32(hash, (u32)traceAnimReadU16(anim, 0x0e));
    hash = traceIntroHashU32(hash, traceAnimReadU32(anim, 0x10));
    return hash;
}

void portTraceBondIntroRendered(const ChrRecord *chr, s32 withalpha) {
    if (chr == NULL) {
        return;
    }

    s_bondIntroRenderCount++;
    s_bondIntroLastTraceFrame = s_traceFrame + 1;
    s_bondIntroLastChrnum = chr->chrnum;
    s_bondIntroLastPass = withalpha;
}

static int traceCombatScanEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        enabled = (getenv("GE007_TRACE_COMBAT_SCAN") != NULL) ? 1 : 0;
    }
    return enabled;
}

static int traceInventoryStateEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        enabled = (getenv("GE007_TRACE_INVENTORY") != NULL) ? 1 : 0;
    }
    return enabled;
}

static int traceObjectiveStateEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        enabled = (getenv("GE007_TRACE_OBJECTIVES") != NULL) ? 1 : 0;
    }
    return enabled;
}

static int traceFlowOnlyEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        enabled = (getenv("GE007_TRACE_FLOW_ONLY") != NULL ||
                   getenv("GE007_TRACE_MINIMAL") != NULL) ? 1 : 0;
    }
    return enabled;
}

static int traceFullRoomListsEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *env = getenv("GE007_TRACE_FULL_ROOM_LIST");
        enabled = env ? (atoi(env) != 0) : 0;
    }
    return enabled;
}

/* Frontend/title transitions can leave stage pointers non-null after unload. */
static int traceLiveStageGlobalsSafe(void) {
    int stage_menu_active;

    if (g_StageNum == LEVELID_TITLE || lvlGetCurrentStageToLoad() == LEVELID_TITLE) {
        return 0;
    }

    stage_menu_active =
        current_menu == MENU_INVALID ||
        current_menu == MENU_RUN_STAGE ||
        current_menu == MENU_MISSION_COMPLETE ||
        current_menu == MENU_MISSION_FAILED ||
        (current_menu == MENU_SWITCH_SCREENS &&
         (menu_update == MENU_RUN_STAGE ||
          menu_update == MENU_MISSION_COMPLETE ||
          menu_update == MENU_MISSION_FAILED ||
          maybe_prev_menu == MENU_RUN_STAGE ||
          maybe_prev_menu == MENU_MISSION_COMPLETE ||
          maybe_prev_menu == MENU_MISSION_FAILED));

    return stage_menu_active ? 1 : 0;
}

static const char *traceGuardOraclePath(void) {
    static const char *path = NULL;
    static int initialized = 0;

    if (!initialized) {
        const char *value = getenv("GE007_GUARD_ORACLE_TRACE");

        if (value != NULL) {
            if (value[0] != '\0'
                && strcmp(value, "1") != 0
                && strcmp(value, "true") != 0
                && strcmp(value, "TRUE") != 0) {
                path = value;
            } else {
                path = "guard_oracle_trace.jsonl";
            }
        }

        initialized = 1;
    }

    return path;
}

static int traceGuardOracleEnabled(void) {
    return traceGuardOraclePath() != NULL;
}

static int traceTrackedChrNum(void) {
    static int initialized = 0;
    static int value = -1;

    if (!initialized) {
        const char *env = getenv("GE007_TRACE_CHRNUM");

        if (env != NULL && env[0] != '\0') {
            value = atoi(env);
        }

        initialized = 1;
    }

    return value;
}

static const char *traceStagePadDumpPath(void) {
    static const char *path = NULL;
    static int initialized = 0;
    if (!initialized) {
        const char *value = getenv("GE007_DUMP_STAGE_PADS");
        path = (value != NULL && value[0] != '\0') ? value : NULL;
        initialized = 1;
    }
    return path;
}

static int traceStagePadDumpEnabled(void) {
    return traceStagePadDumpPath() != NULL;
}

static int traceStagePadDumpFrame(void) {
    static int initialized = 0;
    static int value = 1;

    if (!initialized) {
        const char *env_value = getenv("GE007_DUMP_STAGE_PADS_FRAME");
        if (env_value != NULL && env_value[0] != '\0') {
            int parsed = atoi(env_value);
            if (parsed > 0) {
                value = parsed;
            }
        }
        initialized = 1;
    }

    return value;
}

static const char *traceStageChrDumpPath(void) {
    static const char *path = NULL;
    static int initialized = 0;
    if (!initialized) {
        const char *value = getenv("GE007_DUMP_STAGE_CHRS");
        path = (value != NULL && value[0] != '\0') ? value : NULL;
        initialized = 1;
    }
    return path;
}

static int traceStageChrDumpEnabled(void) {
    return traceStageChrDumpPath() != NULL;
}

static int traceStageChrDumpFrame(void) {
    static int initialized = 0;
    static int value = 1;

    if (!initialized) {
        const char *env_value = getenv("GE007_DUMP_STAGE_CHRS_FRAME");
        if (env_value != NULL && env_value[0] != '\0') {
            int parsed = atoi(env_value);
            if (parsed > 0) {
                value = parsed;
            }
        }
        initialized = 1;
    }

    return value;
}

static const char *traceMissionGateDumpPath(void) {
    static const char *path = NULL;
    static int initialized = 0;
    if (!initialized) {
        const char *value = getenv("GE007_DUMP_MISSION_GATES");
        path = (value != NULL && value[0] != '\0') ? value : NULL;
        initialized = 1;
    }
    return path;
}

static FILE *s_missionGateDumpFile = NULL;
static int s_missionGateDumpDone = 0;
static FILE *s_stagePadDumpFile = NULL;
static int s_stagePadDumpDone = 0;
static FILE *s_stageChrDumpFile = NULL;
static int s_stageChrDumpDone = 0;

#define TRACE_CHR_REACT_MAX 1000
#define TRACE_CHR_REACT_NEAR_MISS 0x01
#define TRACE_CHR_REACT_ALERTED   0x02
#define TRACE_CHR_REACT_SAW_SHOT  0x04
#define TRACE_CHR_REACT_SAW_DIE   0x08

static u8 s_traceChrReactLatched[TRACE_CHR_REACT_MAX];

#define TRACE_GUARD_SPAWN_EVENTS_MAX 8

typedef struct TraceGuardSpawnEvent {
    char reason[32];
    char source[16];
    int frame;
    int body;
    int head;
    int requested_pad;
    int resolved_pad;
    int source_chrnum;
    int target_chrnum;
    int flags;
    int free_count;
    int ai_list;
    int ai_global;
    int success;
    int chrnum;
    int stan_room;
    int prop_first_room;
    int prop_room_count;
    int has_final_pos;
    float original_pos[3];
    float final_pos[3];
} TraceGuardSpawnEvent;

static TraceGuardSpawnEvent s_traceGuardSpawnEvents[TRACE_GUARD_SPAWN_EVENTS_MAX];
static int s_traceGuardSpawnEventCount = 0;
static int s_traceGuardSpawnOverflow = 0;

#define TRACE_BULLET_IMPACT_EVENTS_MAX 16

typedef struct TracePropSummary {
    int type;
    int obj_type;
    int chrnum;
    int obj;
    int item;
    int pad;
} TracePropSummary;

typedef struct TraceBulletImpactCreateEvent {
    TracePropSummary prop;
    int frame;
    int shot_id;
    int slot;
    int impact_type;
    int material_texturenum;
    int material_hit_sound;
    int material_impact_type;
    int room;
    int model_pos;
    int clear;
    int width;
    int height;
    float pos[3];
    float normal[3];
    float size[2];
    float normal_offset;
    float normal_unit[3];
    float axis_x[3];
    float axis_y[3];
    float raw_vertices[4][3];
    int rounded_vertices[4][3];
} TraceBulletImpactCreateEvent;

typedef struct TraceBulletImpactRenderEvent {
    TracePropSummary prop;
    int frame;
    int world;
    int alpha_pass;
    int flat;
    int rendered;
    int last_impact;
    int current_slot;
} TraceBulletImpactRenderEvent;

static TraceBulletImpactCreateEvent s_traceBulletImpactCreates[TRACE_BULLET_IMPACT_EVENTS_MAX];
static TraceBulletImpactRenderEvent s_traceBulletImpactRenders[TRACE_BULLET_IMPACT_EVENTS_MAX];
static int s_traceBulletImpactCreateCount = 0;
static int s_traceBulletImpactRenderCount = 0;
static int s_traceBulletImpactOverflow = 0;
static int s_traceBulletImpactPendingMaterialValid = 0;
static int s_traceBulletImpactPendingTexturenum = -1;
static int s_traceBulletImpactPendingHitSound = -1;
static int s_traceBulletImpactPendingImpactType = -1;

#define TRACE_PROJECTILE_EVENTS_MAX 16

typedef struct TraceProjectileMotionEvent {
    char phase[24];
    TracePropSummary hitprop;
    int frame;
    int obj;
    int type;
    int pad;
    int result;
    int hitprop_pad;
    int hitprop_flags;
    u32 projflags;
    int rooms[4];
    float pos[3];
    float prop_pos[3];
    float hit[3];
    float normal[3];
    float hitprop_pos[3];
    float to[3];
} TraceProjectileMotionEvent;

static TraceProjectileMotionEvent s_traceProjectileMotionEvents[TRACE_PROJECTILE_EVENTS_MAX];
static int s_traceProjectileMotionEventCount = 0;
static int s_traceProjectileMotionOverflow = 0;

#define TRACE_SHOT_EVENTS_MAX 128

typedef struct TraceShotEvent {
    char phase[24];
    TracePropSummary prop;
    int frame;
    int shot_id;
    int hand;
    int weapon;
    int slot;
    int obj_pad;
    int hit_result;
    int do_damage;
    int mtx_index;
    int texture;
    int player_room;
    int best_room;
    int hit_bg;
    int hit_something;
    int hit_count;
    int shoot_through;
    int impact_type;
    int ray_valid;
    int dest_valid;
    float distance;
    float threshold;
    float pos[3];
    float normal[3];
    float screen_pos[3];
    float screen_dir[3];
    float attacker_pos[3];
    float world_dir[3];
    float dest[3];
} TraceShotEvent;

static TraceShotEvent s_traceShotEvents[TRACE_SHOT_EVENTS_MAX];
static int s_traceShotEventCount = 0;
static int s_traceShotOverflow = 0;
static int s_traceCurrentShotId = -1;
static int s_traceNextShotId = 0;

#define TRACE_GUARD_HIT_EVENTS_MAX 16

typedef struct TraceGuardHitEvent {
    char phase[16];
    int frame;
    int shot_id;
    int chrnum;
    int initial_hitpart;
    int final_hitpart;
    int weapon;
    int is_player;
    int accepted;
    int reason;
    int action_before;
    int action_after;
    int hidden_before;
    int hidden_after;
    int chrflags_before;
    int chrflags_after;
    int numarghs_before;
    int numarghs_after;
    int numclose_before;
    int numclose_after;
    int hat_action;
    int preargh;
    int lethal;
    int dropped_right;
    int dropped_left;
    int dropped_hat;
    int impact_target;
    int impact_created;
    int model_pos;
    float damage_before;
    float damage_after;
    float maxdamage;
    float damage_delta;
    float angle;
} TraceGuardHitEvent;

static TraceGuardHitEvent s_traceGuardHitEvents[TRACE_GUARD_HIT_EVENTS_MAX];
static int s_traceGuardHitEventCount = 0;
static int s_traceGuardHitOverflow = 0;

/* Running total of accepted player-inflicted guard hits, for the combat/floor
 * oracle (FID-0032) "combat_oracle.combat.hits_landed_total" field. Trace-only:
 * never read by sim code, never registered in the sim-state hash. */
static s32 s_combatOracleHitsTotal = 0;

#define TRACE_FORCED_GUARD_HIT_EVENTS_MAX 8

typedef struct TraceForcedGuardHitEvent {
    char source[24];
    int frame;
    int chrnum;
    int hitpart;
    int collision;
    int texture;
    int mtx_index;
    int queue_node;
    int hit_node;
    int model;
    int aim_valid;
    float origin[3];
    float point[3];
    float dir[3];
    float world_origin[3];
    float world_point[3];
    float world_dir[3];
} TraceForcedGuardHitEvent;

static TraceForcedGuardHitEvent s_traceForcedGuardHitEvents[TRACE_FORCED_GUARD_HIT_EVENTS_MAX];
static int s_traceForcedGuardHitEventCount = 0;
static int s_traceForcedGuardHitOverflow = 0;

/*
 * Mission completion/cleanup can flush many pending held-item drops in one
 * native trace frame; keep the trace lossless for full-route artifact health.
 */
#define TRACE_GUARD_DROP_EVENTS_MAX 128
#define TRACE_GUARD_DROP_JSON_SIZE 65536
#define TRACE_FRAME_JSON_LINE_SIZE 131072

typedef struct TraceGuardDropEvent {
    char phase[16];
    int frame;
    int chrnum;
    int action;
    int hidden;
    int child_type;
    int detail;
    int dropped;
    u32 runtime;
    int owner_type;
    int owner_chrnum;
    int owner_obj;
    int next_type;
    int room_stan;
    int room_first;
    int room_count;
    int room_any_rendered;
    int room_first_rendered;
    char rooms_json[64];
    float damage;
    float maxdamage;
    float pos[3];
    float scale;
    float basis[3];
} TraceGuardDropEvent;

static TraceGuardDropEvent s_traceGuardDropEvents[TRACE_GUARD_DROP_EVENTS_MAX];
static int s_traceGuardDropEventCount = 0;
static int s_traceGuardDropOverflow = 0;

static int traceJsonEventTraceEnabled(void)
{
    return traceGuardOracleEnabled() || g_traceStatePath != NULL;
}

static void traceCopyToken(char *dst, size_t dst_size, const char *src);

static int traceStateFileOpen(const char *path)
{
    if (path == NULL) {
        return -1;
    }

#ifdef _WIN32
    return _open(path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
#endif
}

static void traceStateFileClose(void)
{
    if (s_traceFd < 0) {
        return;
    }

#ifdef _WIN32
    _close(s_traceFd);
#else
    close(s_traceFd);
#endif
    s_traceFd = -1;
}

static void traceWriteStateLine(const char *line, size_t len)
{
    if (s_traceFd < 0 || line == NULL || len == 0) {
        return;
    }

    /*
     * Keep JSONL records out of stdio's buffered write path. Crash recovery can
     * reenter the main loop after an interrupted libc fwrite, leaving half a
     * frame record in the trace and corrupting artifact-health checks.
     */
#ifdef _WIN32
    {
        const char *cursor = line;
        size_t remaining = len;

        while (remaining > 0) {
            unsigned int chunk = remaining > (size_t)INT_MAX ? (unsigned int)INT_MAX : (unsigned int)remaining;
            int wrote = _write(s_traceFd, cursor, chunk);

            if (wrote <= 0) {
                return;
            }

            cursor += wrote;
            remaining -= (size_t)wrote;
        }
    }
#else
    {
        const char *cursor = line;
        size_t remaining = len;

        while (remaining > 0) {
            ssize_t wrote = write(s_traceFd, cursor, remaining);

            if (wrote < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return;
            }

            if (wrote == 0) {
                return;
            }

            cursor += wrote;
            remaining -= (size_t)wrote;
        }
    }
#endif
}

void portTraceLatchChrReaction(const ChrRecord *chr)
{
    u8 mask = 0;

    if (chr == NULL || chr->chrnum < 0 || chr->chrnum >= TRACE_CHR_REACT_MAX) {
        return;
    }

    if (chr->chrflags & CHRFLAG_NEAR_MISS) {
        mask |= TRACE_CHR_REACT_NEAR_MISS;
    }

    if (chr->hidden & CHRHIDDEN_ALERT_GUARD_RELATED) {
        mask |= TRACE_CHR_REACT_ALERTED;
    }

    if (chr->chrseeshot >= 0) {
        mask |= TRACE_CHR_REACT_SAW_SHOT;
    }

    if (chr->chrseedie >= 0) {
        mask |= TRACE_CHR_REACT_SAW_DIE;
    }

    s_traceChrReactLatched[chr->chrnum] |= mask;
}

static void traceSummarizeProp(const PropRecord *prop, TracePropSummary *out)
{
    memset(out, 0, sizeof(*out));
    out->type = -1;
    out->obj_type = -1;
    out->chrnum = -1;
    out->obj = -1;
    out->item = -1;
    out->pad = -1;

    if (prop == NULL) {
        return;
    }

    out->type = prop->type;

    if (prop->type == PROP_TYPE_CHR && prop->chr != NULL) {
        out->chrnum = prop->chr->chrnum;
        return;
    }

    if ((prop->type == PROP_TYPE_OBJ || prop->type == PROP_TYPE_WEAPON || prop->type == PROP_TYPE_DOOR)
        && prop->obj != NULL) {
        out->obj_type = prop->obj->type;
        out->obj = prop->obj->obj;
        out->pad = prop->obj->pad;
    }

    if (prop->type == PROP_TYPE_WEAPON && prop->weapon != NULL) {
        out->item = prop->weapon->weaponnum;
    }
}

s32 portTraceBeginShot(void)
{
    if (!traceJsonEventTraceEnabled()) {
        return -1;
    }

    s_traceCurrentShotId = ++s_traceNextShotId;
    return s_traceCurrentShotId;
}

void portTraceEndShot(s32 shot_id)
{
    if (shot_id >= 0 && s_traceCurrentShotId == shot_id) {
        s_traceCurrentShotId = -1;
    }
}

void portTraceShotEvent(const char *phase,
                        s32 hand,
                        s32 weapon,
                        s32 slot,
                        const PropRecord *prop,
                        s32 obj_pad,
                        s32 hit_result,
                        s32 do_damage,
                        f32 distance,
                        s32 mtx_index,
                        s32 texture,
                        s32 player_room,
                        s32 best_room,
                        s32 hit_bg,
                        s32 hit_something,
                        s32 hit_count,
                        s32 shoot_through,
                        s32 impact_type,
                        const coord3d *pos,
                        const coord3d *normal,
                        const coord3d *screen_pos,
                        const coord3d *screen_dir,
                        const coord3d *attacker_pos,
                        const coord3d *world_dir,
                        const coord3d *dest,
                        f32 threshold)
{
    TraceShotEvent *event;

    if (!traceJsonEventTraceEnabled()) {
        return;
    }

    if (s_traceShotEventCount >= TRACE_SHOT_EVENTS_MAX) {
        s_traceShotOverflow++;
        return;
    }

    event = &s_traceShotEvents[s_traceShotEventCount++];
    memset(event, 0, sizeof(*event));
    traceCopyToken(event->phase, sizeof(event->phase), phase);
    traceSummarizeProp(prop, &event->prop);
    event->frame = g_frame_count_diag;
    event->shot_id = s_traceCurrentShotId;
    event->hand = hand;
    event->weapon = weapon;
    event->slot = slot;
    event->obj_pad = obj_pad;
    event->hit_result = hit_result;
    event->do_damage = do_damage;
    event->distance = distance;
    event->mtx_index = mtx_index;
    event->texture = texture;
    event->player_room = player_room;
    event->best_room = best_room;
    event->hit_bg = hit_bg;
    event->hit_something = hit_something;
    event->hit_count = hit_count;
    event->shoot_through = shoot_through;
    event->impact_type = impact_type;
    event->threshold = threshold;

    if (pos != NULL) {
        event->pos[0] = pos->x;
        event->pos[1] = pos->y;
        event->pos[2] = pos->z;
    }

    if (normal != NULL) {
        event->normal[0] = normal->x;
        event->normal[1] = normal->y;
        event->normal[2] = normal->z;
    }

    if (screen_pos != NULL && screen_dir != NULL && attacker_pos != NULL && world_dir != NULL) {
        event->ray_valid = 1;
        event->screen_pos[0] = screen_pos->x;
        event->screen_pos[1] = screen_pos->y;
        event->screen_pos[2] = screen_pos->z;
        event->screen_dir[0] = screen_dir->x;
        event->screen_dir[1] = screen_dir->y;
        event->screen_dir[2] = screen_dir->z;
        event->attacker_pos[0] = attacker_pos->x;
        event->attacker_pos[1] = attacker_pos->y;
        event->attacker_pos[2] = attacker_pos->z;
        event->world_dir[0] = world_dir->x;
        event->world_dir[1] = world_dir->y;
        event->world_dir[2] = world_dir->z;
    }

    if (dest != NULL) {
        event->dest_valid = 1;
        event->dest[0] = dest->x;
        event->dest[1] = dest->y;
        event->dest[2] = dest->z;
    }
}

void portTraceBulletImpactCreate(s32 slot,
                                 s32 impact_type,
                                 s32 room,
                                 const PropRecord *prop,
                                 s32 model_pos,
                                 s32 clear,
                                 s32 width,
                                 s32 height,
                                 const coord3d *pos,
                                 const coord3d *normal,
                                 f32 size_x,
                                 f32 size_y,
                                 f32 normal_offset,
                                 const f32 normal_unit[3],
                                 const f32 axis_x[3],
                                 const f32 axis_y[3],
                                 const f32 raw_vertices[4][3],
                                 const s16 rounded_vertices[4][3])
{
    TraceBulletImpactCreateEvent *event;
    int i;
    int j;

    if (!traceJsonEventTraceEnabled()) {
        return;
    }

    if (s_traceBulletImpactCreateCount >= TRACE_BULLET_IMPACT_EVENTS_MAX) {
        s_traceBulletImpactOverflow++;
        s_traceBulletImpactPendingMaterialValid = 0;
        return;
    }

    event = &s_traceBulletImpactCreates[s_traceBulletImpactCreateCount++];
    memset(event, 0, sizeof(*event));
    traceSummarizeProp(prop, &event->prop);

    event->frame = g_frame_count_diag;
    event->shot_id = s_traceCurrentShotId;
    event->slot = slot;
    event->impact_type = impact_type;
    event->material_texturenum = -1;
    event->material_hit_sound = -1;
    event->material_impact_type = -1;
    event->room = room;
    event->model_pos = model_pos;
    event->clear = clear;
    event->width = width;
    event->height = height;
    event->size[0] = size_x;
    event->size[1] = size_y;
    event->normal_offset = normal_offset;

    if (pos != NULL) {
        event->pos[0] = pos->x;
        event->pos[1] = pos->y;
        event->pos[2] = pos->z;
    }

    if (normal != NULL) {
        event->normal[0] = normal->x;
        event->normal[1] = normal->y;
        event->normal[2] = normal->z;
    }

    if (normal_unit != NULL) {
        event->normal_unit[0] = normal_unit[0];
        event->normal_unit[1] = normal_unit[1];
        event->normal_unit[2] = normal_unit[2];
    }

    if (axis_x != NULL) {
        event->axis_x[0] = axis_x[0];
        event->axis_x[1] = axis_x[1];
        event->axis_x[2] = axis_x[2];
    }

    if (axis_y != NULL) {
        event->axis_y[0] = axis_y[0];
        event->axis_y[1] = axis_y[1];
        event->axis_y[2] = axis_y[2];
    }

    if (raw_vertices != NULL) {
        for (i = 0; i < 4; i++) {
            for (j = 0; j < 3; j++) {
                event->raw_vertices[i][j] = raw_vertices[i][j];
            }
        }
    }

    if (rounded_vertices != NULL) {
        for (i = 0; i < 4; i++) {
            for (j = 0; j < 3; j++) {
                event->rounded_vertices[i][j] = rounded_vertices[i][j];
            }
        }
    }

    if (s_traceBulletImpactPendingMaterialValid) {
        event->material_texturenum = s_traceBulletImpactPendingTexturenum;
        event->material_hit_sound = s_traceBulletImpactPendingHitSound;
        event->material_impact_type = s_traceBulletImpactPendingImpactType;
        s_traceBulletImpactPendingMaterialValid = 0;
    }
}

void portTraceBulletImpactMaterial(s32 texturenum, s32 hit_sound, s32 impact_type)
{
    if (!traceJsonEventTraceEnabled()) {
        return;
    }

    s_traceBulletImpactPendingMaterialValid = 1;
    s_traceBulletImpactPendingTexturenum = texturenum;
    s_traceBulletImpactPendingHitSound = hit_sound;
    s_traceBulletImpactPendingImpactType = impact_type;
}

void portTraceBulletImpactRender(const PropRecord *prop,
                                 s32 world,
                                 s32 alpha_pass,
                                 s32 flat,
                                 s32 rendered,
                                 s32 last_impact,
                                 s32 current_slot)
{
    TraceBulletImpactRenderEvent *event;

    if (!traceJsonEventTraceEnabled() || rendered <= 0) {
        return;
    }

    if (s_traceBulletImpactRenderCount >= TRACE_BULLET_IMPACT_EVENTS_MAX) {
        s_traceBulletImpactOverflow++;
        return;
    }

    event = &s_traceBulletImpactRenders[s_traceBulletImpactRenderCount++];
    memset(event, 0, sizeof(*event));
    traceSummarizeProp(prop, &event->prop);

    event->frame = g_frame_count_diag;
    event->world = world;
    event->alpha_pass = alpha_pass;
    event->flat = flat;
    event->rendered = rendered;
    event->last_impact = last_impact;
    event->current_slot = current_slot;
}

void portTraceProjectileMotionEnd(const ObjectRecord *obj,
                                  const PropRecord *projectile_prop,
                                  s32 result,
                                  const coord3d *hit,
                                  const coord3d *normal,
                                  const PropRecord *hitprop,
                                  u32 projflags,
                                  s32 room0,
                                  s32 room1,
                                  s32 room2,
                                  s32 room3)
{
    TraceProjectileMotionEvent *event;

    if (!traceJsonEventTraceEnabled()) {
        return;
    }

    if (s_traceProjectileMotionEventCount >= TRACE_PROJECTILE_EVENTS_MAX) {
        s_traceProjectileMotionOverflow++;
        return;
    }

    event = &s_traceProjectileMotionEvents[s_traceProjectileMotionEventCount++];
    memset(event, 0, sizeof(*event));
    traceCopyToken(event->phase, sizeof(event->phase), "motion_end");
    traceSummarizeProp(hitprop, &event->hitprop);

    event->frame = g_frame_count_diag;
    event->obj = obj != NULL ? obj->obj : -1;
    event->type = obj != NULL ? obj->type : -1;
    event->pad = obj != NULL ? obj->pad : -1;
    event->result = result;
    event->hitprop_pad = -1;
    event->hitprop_flags = hitprop != NULL ? hitprop->flags : 0;
    event->projflags = projflags;
    event->rooms[0] = room0;
    event->rooms[1] = room1;
    event->rooms[2] = room2;
    event->rooms[3] = room3;

    if (obj != NULL) {
        event->pos[0] = obj->runtime_pos.x;
        event->pos[1] = obj->runtime_pos.y;
        event->pos[2] = obj->runtime_pos.z;
    }

    if (projectile_prop != NULL) {
        event->prop_pos[0] = projectile_prop->pos.x;
        event->prop_pos[1] = projectile_prop->pos.y;
        event->prop_pos[2] = projectile_prop->pos.z;
    }

    if (hit != NULL) {
        event->hit[0] = hit->x;
        event->hit[1] = hit->y;
        event->hit[2] = hit->z;
        event->to[0] = hit->x;
        event->to[1] = hit->y;
        event->to[2] = hit->z;
    }

    if (normal != NULL) {
        event->normal[0] = normal->x;
        event->normal[1] = normal->y;
        event->normal[2] = normal->z;
    }

    if (hitprop != NULL) {
        event->hitprop_pos[0] = hitprop->pos.x;
        event->hitprop_pos[1] = hitprop->pos.y;
        event->hitprop_pos[2] = hitprop->pos.z;
        if ((hitprop->type == PROP_TYPE_OBJ ||
             hitprop->type == PROP_TYPE_WEAPON ||
             hitprop->type == PROP_TYPE_DOOR)
            && hitprop->obj != NULL) {
            event->hitprop_pad = hitprop->obj->pad;
        }
    }
}

void portTraceProjectileImpactReposition(const ObjectRecord *obj,
                                         const PropRecord *projectile_prop,
                                         const coord3d *impact_pos)
{
    TraceProjectileMotionEvent *event;

    if (!traceJsonEventTraceEnabled()) {
        return;
    }

    if (obj == NULL || projectile_prop == NULL || impact_pos == NULL) {
        return;
    }

    if (s_traceProjectileMotionEventCount >= TRACE_PROJECTILE_EVENTS_MAX) {
        s_traceProjectileMotionOverflow++;
        return;
    }

    event = &s_traceProjectileMotionEvents[s_traceProjectileMotionEventCount++];
    memset(event, 0, sizeof(*event));
    traceCopyToken(event->phase, sizeof(event->phase), "impact_reposition");
    traceSummarizeProp(NULL, &event->hitprop);

    event->frame = g_frame_count_diag;
    event->obj = obj->obj;
    event->type = obj->type;
    event->pad = obj->pad;
    event->result = -1;
    event->hitprop_pad = -1;
    event->hitprop_flags = 0;
    event->projflags = obj->projectile != NULL ? obj->projectile->flags : 0;
    event->rooms[0] = obj->projectile != NULL ? obj->projectile->unkCC : -1;
    event->rooms[1] = obj->projectile != NULL ? obj->projectile->unkCD : -1;
    event->rooms[2] = obj->projectile != NULL ? obj->projectile->unkCE : -1;
    event->rooms[3] = obj->projectile != NULL ? obj->projectile->unkCF : -1;

    event->pos[0] = obj->runtime_pos.x;
    event->pos[1] = obj->runtime_pos.y;
    event->pos[2] = obj->runtime_pos.z;
    event->prop_pos[0] = projectile_prop->pos.x;
    event->prop_pos[1] = projectile_prop->pos.y;
    event->prop_pos[2] = projectile_prop->pos.z;
    event->hit[0] = impact_pos->x;
    event->hit[1] = impact_pos->y;
    event->hit[2] = impact_pos->z;
    event->to[0] = impact_pos->x;
    event->to[1] = impact_pos->y;
    event->to[2] = impact_pos->z;
}

static void traceGuardHitEvent(const char *phase,
                               const ChrRecord *chr,
                               s32 initial_hitpart,
                               s32 final_hitpart,
                               s32 weapon,
                               s32 is_player,
                               s32 accepted,
                               s32 reason,
                               f32 damage_before,
                               f32 damage_after,
                               f32 maxdamage,
                               s32 action_before,
                               s32 action_after,
                               s32 hidden_before,
                               s32 hidden_after,
                               s32 chrflags_before,
                               s32 chrflags_after,
                               s32 numarghs_before,
                               s32 numarghs_after,
                               s32 numclose_before,
                               s32 numclose_after,
                               s32 hat_action,
                               s32 preargh,
                               s32 dropped_right,
                               s32 dropped_left,
                               s32 dropped_hat,
                               s32 impact_target,
                               s32 impact_created,
                               s32 model_pos,
                               f32 angle)
{
    TraceGuardHitEvent *event;

    if (!traceJsonEventTraceEnabled()) {
        return;
    }

    if (s_traceGuardHitEventCount >= TRACE_GUARD_HIT_EVENTS_MAX) {
        s_traceGuardHitOverflow++;
        return;
    }

    event = &s_traceGuardHitEvents[s_traceGuardHitEventCount++];
    memset(event, 0, sizeof(*event));
    traceCopyToken(event->phase, sizeof(event->phase), phase);

    event->frame = g_frame_count_diag;
    event->shot_id = s_traceCurrentShotId;
    event->chrnum = chr != NULL ? chr->chrnum : -1;
    event->initial_hitpart = initial_hitpart;
    event->final_hitpart = final_hitpart;
    event->weapon = weapon;
    event->is_player = is_player;
    event->accepted = accepted;
    event->reason = reason;
    event->damage_before = damage_before;
    event->damage_after = damage_after;
    event->maxdamage = maxdamage;
    event->damage_delta = damage_after - damage_before;
    event->action_before = action_before;
    event->action_after = action_after;
    event->hidden_before = hidden_before;
    event->hidden_after = hidden_after;
    event->chrflags_before = chrflags_before;
    event->chrflags_after = chrflags_after;
    event->numarghs_before = numarghs_before;
    event->numarghs_after = numarghs_after;
    event->numclose_before = numclose_before;
    event->numclose_after = numclose_after;
    event->hat_action = hat_action;
    event->preargh = preargh;
    event->lethal = (maxdamage > 0.0f && damage_after >= maxdamage) ? 1 : 0;
    event->dropped_right = dropped_right;
    event->dropped_left = dropped_left;
    event->dropped_hat = dropped_hat;
    event->impact_target = impact_target;
    event->impact_created = impact_created;
    event->model_pos = model_pos;
    event->angle = angle;
}

void portTraceGuardHitApply(const ChrRecord *chr,
                            s32 initial_hitpart,
                            s32 final_hitpart,
                            s32 weapon,
                            s32 is_player,
                            s32 accepted,
                            s32 reason,
                            f32 damage_before,
                            f32 damage_after,
                            f32 maxdamage,
                            s32 action_before,
                            s32 action_after,
                            s32 hidden_before,
                            s32 hidden_after,
                            s32 chrflags_before,
                            s32 chrflags_after,
                            s32 numarghs_before,
                            s32 numarghs_after,
                            s32 numclose_before,
                            s32 numclose_after,
                            s32 hat_action,
                            s32 preargh,
                            f32 angle)
{
    if (is_player != 0 && accepted != 0) {
        s_combatOracleHitsTotal++;
    }
    traceGuardHitEvent("apply",
                       chr,
                       initial_hitpart,
                       final_hitpart,
                       weapon,
                       is_player,
                       accepted,
                       reason,
                       damage_before,
                       damage_after,
                       maxdamage,
                       action_before,
                       action_after,
                       hidden_before,
                       hidden_after,
                       chrflags_before,
                       chrflags_after,
                       numarghs_before,
                       numarghs_after,
                       numclose_before,
                       numclose_after,
                       hat_action,
                       preargh,
                       0,
                       0,
                       0,
                       -1,
                       0,
                       -1,
                       angle);
}

void portTraceGuardHitAnimation(const ChrRecord *chr,
                                s32 hitpart,
                                s32 weapon,
                                f32 damage_before,
                                f32 damage_after,
                                f32 maxdamage,
                                s32 action_before,
                                s32 action_after,
                                s32 hidden_before,
                                s32 hidden_after,
                                s32 chrflags_before,
                                s32 chrflags_after,
                                s32 dropped_right,
                                s32 dropped_left,
                                s32 dropped_hat,
                                f32 angle)
{
    traceGuardHitEvent("anim",
                       chr,
                       hitpart,
                       hitpart,
                       weapon,
                       0,
                       1,
                       0,
                       damage_before,
                       damage_after,
                       maxdamage,
                       action_before,
                       action_after,
                       hidden_before,
                       hidden_after,
                       chrflags_before,
                       chrflags_after,
                       chr != NULL ? chr->numarghs : 0,
                       chr != NULL ? chr->numarghs : 0,
                       chr != NULL ? chr->numclosearghs : 0,
                       chr != NULL ? chr->numclosearghs : 0,
                       0,
                       action_after == ACT_PREARGH ? 1 : 0,
                       dropped_right,
                       dropped_left,
                       dropped_hat,
                       -1,
                       0,
                       -1,
                       angle);
}

void portTraceGuardHitImpact(const ChrRecord *chr,
                             s32 hitpart,
                             s32 weapon,
                             s32 impact_target,
                             s32 impact_created,
                             s32 model_pos)
{
    traceGuardHitEvent("impact",
                       chr,
                       hitpart,
                       hitpart,
                       weapon,
                       1,
                       1,
                       0,
                       chr != NULL ? chr->damage : 0.0f,
                       chr != NULL ? chr->damage : 0.0f,
                       chr != NULL ? chr->maxdamage : 0.0f,
                       chr != NULL ? chr->actiontype : -1,
                       chr != NULL ? chr->actiontype : -1,
                       chr != NULL ? chr->hidden : 0,
                       chr != NULL ? chr->hidden : 0,
                       chr != NULL ? chr->chrflags : 0,
                       chr != NULL ? chr->chrflags : 0,
                       chr != NULL ? chr->numarghs : 0,
                       chr != NULL ? chr->numarghs : 0,
                       chr != NULL ? chr->numclosearghs : 0,
                       chr != NULL ? chr->numclosearghs : 0,
                       0,
                       0,
                       0,
                       0,
                       0,
                       impact_target,
                       impact_created,
                       model_pos,
                       0.0f);
}

void portTraceForcedGuardHitEvent(s32 chrnum,
                                  s32 hitpart,
                                  const char *source,
                                  s32 collision,
                                  s32 texture,
                                  s32 mtx_index,
                                  s32 queue_node,
                                  s32 hit_node,
                                  s32 model,
                                  const coord3d *origin,
                                  const coord3d *point,
                                  const coord3d *dir,
                                  const coord3d *world_origin,
                                  const coord3d *world_point,
                                  const coord3d *world_dir)
{
    TraceForcedGuardHitEvent *event;

    if (!traceJsonEventTraceEnabled()) {
        return;
    }

    if (s_traceForcedGuardHitEventCount >= TRACE_FORCED_GUARD_HIT_EVENTS_MAX) {
        s_traceForcedGuardHitOverflow++;
        return;
    }

    event = &s_traceForcedGuardHitEvents[s_traceForcedGuardHitEventCount++];
    memset(event, 0, sizeof(*event));
    traceCopyToken(event->source, sizeof(event->source), source);

    event->frame = g_frame_count_diag;
    event->chrnum = chrnum;
    event->hitpart = hitpart;
    event->collision = collision;
    event->texture = texture;
    event->mtx_index = mtx_index;
    event->queue_node = queue_node;
    event->hit_node = hit_node;
    event->model = model;

    if (origin != NULL && point != NULL && dir != NULL) {
        event->aim_valid = 1;
        event->origin[0] = origin->x;
        event->origin[1] = origin->y;
        event->origin[2] = origin->z;
        event->point[0] = point->x;
        event->point[1] = point->y;
        event->point[2] = point->z;
        event->dir[0] = dir->x;
        event->dir[1] = dir->y;
        event->dir[2] = dir->z;
        if (world_origin != NULL && world_point != NULL && world_dir != NULL) {
            event->world_origin[0] = world_origin->x;
            event->world_origin[1] = world_origin->y;
            event->world_origin[2] = world_origin->z;
            event->world_point[0] = world_point->x;
            event->world_point[1] = world_point->y;
            event->world_point[2] = world_point->z;
            event->world_dir[0] = world_dir->x;
            event->world_dir[1] = world_dir->y;
            event->world_dir[2] = world_dir->z;
        }
    }
}

static void traceCopyToken(char *dst, size_t dst_size, const char *src)
{
    size_t i;

    if (dst_size == 0) {
        return;
    }

    if (src == NULL) {
        src = "unknown";
    }

    for (i = 0; i + 1 < dst_size && src[i] != '\0'; i++) {
        char c = src[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            dst[i] = c;
        } else {
            dst[i] = '_';
        }
    }

    dst[i] = '\0';
}

void portTraceGuardSpawnEvent(const char *source,
                              const char *reason,
                              s32 source_chrnum,
                              s32 target_chrnum,
                              s32 bodynum,
                              s32 headnum,
                              s32 requested_pad,
                              s32 resolved_pad,
                              s32 flags,
                              s32 free_count,
                              const coord3d *original_pos,
                              const coord3d *final_pos,
                              const StandTile *stan,
                              AIRecord *ailist,
                              const PropRecord *prop)
{
    TraceGuardSpawnEvent *event;
    bool is_global_ai_list = false;
    s32 i;

    if (!traceGuardOracleEnabled() && g_traceStatePath == NULL) {
        return;
    }

    if (s_traceGuardSpawnEventCount >= TRACE_GUARD_SPAWN_EVENTS_MAX) {
        s_traceGuardSpawnOverflow++;
        return;
    }

    event = &s_traceGuardSpawnEvents[s_traceGuardSpawnEventCount++];
    memset(event, 0, sizeof(*event));

    traceCopyToken(event->source, sizeof(event->source), source);
    traceCopyToken(event->reason, sizeof(event->reason), reason);

    event->frame = g_frame_count_diag;
    event->body = bodynum;
    event->head = headnum;
    event->requested_pad = requested_pad;
    event->resolved_pad = resolved_pad;
    event->source_chrnum = source_chrnum;
    event->target_chrnum = target_chrnum;
    event->flags = flags;
    event->free_count = free_count;
    event->success = prop != NULL ? 1 : 0;
    event->chrnum = (prop != NULL && prop->chr != NULL) ? prop->chr->chrnum : -1;
    event->stan_room = stan != NULL ? stan->room : -1;
    event->prop_first_room = -1;

    if (ailist != NULL) {
        event->ai_list = chraiGetAIListID(ailist, &is_global_ai_list);
        event->ai_global = is_global_ai_list ? 1 : 0;
    } else {
        event->ai_list = -1;
        event->ai_global = 0;
    }

    if (prop != NULL) {
        for (i = 0; i < PROPRECORD_STAN_ROOM_LEN && prop->rooms[i] != 0xFF; i++) {
            if (i == 0) {
                event->prop_first_room = prop->rooms[i];
            }
        }
        event->prop_room_count = i;
    }

    if (original_pos != NULL) {
        event->original_pos[0] = original_pos->x;
        event->original_pos[1] = original_pos->y;
        event->original_pos[2] = original_pos->z;
    }

    if (final_pos != NULL) {
        event->has_final_pos = 1;
        event->final_pos[0] = final_pos->x;
        event->final_pos[1] = final_pos->y;
        event->final_pos[2] = final_pos->z;
    }
}

static int traceMissionGateDumpEnabled(void) {
    return traceMissionGateDumpPath() != NULL;
}

static int traceMissionGateDumpFrame(void) {
    static int initialized = 0;
    static int value = 1;

    if (!initialized) {
        const char *env_value = getenv("GE007_DUMP_MISSION_GATES_FRAME");
        if (env_value != NULL && env_value[0] != '\0') {
            int parsed = atoi(env_value);
            if (parsed > 0) {
                value = parsed;
            }
        }
        initialized = 1;
    }

    return value;
}

static void traceMissionGateDumpWriteLine(const char *fmt, ...) {
    va_list ap;

    if (s_missionGateDumpFile == NULL) {
        return;
    }

    va_start(ap, fmt);
    vfprintf(s_missionGateDumpFile, fmt, ap);
    va_end(ap);
    fputc('\n', s_missionGateDumpFile);
}

static void traceStagePadDumpWriteLine(const char *fmt, ...) {
    va_list ap;

    if (s_stagePadDumpFile == NULL) {
        return;
    }

    va_start(ap, fmt);
    vfprintf(s_stagePadDumpFile, fmt, ap);
    va_end(ap);
    fputc('\n', s_stagePadDumpFile);
}

static void traceStageChrDumpWriteLine(const char *fmt, ...) {
    va_list ap;

    if (s_stageChrDumpFile == NULL) {
        return;
    }

    va_start(ap, fmt);
    vfprintf(s_stageChrDumpFile, fmt, ap);
    va_end(ap);
    fputc('\n', s_stageChrDumpFile);
}

static int traceMissionGateResolveObjectPos(const ObjectRecord *obj,
                                            float *out_x,
                                            float *out_y,
                                            float *out_z,
                                            int *out_room) {
    if (out_x) *out_x = 0.0f;
    if (out_y) *out_y = 0.0f;
    if (out_z) *out_z = 0.0f;
    if (out_room) *out_room = -1;

    if (obj == NULL || obj->pad < 0) {
        return 0;
    }

    if (obj->type == PROPDEF_DOOR) {
        const DoorRecord *door = (const DoorRecord *)obj;
        if (g_CurrentSetup.boundpads == NULL) {
            return 0;
        }
        if (out_x) *out_x = g_CurrentSetup.boundpads[door->pad].pos.x;
        if (out_y) *out_y = g_CurrentSetup.boundpads[door->pad].pos.y;
        if (out_z) *out_z = g_CurrentSetup.boundpads[door->pad].pos.z;
        if (out_room) {
            *out_room = g_CurrentSetup.boundpads[door->pad].stan != NULL
                ? g_CurrentSetup.boundpads[door->pad].stan->room
                : -1;
        }
        return 1;
    }

    if (isNotBoundPad(obj->pad)) {
        if (g_CurrentSetup.pads == NULL) {
            return 0;
        }
        if (out_x) *out_x = g_CurrentSetup.pads[obj->pad].pos.x;
        if (out_y) *out_y = g_CurrentSetup.pads[obj->pad].pos.y;
        if (out_z) *out_z = g_CurrentSetup.pads[obj->pad].pos.z;
        if (out_room) {
            *out_room = g_CurrentSetup.pads[obj->pad].stan != NULL
                ? g_CurrentSetup.pads[obj->pad].stan->room
                : -1;
        }
        return 1;
    }

    if (g_CurrentSetup.boundpads == NULL) {
        return 0;
    }

    {
        s32 bound_pad = getBoundPadNum(obj->pad);
        if (out_x) *out_x = g_CurrentSetup.boundpads[bound_pad].pos.x;
        if (out_y) *out_y = g_CurrentSetup.boundpads[bound_pad].pos.y;
        if (out_z) *out_z = g_CurrentSetup.boundpads[bound_pad].pos.z;
        if (out_room) {
            *out_room = g_CurrentSetup.boundpads[bound_pad].stan != NULL
                ? g_CurrentSetup.boundpads[bound_pad].stan->room
                : -1;
        }
    }

    return 1;
}

static u32 traceReadU32Field(const void *base, size_t offset) {
    u32 value = 0;
    memcpy(&value, ((const u8 *)base) + offset, sizeof(value));
    return value;
}

static const char *tracePropDefName(s32 type) {
    switch (type) {
        case PROPDEF_DOOR: return "door";
        case PROPDEF_PROP: return "prop";
        case PROPDEF_KEY: return "key";
        case PROPDEF_ALARM: return "alarm";
        case PROPDEF_CCTV: return "cctv";
        case PROPDEF_COLLECTABLE: return "collectable";
        case PROPDEF_MONITOR: return "monitor";
        case PROPDEF_MULTI_MONITOR: return "multi_monitor";
        case PROPDEF_AUTOGUN: return "autogun";
        case PROPDEF_ARMOUR: return "armour";
        case PROPDEF_TAG: return "tag";
        case PROPDEF_OBJECTIVE_START: return "objective_start";
        case PROPDEF_OBJECTIVE_END: return "objective_end";
        case PROPDEF_OBJECTIVE_DESTROY_OBJECT: return "objective_destroy_object";
        case PROPDEF_OBJECTIVE_COMPLETE_CONDITION: return "objective_complete_condition";
        case PROPDEF_OBJECTIVE_FAIL_CONDITION: return "objective_fail_condition";
        case PROPDEF_OBJECTIVE_COLLECT_OBJECT: return "objective_collect_object";
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT: return "objective_deposit_object";
        case PROPDEF_OBJECTIVE_PHOTOGRAPH: return "objective_photograph";
        case PROPDEF_OBJECTIVE_NULL: return "objective_null";
        case PROPDEF_OBJECTIVE_ENTER_ROOM: return "objective_enter_room";
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM: return "objective_deposit_object_in_room";
        case PROPDEF_OBJECTIVE_COPY_ITEM: return "objective_copy_item";
        case PROPDEF_SWITCH: return "switch";
        case PROPDEF_LOCK_DOOR: return "lock_door";
        case PROPDEF_VEHICHLE: return "vehicle";
        case PROPDEF_AIRCRAFT: return "aircraft";
        case PROPDEF_GLASS: return "glass";
        case PROPDEF_SAFE: return "safe";
        case PROPDEF_SAFE_ITEM: return "safe_item";
        case PROPDEF_TANK: return "tank";
        case PROPDEF_TINTED_GLASS: return "tinted_glass";
        default: return "other";
    }
}

static int traceStageObjectIsMissionRelevant(s32 type) {
    switch (type) {
        case PROPDEF_DOOR:
        case PROPDEF_PROP:
        case PROPDEF_KEY:
        case PROPDEF_ALARM:
        case PROPDEF_CCTV:
        case PROPDEF_COLLECTABLE:
        case PROPDEF_MONITOR:
        case PROPDEF_MULTI_MONITOR:
        case PROPDEF_AUTOGUN:
        case PROPDEF_ARMOUR:
        case PROPDEF_VEHICHLE:
        case PROPDEF_AIRCRAFT:
        case PROPDEF_SAFE:
        case PROPDEF_GLASS:
        case PROPDEF_TINTED_GLASS:
        case PROPDEF_TANK:
            return 1;
        default:
            return 0;
    }
}

static void traceStageDumpObjectRecord(const PropDefHeaderRecord *cmd, int setup_index) {
    const ObjectRecord *obj = (const ObjectRecord *)cmd;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int room = -1;
    int has_pos = traceMissionGateResolveObjectPos(obj, &x, &y, &z, &room);

    traceStagePadDumpWriteLine(
        "{\"scope\":\"setup\",\"kind\":\"object\",\"index\":%d,"
        "\"type\":%d,\"type_name\":\"%s\",\"obj\":%d,\"pad\":%d,"
        "\"flags\":\"0x%08X\",\"flags2\":\"0x%08X\","
        "\"runtime\":\"0x%08X\",\"state\":%d,\"has_prop\":%d,\"has_model\":%d,"
        "\"damage\":%.4f,\"maxdamage\":%.4f,\"has_pos\":%d,\"room\":%d,"
        "\"pos\":[%.2f,%.2f,%.2f]}",
        setup_index,
        cmd->type,
        tracePropDefName(cmd->type),
        obj->obj,
        obj->pad,
        obj->flags,
        obj->flags2,
        obj->runtime_bitflags,
        obj->state,
        obj->prop != NULL ? 1 : 0,
        obj->model != NULL ? 1 : 0,
        obj->damage,
        obj->maxdamage,
        has_pos,
        room,
        x, y, z);
}

static void traceStageDumpTagRecord(const TagObjectRecord *tag, int setup_index) {
    const ObjectRecord *obj = tag->TaggedObject;
    traceStagePadDumpWriteLine(
        "{\"scope\":\"setup\",\"kind\":\"tag\",\"index\":%d,"
        "\"tag\":%d,\"offset\":%d,\"target_obj\":%d,\"target_type\":%d,"
        "\"target_type_name\":\"%s\",\"target_pad\":%d,\"target_runtime\":\"0x%08X\"}",
        setup_index,
        tag->ID,
        tag->OffsetToObj,
        obj != NULL ? obj->obj : -1,
        obj != NULL ? obj->type : -1,
        obj != NULL ? tracePropDefName(obj->type) : "none",
        obj != NULL ? obj->pad : -1,
        obj != NULL ? obj->runtime_bitflags : 0);
}

static void traceStageDumpObjectiveRecord(const PropDefHeaderRecord *cmd,
                                          int setup_index,
                                          int current_objective) {
    switch (cmd->type) {
        case PROPDEF_OBJECTIVE_START: {
            const struct objective_entry *entry = (const struct objective_entry *)cmd;
            int menu = (int)traceReadU32Field(entry, 4);
            int textid = (int)(traceReadU32Field(entry, 8) & 0xffff);
            int flags = (int)((traceReadU32Field(entry, 12) >> 8) & 0xff);
            int difficulty = (s8)(traceReadU32Field(entry, 12) & 0xff);
            int status = (menu >= 0 && menu < OBJECTIVES_MAX) ? (int)get_status_of_objective(menu) : -1;

            traceStagePadDumpWriteLine(
                "{\"scope\":\"setup\",\"kind\":\"objective\",\"index\":%d,"
                "\"type\":%d,\"type_name\":\"%s\",\"objective\":%d,"
                "\"text_id\":%d,\"difficulty\":%d,\"flags\":%d,\"status\":%d}",
                setup_index,
                cmd->type,
                tracePropDefName(cmd->type),
                menu,
                textid,
                difficulty,
                flags,
                status);
            break;
        }
        case PROPDEF_OBJECTIVE_END:
        case PROPDEF_OBJECTIVE_NULL:
        case PROPDEF_OBJECTIVE_DESTROY_OBJECT:
        case PROPDEF_OBJECTIVE_COMPLETE_CONDITION:
        case PROPDEF_OBJECTIVE_FAIL_CONDITION:
        case PROPDEF_OBJECTIVE_COLLECT_OBJECT:
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:
        case PROPDEF_OBJECTIVE_COPY_ITEM:
            traceStagePadDumpWriteLine(
                "{\"scope\":\"setup\",\"kind\":\"objective_criterion\",\"index\":%d,"
                "\"type\":%d,\"type_name\":\"%s\",\"objective\":%d,\"ref\":%u}",
                setup_index,
                cmd->type,
                tracePropDefName(cmd->type),
                current_objective,
                traceReadU32Field(cmd, 4));
            break;
        case PROPDEF_OBJECTIVE_PHOTOGRAPH: {
            const struct criteria_picture *criteria = (const struct criteria_picture *)cmd;
            traceStagePadDumpWriteLine(
                "{\"scope\":\"setup\",\"kind\":\"objective_criterion\",\"index\":%d,"
                "\"type\":%d,\"type_name\":\"%s\",\"objective\":%d,"
                "\"tag\":%d,\"flag\":%d}",
                setup_index,
                cmd->type,
                tracePropDefName(cmd->type),
                current_objective,
                criteria->tag_id,
                criteria->flag);
            break;
        }
        case PROPDEF_OBJECTIVE_ENTER_ROOM: {
            const struct criteria_roomentered *criteria = (const struct criteria_roomentered *)cmd;
            traceStagePadDumpWriteLine(
                "{\"scope\":\"setup\",\"kind\":\"objective_criterion\",\"index\":%d,"
                "\"type\":%d,\"type_name\":\"%s\",\"objective\":%d,"
                "\"pad\":%u,\"status\":%u}",
                setup_index,
                cmd->type,
                tracePropDefName(cmd->type),
                current_objective,
                criteria->pad,
                criteria->status);
            break;
        }
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM: {
            const struct criteria_deposit *criteria = (const struct criteria_deposit *)cmd;
            traceStagePadDumpWriteLine(
                "{\"scope\":\"setup\",\"kind\":\"objective_criterion\",\"index\":%d,"
                "\"type\":%d,\"type_name\":\"%s\",\"objective\":%d,"
                "\"weapon\":%d,\"pad\":%d,\"flag\":%d}",
                setup_index,
                cmd->type,
                tracePropDefName(cmd->type),
                current_objective,
                criteria->weaponnum,
                criteria->padid,
                criteria->flag);
            break;
        }
        default:
            break;
    }
}

static void traceStagePadDumpSetup(void) {
    PropDefHeaderRecord *cmd;
    int i;
    int setup_index = 0;
    int current_objective = -1;

    traceStagePadDumpWriteLine(
        "{\"scope\":\"meta\",\"kind\":\"stage\",\"level\":%d,\"difficulty\":%d,"
        "\"difficulty_mask\":\"0x%08X\",\"frame\":%d}",
        bossGetStageNum(),
        lvlGetSelectedDifficulty(),
        1U << (lvlGetSelectedDifficulty() + 4),
        s_traceFrame);

    if (g_CurrentSetup.pads != NULL) {
        for (i = 0; g_CurrentSetup.pads[i].plink != NULL; i++) {
            traceStagePadDumpWriteLine(
                "{\"scope\":\"setup\",\"kind\":\"pad\",\"pad\":%d,"
                "\"room\":%d,\"pos\":[%.2f,%.2f,%.2f],"
                "\"look\":[%.4f,%.4f,%.4f],\"up\":[%.4f,%.4f,%.4f]}",
                i,
                g_CurrentSetup.pads[i].stan != NULL ? g_CurrentSetup.pads[i].stan->room : -1,
                g_CurrentSetup.pads[i].pos.x,
                g_CurrentSetup.pads[i].pos.y,
                g_CurrentSetup.pads[i].pos.z,
                g_CurrentSetup.pads[i].look.x,
                g_CurrentSetup.pads[i].look.y,
                g_CurrentSetup.pads[i].look.z,
                g_CurrentSetup.pads[i].up.x,
                g_CurrentSetup.pads[i].up.y,
                g_CurrentSetup.pads[i].up.z);
        }
    }

    if (g_CurrentSetup.boundpads != NULL) {
        for (i = 0; g_CurrentSetup.boundpads[i].plink != NULL; i++) {
            traceStagePadDumpWriteLine(
                "{\"scope\":\"setup\",\"kind\":\"boundpad\",\"pad\":%d,"
                "\"room\":%d,\"pos\":[%.2f,%.2f,%.2f],"
                "\"look\":[%.4f,%.4f,%.4f],\"up\":[%.4f,%.4f,%.4f],"
                "\"bbox\":[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]}",
                i,
                g_CurrentSetup.boundpads[i].stan != NULL ? g_CurrentSetup.boundpads[i].stan->room : -1,
                g_CurrentSetup.boundpads[i].pos.x,
                g_CurrentSetup.boundpads[i].pos.y,
                g_CurrentSetup.boundpads[i].pos.z,
                g_CurrentSetup.boundpads[i].look.x,
                g_CurrentSetup.boundpads[i].look.y,
                g_CurrentSetup.boundpads[i].look.z,
                g_CurrentSetup.boundpads[i].up.x,
                g_CurrentSetup.boundpads[i].up.y,
                g_CurrentSetup.boundpads[i].up.z,
                g_CurrentSetup.boundpads[i].bbox.xmin,
                g_CurrentSetup.boundpads[i].bbox.xmax,
                g_CurrentSetup.boundpads[i].bbox.ymin,
                g_CurrentSetup.boundpads[i].bbox.ymax,
                g_CurrentSetup.boundpads[i].bbox.zmin,
                g_CurrentSetup.boundpads[i].bbox.zmax);
        }
    }

    if (g_CurrentSetup.propDefs == NULL) {
        return;
    }

    for (cmd = g_CurrentSetup.propDefs; cmd->type != PROPDEF_END; cmd += sizepropdef(cmd), setup_index++) {
        if (traceStageObjectIsMissionRelevant(cmd->type)) {
            traceStageDumpObjectRecord(cmd, setup_index);
        } else if (cmd->type == PROPDEF_TAG) {
            traceStageDumpTagRecord((const TagObjectRecord *)cmd, setup_index);
        } else if (cmd->type == PROPDEF_SAFE_ITEM) {
            const SafeObjectRecord *safe_item = (const SafeObjectRecord *)cmd;
            traceStagePadDumpWriteLine(
                "{\"scope\":\"setup\",\"kind\":\"safe_item\",\"index\":%d,"
                "\"item_obj\":%d,\"safe_obj\":%d,\"door_obj\":%d}",
                setup_index,
                safe_item->item != NULL ? safe_item->item->obj : -1,
                safe_item->safe != NULL ? safe_item->safe->obj : -1,
                safe_item->door != NULL ? safe_item->door->obj : -1);
        } else if (cmd->type == PROPDEF_LOCK_DOOR) {
            const LockDoorRecord *lock = (const LockDoorRecord *)cmd;
            traceStagePadDumpWriteLine(
                "{\"scope\":\"setup\",\"kind\":\"lock_door\",\"index\":%d,"
                "\"door_obj\":%d,\"lock_obj\":%d}",
                setup_index,
                lock->door != NULL ? lock->door->obj : -1,
                lock->lock != NULL ? lock->lock->obj : -1);
        } else if (cmd->type == PROPDEF_SWITCH) {
            const SwitchRecord *sw = (const SwitchRecord *)cmd;
            const ObjectRecord *switch_obj = (sw->first != NULL) ? sw->first->obj : NULL;
            const ObjectRecord *target_obj = (sw->second != NULL) ? sw->second->obj : NULL;
            traceStagePadDumpWriteLine(
                "{\"scope\":\"setup\",\"kind\":\"switch\",\"index\":%d,"
                "\"switch_obj\":%d,\"target_obj\":%d}",
                setup_index,
                switch_obj != NULL ? switch_obj->obj : -1,
                target_obj != NULL ? target_obj->obj : -1);
        } else if (cmd->type >= PROPDEF_OBJECTIVE_START && cmd->type <= PROPDEF_OBJECTIVE_COPY_ITEM) {
            traceStageDumpObjectiveRecord(cmd, setup_index, current_objective);
            if (cmd->type == PROPDEF_OBJECTIVE_START) {
                current_objective = (int)traceReadU32Field(cmd, 4);
            } else if (cmd->type == PROPDEF_OBJECTIVE_END) {
                current_objective = -1;
            }
        }

        if (cmd->type == PROPDEF_ARMOUR) {
            const BodyArmourRecord *armour = (const BodyArmourRecord *)cmd;
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            int room = -1;
            int has_pos = traceMissionGateResolveObjectPos((const ObjectRecord *)armour, &x, &y, &z, &room);

            traceStagePadDumpWriteLine(
                "{\"scope\":\"setup\",\"kind\":\"armour\",\"obj\":%d,\"pad\":%d,"
                "\"room\":%d,\"has_pos\":%d,\"amount\":%.4f,\"pos\":[%.2f,%.2f,%.2f]}",
                armour->obj,
                armour->pad,
                room,
                has_pos,
                armour->amount,
                x,
                y,
                z);
        }
    }
}

static void traceStagePadDumpOnce(void) {
    if (!traceStagePadDumpEnabled() || s_stagePadDumpDone || s_stagePadDumpFile == NULL) {
        return;
    }

    if (s_traceFrame < traceStagePadDumpFrame()) {
        return;
    }

    s_stagePadDumpDone = 1;
    traceStagePadDumpSetup();
    fflush(s_stagePadDumpFile);
}

static void traceStageChrDumpSetup(void) {
    int i;

    traceStageChrDumpWriteLine(
        "{\"scope\":\"meta\",\"kind\":\"stage\",\"level\":%d,\"difficulty\":%d,"
        "\"difficulty_mask\":\"0x%08X\",\"frame\":%d}",
        bossGetStageNum(),
        lvlGetSelectedDifficulty(),
        1U << (lvlGetSelectedDifficulty() + 4),
        s_traceFrame);

    if (g_ChrSlots == NULL || g_NumChrSlots <= 0) {
        return;
    }

    for (i = 0; i < g_NumChrSlots; i++) {
        ChrRecord *chr = &g_ChrSlots[i];
        PropRecord *prop = chr != NULL ? chr->prop : NULL;
        bool is_global_ai_list = false;
        int ai_list = -1;
        int ai_cmd = -1;
        int ai_arg0 = -1;
        int ai_arg1 = -1;
        int ai_arg2 = -1;
        int ai_arg3 = -1;
        int ai_arg4 = -1;
        int ai_arg5 = -1;

        if (chr == NULL || chr->chrnum < 0 || chr->chrnum >= 1000) {
            continue;
        }

        ai_list = chraiGetAIListID(chr->ailist, &is_global_ai_list);
        if (chr->ailist != NULL) {
            ai_cmd = chr->ailist[chr->aioffset].cmd;
            ai_arg0 = chr->ailist[chr->aioffset].val[0];
            ai_arg1 = chr->ailist[chr->aioffset].val[1];
            ai_arg2 = chr->ailist[chr->aioffset].val[2];
            ai_arg3 = chr->ailist[chr->aioffset].val[3];
            ai_arg4 = chr->ailist[chr->aioffset].val[4];
            ai_arg5 = chr->ailist[chr->aioffset].val[5];
        }

        traceStageChrDumpWriteLine(
            "{\"scope\":\"stage\",\"kind\":\"chr\",\"slot\":%d,\"chrnum\":%d,"
            "\"hidden\":%d,\"alive\":%d,\"action\":%d,\"alert\":%d,\"sleep\":%d,"
            "\"damage\":%.4f,\"maxdamage\":%.4f,\"padpreset1\":%d,"
            "\"ai\":{\"list\":%d,\"global\":%d,\"offset\":%d,\"return\":%d,\"cmd\":%d,"
            "\"arg0\":%d,\"arg1\":%d,\"arg2\":%d,\"arg3\":%d,\"arg4\":%d,\"arg5\":%d},"
            "\"prop\":{\"present\":%d,\"room\":%d,\"x\":%.2f,\"y\":%.2f,\"z\":%.2f}}",
            i,
            chr->chrnum,
            chr->hidden ? 1 : 0,
            (chr->damage < chr->maxdamage) ? 1 : 0,
            chr->actiontype,
            chr->alertness,
            chr->sleep,
            chr->damage,
            chr->maxdamage,
            chr->padpreset1,
            ai_list,
            is_global_ai_list ? 1 : 0,
            chr->aioffset,
            chr->aireturnlist,
            ai_cmd,
            ai_arg0,
            ai_arg1,
            ai_arg2,
            ai_arg3,
            ai_arg4,
            ai_arg5,
            prop != NULL ? 1 : 0,
            (prop != NULL && prop->stan != NULL) ? prop->stan->room : -1,
            prop != NULL ? prop->pos.x : 0.0f,
            prop != NULL ? prop->pos.y : 0.0f,
            prop != NULL ? prop->pos.z : 0.0f);
    }

    for (i = 0; i < g_ActiveChrsCount; i++) {
        ChrRecord *chr = &g_ActiveChrs[i];
        bool is_global_ai_list = false;
        int ai_list = -1;
        int ai_cmd = -1;
        int ai_arg0 = -1;
        int ai_arg1 = -1;
        int ai_arg2 = -1;
        int ai_arg3 = -1;
        int ai_arg4 = -1;
        int ai_arg5 = -1;

        if (chr == NULL) {
            continue;
        }

        ai_list = chraiGetAIListID(chr->ailist, &is_global_ai_list);
        if (chr->ailist != NULL) {
            ai_cmd = chr->ailist[chr->aioffset].cmd;
            ai_arg0 = chr->ailist[chr->aioffset].val[0];
            ai_arg1 = chr->ailist[chr->aioffset].val[1];
            ai_arg2 = chr->ailist[chr->aioffset].val[2];
            ai_arg3 = chr->ailist[chr->aioffset].val[3];
            ai_arg4 = chr->ailist[chr->aioffset].val[4];
            ai_arg5 = chr->ailist[chr->aioffset].val[5];
        }

        traceStageChrDumpWriteLine(
            "{\"scope\":\"stage\",\"kind\":\"active_chr\",\"slot\":%d,\"chrnum\":%d,"
            "\"hidden\":%d,\"alive\":%d,\"action\":%d,\"alert\":%d,\"sleep\":%d,"
            "\"damage\":%.4f,\"maxdamage\":%.4f,\"padpreset1\":%d,"
            "\"ai\":{\"list\":%d,\"global\":%d,\"offset\":%d,\"return\":%d,\"cmd\":%d,"
            "\"arg0\":%d,\"arg1\":%d,\"arg2\":%d,\"arg3\":%d,\"arg4\":%d,\"arg5\":%d},"
            "\"prop\":{\"present\":0,\"room\":-1,\"x\":0.00,\"y\":0.00,\"z\":0.00}}",
            i,
            chr->chrnum,
            chr->hidden ? 1 : 0,
            (chr->damage < chr->maxdamage) ? 1 : 0,
            chr->actiontype,
            chr->alertness,
            chr->sleep,
            chr->damage,
            chr->maxdamage,
            chr->padpreset1,
            ai_list,
            is_global_ai_list ? 1 : 0,
            chr->aioffset,
            chr->aireturnlist,
            ai_cmd,
            ai_arg0,
            ai_arg1,
            ai_arg2,
            ai_arg3,
            ai_arg4,
            ai_arg5);
    }
}

static void traceStageChrDumpOnce(void) {
    if (!traceStageChrDumpEnabled() || s_stageChrDumpDone || s_stageChrDumpFile == NULL) {
        return;
    }

    if (s_traceFrame < traceStageChrDumpFrame()) {
        return;
    }

    s_stageChrDumpDone = 1;
    traceStageChrDumpSetup();
    fflush(s_stageChrDumpFile);
}

static void traceMissionGateDescribeOwner(const PropRecord *prop,
                                          int *out_prop_type,
                                          int *out_attached,
                                          int *out_parent_type,
                                          int *out_parent_chrnum,
                                          int *out_parent_obj,
                                          int *out_parent_room,
                                          float *out_parent_x,
                                          float *out_parent_y,
                                          float *out_parent_z) {
    const PropRecord *parent = NULL;

    if (out_prop_type) *out_prop_type = -1;
    if (out_attached) *out_attached = 0;
    if (out_parent_type) *out_parent_type = -1;
    if (out_parent_chrnum) *out_parent_chrnum = -1;
    if (out_parent_obj) *out_parent_obj = -1;
    if (out_parent_room) *out_parent_room = -1;
    if (out_parent_x) *out_parent_x = 0.0f;
    if (out_parent_y) *out_parent_y = 0.0f;
    if (out_parent_z) *out_parent_z = 0.0f;

    if (prop == NULL) {
        return;
    }

    if (out_prop_type) *out_prop_type = prop->type;
    parent = prop->parent;
    if (parent == NULL) {
        return;
    }

    if (out_attached) *out_attached = 1;
    if (out_parent_type) *out_parent_type = parent->type;
    if (out_parent_room) *out_parent_room = parent->stan != NULL ? parent->stan->room : -1;
    if (out_parent_x) *out_parent_x = parent->pos.x;
    if (out_parent_y) *out_parent_y = parent->pos.y;
    if (out_parent_z) *out_parent_z = parent->pos.z;

    if (parent->type == PROP_TYPE_CHR && parent->chr != NULL) {
        if (out_parent_chrnum) *out_parent_chrnum = parent->chr->chrnum;
    } else if ((parent->type == PROP_TYPE_OBJ || parent->type == PROP_TYPE_DOOR || parent->type == PROP_TYPE_WEAPON)
               && parent->obj != NULL) {
        if (out_parent_obj) *out_parent_obj = parent->obj->obj;
    }
}

static void traceMissionGateDumpSetup(void) {
    PropDefHeaderRecord *cmd;

    if (g_CurrentSetup.propDefs == NULL) {
        return;
    }

    for (cmd = g_CurrentSetup.propDefs; cmd->type != PROPDEF_END; cmd += sizepropdef(cmd)) {
        if (cmd->type == PROPDEF_KEY) {
            const KeyRecord *key = (const KeyRecord *)cmd;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            int room = -1;
            int has_pos = traceMissionGateResolveObjectPos((const ObjectRecord *)key, &x, &y, &z, &room);
            int prop_type = -1;
            int attached = 0;
            int parent_type = -1;
            int parent_chrnum = -1;
            int parent_obj = -1;
            int parent_room = -1;
            float parent_x = 0.0f;
            float parent_y = 0.0f;
            float parent_z = 0.0f;

            traceMissionGateDescribeOwner(key->prop,
                                          &prop_type,
                                          &attached,
                                          &parent_type,
                                          &parent_chrnum,
                                          &parent_obj,
                                          &parent_room,
                                          &parent_x,
                                          &parent_y,
                                          &parent_z);

            traceMissionGateDumpWriteLine(
                "{\"scope\":\"setup\",\"kind\":\"key\",\"obj\":%d,\"pad\":%d,"
                "\"keyflags\":\"0x%08X\",\"flags\":\"0x%08X\",\"flags2\":\"0x%08X\","
                "\"has_prop\":%d,\"prop_type\":%d,\"attached\":%d,"
                "\"parent_type\":%d,\"parent_chrnum\":%d,\"parent_obj\":%d,"
                "\"parent_room\":%d,\"parent_pos\":[%.2f,%.2f,%.2f],"
                "\"has_pos\":%d,\"room\":%d,\"pos\":[%.2f,%.2f,%.2f]}",
                key->obj,
                key->pad,
                key->keyflags,
                key->flags,
                key->flags2,
                key->prop != NULL ? 1 : 0,
                prop_type,
                attached,
                parent_type,
                parent_chrnum,
                parent_obj,
                parent_room,
                parent_x, parent_y, parent_z,
                has_pos,
                room,
                x, y, z);
        } else if (cmd->type == PROPDEF_DOOR) {
            const DoorRecord *door = (const DoorRecord *)cmd;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            int room = -1;
            int has_pos = traceMissionGateResolveObjectPos((const ObjectRecord *)door, &x, &y, &z, &room);

            if (door->keyflags == 0) {
                continue;
            }

            traceMissionGateDumpWriteLine(
                "{\"scope\":\"setup\",\"kind\":\"door\",\"obj\":%d,\"pad\":%d,"
                "\"keyflags\":\"0x%08X\",\"door_flags\":\"0x%04X\",\"door_type\":\"0x%04X\","
                "\"flags\":\"0x%08X\",\"flags2\":\"0x%08X\",\"has_prop\":%d,\"has_pos\":%d,"
                "\"room\":%d,\"pos\":[%.2f,%.2f,%.2f]}",
                door->obj,
                door->pad,
                door->keyflags,
                (unsigned int)door->doorFlags,
                (unsigned int)door->doorType,
                door->flags,
                door->flags2,
                door->prop != NULL ? 1 : 0,
                has_pos,
                room,
                x, y, z);
        }
    }
}

static void traceMissionGateDumpActiveProps(void) {
    const PropRecord *prop;
    int guard = 0;

    for (prop = ptr_obj_pos_list_first_entry; prop != NULL && guard < 4096; prop = prop->next, guard++) {
        if (prop->type == PROP_TYPE_DOOR && prop->door != NULL && prop->door->keyflags != 0) {
            const DoorRecord *door = prop->door;
            traceMissionGateDumpWriteLine(
                "{\"scope\":\"active\",\"kind\":\"door\",\"obj\":%d,\"pad\":%d,"
                "\"keyflags\":\"0x%08X\",\"runtime\":\"0x%08X\",\"state\":%d,"
                "\"open_pos\":%.3f,\"room\":%d,\"pos\":[%.2f,%.2f,%.2f]}",
                door->obj,
                door->pad,
                door->keyflags,
                door->runtime_bitflags,
                (int)door->openstate,
                door->openPosition,
                prop->stan != NULL ? prop->stan->room : -1,
                prop->pos.x, prop->pos.y, prop->pos.z);
        } else if (prop->type == PROP_TYPE_OBJ && prop->obj != NULL && prop->obj->type == PROPDEF_KEY) {
            const KeyRecord *key = (const KeyRecord *)prop->obj;
            traceMissionGateDumpWriteLine(
                "{\"scope\":\"active\",\"kind\":\"key\",\"obj\":%d,\"pad\":%d,"
                "\"keyflags\":\"0x%08X\",\"runtime\":\"0x%08X\",\"state\":%d,"
                "\"room\":%d,\"pos\":[%.2f,%.2f,%.2f]}",
                key->obj,
                key->pad,
                key->keyflags,
                key->runtime_bitflags,
                key->state,
                prop->stan != NULL ? prop->stan->room : -1,
                prop->pos.x, prop->pos.y, prop->pos.z);
        }
    }
}

static void traceMissionGateDumpOnce(void) {
    if (!traceMissionGateDumpEnabled() || s_missionGateDumpDone || s_missionGateDumpFile == NULL) {
        return;
    }

    if (s_traceFrame < traceMissionGateDumpFrame()) {
        return;
    }

    s_missionGateDumpDone = 1;
    traceMissionGateDumpWriteLine(
        "{\"scope\":\"meta\",\"kind\":\"stage\",\"level\":%d,\"difficulty\":%d,"
        "\"difficulty_mask\":\"0x%08X\",\"frame\":%d}",
        bossGetStageNum(),
        lvlGetSelectedDifficulty(),
        1U << (lvlGetSelectedDifficulty() + 4),
        s_traceFrame);
    traceMissionGateDumpSetup();
    traceMissionGateDumpActiveProps();
    fflush(s_missionGateDumpFile);
}

static void traceInit(void) {
    if (g_traceStatePath != NULL) {
        s_traceFd = traceStateFileOpen(g_traceStatePath);
        if (s_traceFd < 0) {
            fprintf(stderr, "[TRACE] Failed to open %s for writing\n", g_traceStatePath);
        } else {
            s_traceFileOpened = 1;
            fprintf(stderr, "[TRACE] State trace -> %s\n", g_traceStatePath);
        }
    } else if (traceGuardOracleEnabled()) {
        const char *guard_oracle_path = traceGuardOraclePath();

        s_traceFd = traceStateFileOpen(guard_oracle_path);
        if (s_traceFd < 0) {
            fprintf(stderr, "[TRACE] Failed to open %s for guard oracle trace\n", guard_oracle_path);
        } else {
            s_traceFileOpened = 1;
            fprintf(stderr, "[TRACE] Guard oracle trace -> %s\n", guard_oracle_path);
        }
    }

    if (traceMissionGateDumpEnabled()) {
        s_missionGateDumpFile = fopen(traceMissionGateDumpPath(), "w");
        if (s_missionGateDumpFile == NULL) {
            fprintf(stderr, "[TRACE] Failed to open %s for mission gate dump\n", traceMissionGateDumpPath());
        } else {
            s_traceFileOpened = 1;
            fprintf(stderr, "[TRACE] Mission gates -> %s\n", traceMissionGateDumpPath());
        }
    }

    if (traceStagePadDumpEnabled()) {
        s_stagePadDumpFile = fopen(traceStagePadDumpPath(), "w");
        if (s_stagePadDumpFile == NULL) {
            fprintf(stderr, "[TRACE] Failed to open %s for stage pad dump\n", traceStagePadDumpPath());
        } else {
            s_traceFileOpened = 1;
            fprintf(stderr, "[TRACE] Stage pads -> %s\n", traceStagePadDumpPath());
        }
    }

    if (traceStageChrDumpEnabled()) {
        s_stageChrDumpFile = fopen(traceStageChrDumpPath(), "w");
        if (s_stageChrDumpFile == NULL) {
            fprintf(stderr, "[TRACE] Failed to open %s for stage chr dump\n", traceStageChrDumpPath());
        } else {
            s_traceFileOpened = 1;
            fprintf(stderr, "[TRACE] Stage chrs -> %s\n", traceStageChrDumpPath());
        }
    }
}

static void traceClose(void) {
    traceStateFileClose();
    if (s_missionGateDumpFile) { fclose(s_missionGateDumpFile); s_missionGateDumpFile = NULL; }
    if (s_stagePadDumpFile) { fclose(s_stagePadDumpFile); s_stagePadDumpFile = NULL; }
    if (s_stageChrDumpFile) { fclose(s_stageChrDumpFile); s_stageChrDumpFile = NULL; }
}

static int isFinite(float v) {
    return v == v && v >= -FLT_MAX && v <= FLT_MAX;
}

static int traceCameraMatricesEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        enabled = (getenv("GE007_TRACE_CAMERA_MTX") != NULL) ? 1 : 0;
    }
    return enabled;
}

static int traceCameraMatricesAfterFrame(void) {
    static int after = -1;
    if (after < 0) {
        const char *value = getenv("GE007_TRACE_CAMERA_MTX_AFTER_FRAME");
        after = value != NULL ? atoi(value) : 0;
    }
    return after;
}

static void traceCameraMatrixRow(const char *label, const Mtxf *mtx, int row) {
    if (mtx == NULL) {
        fprintf(stderr, "[CAM-MTX] %s=null\n", label);
        return;
    }

    fprintf(stderr, "[CAM-MTX] %s[%d]=(%.5f,%.5f,%.5f,%.5f)\n",
            label, row,
            mtx->m[row][0], mtx->m[row][1], mtx->m[row][2], mtx->m[row][3]);
}

static void traceCameraMatricesIfRequested(int cam_mode, int player_unknown, int room) {
    static int count = 0;
    Mtxf *view;
    Mtxf *clip;
    Mtxf *proj;
    Mtxf *field10e0;

    if (!traceCameraMatricesEnabled() ||
        s_traceFrame < traceCameraMatricesAfterFrame() ||
        count >= 16 ||
        g_CurrentPlayer == NULL) {
        return;
    }

    view = camGetWorldToScreenMtxf();
    clip = currentPlayerGetMatrix10D4();
    proj = currentPlayerGetProjectionMatrixF();
    field10e0 = bondviewField10E0IsFloat()
        ? (Mtxf *)(uintptr_t)get_BONDdata_field_10E0()
        : NULL;

    fprintf(stderr,
            "[CAM-MTX] frame=%d render_frame=%d cam=%d p_unk=%d room=%d "
            "field10e0_float=%d field10e0=%p proj=%p view=%p clip=%p\n",
            s_traceFrame, g_frame_count_diag, cam_mode, player_unknown, room,
            bondviewField10E0IsFloat() ? 1 : 0,
            (void *)(uintptr_t)get_BONDdata_field_10E0(),
            (void *)proj, (void *)view, (void *)clip);
    traceCameraMatrixRow("proj", proj, 0);
    traceCameraMatrixRow("proj", proj, 1);
    traceCameraMatrixRow("proj", proj, 2);
    traceCameraMatrixRow("proj", proj, 3);
    traceCameraMatrixRow("view", view, 0);
    traceCameraMatrixRow("view", view, 1);
    traceCameraMatrixRow("view", view, 2);
    traceCameraMatrixRow("view", view, 3);
    traceCameraMatrixRow("clip", clip, 0);
    traceCameraMatrixRow("clip", clip, 1);
    traceCameraMatrixRow("clip", clip, 2);
    traceCameraMatrixRow("clip", clip, 3);
    traceCameraMatrixRow("field10e0", field10e0, 0);
    traceCameraMatrixRow("field10e0", field10e0, 1);
    traceCameraMatrixRow("field10e0", field10e0, 2);
    traceCameraMatrixRow("field10e0", field10e0, 3);
    count++;
}

static int traceJsonValueBoundary(char ch) {
    return ch == '\0' || ch == ',' || ch == ']' || ch == '}' ||
           ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t';
}

static int traceJsonValuePrefix(char ch) {
    return ch == ':' || ch == ',' || ch == '[' ||
           ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static int traceJsonMatchesNan(const char *text) {
    return (text[0] == 'n' || text[0] == 'N') &&
           (text[1] == 'a' || text[1] == 'A') &&
           (text[2] == 'n' || text[2] == 'N');
}

static int traceJsonMatchesInf(const char *text) {
    return (text[0] == 'i' || text[0] == 'I') &&
           (text[1] == 'n' || text[1] == 'N') &&
           (text[2] == 'f' || text[2] == 'F');
}

static void traceSanitizeJsonNonFiniteNumbers(char *json) {
    int in_string = 0;
    int escaped = 0;

    for (char *cursor = json; *cursor; cursor++) {
        if (in_string) {
            if (escaped) {
                escaped = 0;
            } else if (*cursor == '\\') {
                escaped = 1;
            } else if (*cursor == '"') {
                in_string = 0;
            }
            continue;
        }

        if (*cursor == '"') {
            in_string = 1;
            continue;
        }

        char previous = cursor == json ? ':' : cursor[-1];
        if (!traceJsonValuePrefix(previous)) {
            continue;
        }

        if (*cursor == '-' &&
            (traceJsonMatchesNan(cursor + 1) || traceJsonMatchesInf(cursor + 1)) &&
            traceJsonValueBoundary(cursor[4])) {
            memcpy(cursor, "0.00", 4);
            cursor += 3;
            continue;
        }

        if ((traceJsonMatchesNan(cursor) || traceJsonMatchesInf(cursor)) &&
            traceJsonValueBoundary(cursor[3])) {
            memcpy(cursor, "0.0", 3);
            cursor += 2;
        }
    }
}

static void traceCombatVisibility(ChrRecord *chr,
                                  int *out_line_clear,
                                  int *out_same_stan,
                                  int *out_could_see_bond,
                                  int *out_line_clear_world,
                                  int *out_line_clear_solid) {
    PropRecord *myprop;
    PropRecord *bondprop;
    StandTile *stan_full;
    StandTile *stan_world;
    StandTile *stan_solid;
    float myheight;

    if (out_line_clear) *out_line_clear = 0;
    if (out_same_stan) *out_same_stan = 0;
    if (out_could_see_bond) *out_could_see_bond = 0;
    if (out_line_clear_world) *out_line_clear_world = 0;
    if (out_line_clear_solid) *out_line_clear_solid = 0;

    if (chr == NULL || chr->prop == NULL || chr->prop->stan == NULL) {
        return;
    }

    if (!bondviewGetVisibleToGuardsFlag()) {
        return;
    }

    bondprop = get_curplayer_positiondata();
    if (bondprop == NULL || bondprop->stan == NULL || g_CurrentPlayer == NULL || g_CurrentPlayer->prop == NULL) {
        return;
    }

    myprop = chr->prop;
    myheight = chr->chrheight - 20.0f;
    stan_full = myprop->stan;
    stan_world = myprop->stan;
    stan_solid = myprop->stan;

    chrSetMoving(chr, FALSE);
    bondviewUpdateGuardTankFlagsRelated(g_CurrentPlayer->prop, 0);

    if (out_line_clear) {
        *out_line_clear = stanTestLineUnobstructed(&stan_full,
                                                   myprop->pos.x, myprop->pos.z,
                                                   bondprop->pos.x, bondprop->pos.z,
                                                   CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE,
                                                   myheight, myheight, 0.0f, 1.0f) ? 1 : 0;
    } else {
        stanTestLineUnobstructed(&stan_full,
                                 myprop->pos.x, myprop->pos.z,
                                 bondprop->pos.x, bondprop->pos.z,
                                 CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE,
                                 myheight, myheight, 0.0f, 1.0f);
    }

    if (out_line_clear_world) {
        *out_line_clear_world = stanTestLineUnobstructed(&stan_world,
                                                         myprop->pos.x, myprop->pos.z,
                                                         bondprop->pos.x, bondprop->pos.z,
                                                         CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE,
                                                         myheight, myheight, 0.0f, 1.0f) ? 1 : 0;
    }

    if (out_line_clear_solid) {
        *out_line_clear_solid = stanTestLineUnobstructed(&stan_solid,
                                                         myprop->pos.x, myprop->pos.z,
                                                         bondprop->pos.x, bondprop->pos.z,
                                                         CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER,
                                                         myheight, myheight, 0.0f, 1.0f) ? 1 : 0;
    }

    if (out_same_stan) {
        *out_same_stan = (stan_full == bondprop->stan) ? 1 : 0;
    }

    if (out_could_see_bond) {
        *out_could_see_bond = ((out_line_clear ? *out_line_clear : 0)
                               && (out_same_stan ? *out_same_stan : 0)) ? 1 : 0;
    }

    bondviewUpdateGuardTankFlagsRelated(g_CurrentPlayer->prop, 1);
    chrSetMoving(chr, TRUE);
}

static void traceCombatTarget(const PropRecord *prop,
                              float player_x, float player_y, float player_z,
                              int *out_type, int *out_chrnum, int *out_hidden, int *out_alive,
                              int *out_actiontype, int *out_alertness, int *out_firecount,
                              int *out_hidden_bits, int *out_sleep,
                              int *out_ai_list, int *out_ai_global, int *out_ai_offset, int *out_ai_return,
                              int *out_ai_cmd, int *out_ai_arg0,
                              int *out_seen_age, int *out_heard_age, int *out_seen_recent, int *out_heard_recent,
                              int *out_line_clear, int *out_same_stan, int *out_could_see_bond,
                              float *out_damage, float *out_maxdamage, float *out_distance,
                              float *out_pos_x, float *out_pos_y, float *out_pos_z,
                              float *out_delta_x, float *out_delta_y, float *out_delta_z) {
    float dx, dy, dz;
    bool is_global_ai_list = FALSE;

    *out_type = -1;
    *out_chrnum = -1;
    *out_hidden = 0;
    *out_alive = 0;
    *out_actiontype = -1;
    *out_alertness = -1;
    *out_firecount = 0;
    *out_hidden_bits = 0;
    *out_sleep = 0;
    *out_ai_list = -1;
    *out_ai_global = 0;
    *out_ai_offset = -1;
    *out_ai_return = -1;
    *out_ai_cmd = -1;
    *out_ai_arg0 = -1;
    *out_seen_age = -1;
    *out_heard_age = -1;
    *out_seen_recent = 0;
    *out_heard_recent = 0;
    *out_line_clear = 0;
    *out_same_stan = 0;
    *out_could_see_bond = 0;
    *out_damage = 0.0f;
    *out_maxdamage = 0.0f;
    *out_distance = 0.0f;
    *out_pos_x = 0.0f;
    *out_pos_y = 0.0f;
    *out_pos_z = 0.0f;
    *out_delta_x = 0.0f;
    *out_delta_y = 0.0f;
    *out_delta_z = 0.0f;

    if (!prop) {
        return;
    }

    *out_type = prop->type;
    *out_pos_x = prop->pos.x;
    *out_pos_y = prop->pos.y;
    *out_pos_z = prop->pos.z;

    dx = prop->pos.x - player_x;
    dy = prop->pos.y - player_y;
    dz = prop->pos.z - player_z;
    *out_delta_x = dx;
    *out_delta_y = dy;
    *out_delta_z = dz;
    *out_distance = sqrtf((dx * dx) + (dy * dy) + (dz * dz));

    if (prop->type == PROP_TYPE_CHR && prop->chr) {
        *out_chrnum = prop->chr->chrnum;
        *out_hidden = prop->chr->hidden ? 1 : 0;
        *out_hidden_bits = prop->chr->hidden;
        *out_damage = prop->chr->damage;
        *out_maxdamage = prop->chr->maxdamage;
        *out_alive = (prop->chr->damage < prop->chr->maxdamage) ? 1 : 0;
        *out_actiontype = prop->chr->actiontype;
        *out_alertness = prop->chr->alertness;
        *out_firecount = prop->chr->firecount[0] + prop->chr->firecount[1];
        *out_sleep = prop->chr->sleep;
        *out_ai_offset = prop->chr->aioffset;
        *out_ai_return = prop->chr->aireturnlist;
        *out_ai_list = chraiGetAIListID(prop->chr->ailist, &is_global_ai_list);
        *out_ai_global = is_global_ai_list ? 1 : 0;
        traceCombatVisibility(prop->chr, out_line_clear, out_same_stan, out_could_see_bond, NULL, NULL);
        if (prop->chr->ailist) {
            *out_ai_cmd = prop->chr->ailist[prop->chr->aioffset].cmd;
            *out_ai_arg0 = prop->chr->ailist[prop->chr->aioffset].val[0];
        }

        if (prop->chr->lastseetarget60 > 0) {
            *out_seen_age = g_GlobalTimer - prop->chr->lastseetarget60;
            *out_seen_recent = (*out_seen_age < CHRLV_10_SEC_TIMER) ? 1 : 0;
        }

        if (prop->chr->lastheartarget60 > 0) {
            *out_heard_age = g_GlobalTimer - prop->chr->lastheartarget60;
            *out_heard_recent = (*out_heard_age < CHRLV_10_SEC_TIMER) ? 1 : 0;
        }
    }
}

static const PropRecord *traceFindNearestCombatGuard(float player_x, float player_y, float player_z) {
    int i;
    float best_dist_sq = 0.0f;
    const PropRecord *best_prop = NULL;

    if (g_ChrSlots == NULL || g_NumChrSlots <= 0) {
        return NULL;
    }

    for (i = 0; i < g_NumChrSlots; i++) {
        ChrRecord *chr = &g_ChrSlots[i];
        PropRecord *prop;
        float dx, dy, dz;
        float dist_sq;

        if (chr == NULL || chr->model == NULL || chr->prop == NULL) {
            continue;
        }

        prop = chr->prop;

        if (prop == NULL || prop->type != PROP_TYPE_CHR) {
            continue;
        }

        if (chr->chrnum < 0 || chr->chrnum >= 1000) {
            continue;
        }

        if (chr->damage >= chr->maxdamage) {
            continue;
        }

        dx = prop->pos.x - player_x;
        dy = prop->pos.y - player_y;
        dz = prop->pos.z - player_z;
        dist_sq = (dx * dx) + (dy * dy) + (dz * dz);

        if (best_prop == NULL || dist_sq < best_dist_sq) {
            best_prop = prop;
            best_dist_sq = dist_sq;
        }
    }

    return best_prop;
}

static void traceCombatChrBlocker(const PropRecord *source_prop,
                                  float dest_x, float dest_z,
                                  int *out_type,
                                  int *out_chrnum,
                                  int *out_self,
                                  float *out_distance) {
    PropRecord *blocker;
    float dx;
    float dy;
    float dz;

    *out_type = -1;
    *out_chrnum = -1;
    *out_self = 0;
    *out_distance = 0.0f;

    if (source_prop == NULL || source_prop->stan == NULL) {
        return;
    }

    blocker = sub_GAME_7F0B1410(source_prop->stan,
                                source_prop->pos.x, source_prop->pos.z,
                                dest_x, dest_z,
                                CDTYPE_CHRS);
    if (blocker == NULL) {
        return;
    }

    *out_type = blocker->type;
    *out_self = (blocker == source_prop) ? 1 : 0;
    if (blocker->type == PROP_TYPE_CHR && blocker->chr != NULL) {
        *out_chrnum = blocker->chr->chrnum;
    }

    dx = blocker->pos.x - source_prop->pos.x;
    dy = blocker->pos.y - source_prop->pos.y;
    dz = blocker->pos.z - source_prop->pos.z;
    *out_distance = sqrtf((dx * dx) + (dy * dy) + (dz * dz));
}

static void traceBuildPropRoomsJson(PropRecord *prop,
                                    char *out,
                                    size_t out_size,
                                    int *out_count,
                                    int *out_first_room,
                                    int *out_stan_room,
                                    int *out_any_rendered,
                                    int *out_first_rendered)
{
    s32 rooms[PROPRECORD_STAN_ROOM_LEN];
    size_t used;
    int i;

    if (out_size == 0) {
        return;
    }

    snprintf(out, out_size, "[]");

    if (out_count) *out_count = 0;
    if (out_first_room) *out_first_room = -1;
    if (out_stan_room) *out_stan_room = -1;
    if (out_any_rendered) *out_any_rendered = 0;
    if (out_first_rendered) *out_first_rendered = 0;

    if (prop == NULL) {
        return;
    }

    if (prop->stan != NULL && out_stan_room != NULL) {
        *out_stan_room = prop->stan->room;
    }

    chraiGetPropRoomIds(prop, rooms);

    used = 0;
    out[used++] = '[';

    for (i = 0; i < PROPRECORD_STAN_ROOM_LEN && rooms[i] >= 0; i++) {
        int rendered = getROOMID_isRendered(rooms[i]) ? 1 : 0;
        int wrote;

        if (i == 0 && out_first_room != NULL) {
            *out_first_room = rooms[i];
            if (out_first_rendered != NULL) {
                *out_first_rendered = rendered;
            }
        }

        if (rendered && out_any_rendered != NULL) {
            *out_any_rendered = 1;
        }

        wrote = snprintf(out + used,
                         out_size - used,
                         "%s%d",
                         i == 0 ? "" : ",",
                         rooms[i]);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            snprintf(out, out_size, "[]");
            return;
        }
        used += (size_t)wrote;
    }

    if (out_count != NULL) {
        *out_count = i;
    }

    if (used + 2 <= out_size) {
        out[used++] = ']';
        out[used] = '\0';
    } else {
        snprintf(out, out_size, "[]");
    }
}

typedef struct TracePropFloorSnapshot {
    int valid;
    int nearest;
    int room;
    float y;
    float delta;
} TracePropFloorSnapshot;

typedef struct TraceActorSample {
    int slot;
    int chrnum;
    int action;
    int alert;
    int sleep;
    int hidden;
    int hidden_bits;
    int alive;
    int onscreen;
    int rendered;
    int room;
    float dist_sq;
    float x;
    float y;
    float z;
} TraceActorSample;

static int tracePropRenderedInRooms(PropRecord *prop, int *out_room)
{
    int saw_room = 0;
    int first_room = -1;
    int i;

    if (out_room != NULL) {
        *out_room = -1;
    }

    if (prop == NULL) {
        return 0;
    }

    for (i = 0; i < PROPRECORD_STAN_ROOM_LEN && prop->rooms[i] != 0xFF; i++) {
        int room = prop->rooms[i];

        if (first_room < 0) {
            first_room = room;
        }

        saw_room = 1;
        if (getROOMID_isRendered(room)) {
            if (out_room != NULL) {
                *out_room = first_room;
            }
            return 1;
        }
    }

    if (first_room < 0 && prop->stan != NULL) {
        first_room = getTileRoom(prop->stan);
    }

    if (out_room != NULL) {
        *out_room = first_room;
    }

    return !saw_room && first_room >= 0 && getROOMID_isRendered(first_room);
}

static void traceInsertActorSample(TraceActorSample *samples,
                                   int *sample_count,
                                   const TraceActorSample *sample)
{
    enum { max_samples = 6 };
    int slot = *sample_count;

    if (slot < max_samples) {
        (*sample_count)++;
    } else if (sample->dist_sq < samples[max_samples - 1].dist_sq) {
        slot = max_samples - 1;
    } else {
        return;
    }

    while (slot > 0 && sample->dist_sq < samples[slot - 1].dist_sq) {
        samples[slot] = samples[slot - 1];
        slot--;
    }

    samples[slot] = *sample;
}

/*
 * Combat / floor-field oracle (FID-0032). Emits the "combat_oracle" namespace:
 *   guards[]      active-chr AI/combat state (schema parity with the ares tracer)
 *   floor{}       the player's current stan tile
 *   combat{}      scalar player combat summary
 *   projectiles[] live thrown/airborne ordnance
 * See docs/fidelity/combat_oracle_fields.md for the field/offset dossier. This is a
 * pure read of existing sim state; it mutates nothing and is not in the sim-state hash.
 */
#define TRACE_COMBAT_ORACLE_MAX_GUARDS 64
#define TRACE_COMBAT_ORACLE_MAX_PROJECTILES 32

static void traceBuildCombatOracleJson(char *out,
                                       size_t out_size,
                                       int has_player,
                                       int shots_fired_total)
{
    extern PropRecord *get_ptr_obj_pos_list_current_entry(void);
    size_t used = 0;
    int wrote;
    int i;
    int emitted;
    int overflow;

    if (out_size == 0) {
        return;
    }

    /* guards[] */
    wrote = snprintf(out + used, out_size - used, "\"combat_oracle\":{\"guards\":[");
    if (wrote < 0 || (size_t)wrote >= out_size - used) {
        snprintf(out, out_size,
                 "\"combat_oracle\":{\"guards\":[],\"guards_overflow\":1,"
                 "\"floor\":{\"stan_id\":-1,\"stan_room\":-1,\"stan_flags\":-1,\"height\":0.00},"
                 "\"combat\":{\"player_health\":0.0000,\"player_armor\":0.0000,"
                 "\"shots_fired_total\":0,\"hits_landed_total\":0,\"rng_seed\":\"0x00000000\"},"
                 "\"projectiles\":[],\"projectiles_overflow\":1},");
        return;
    }
    used += (size_t)wrote;

    emitted = 0;
    overflow = 0;
    if (g_ChrSlots != NULL && g_NumChrSlots > 0 && g_NumChrSlots < 256) {
        for (i = 0; i < g_NumChrSlots; i++) {
            ChrRecord *chr = &g_ChrSlots[i];
            PropRecord *prop;
            u64 anim_hash = 0;
            int room = -1;
            int flags_onscreen = 0;
            int target_visible = 0;
            float health;

            if (chr == NULL || chr->model == NULL || chr->chrnum < 0 || chr->chrnum >= 1000) {
                continue;
            }
            prop = chr->prop;
            if (prop == NULL || prop->type != PROP_TYPE_CHR) {
                continue;
            }
            if (emitted >= TRACE_COMBAT_ORACLE_MAX_GUARDS) {
                overflow = 1;
                break;
            }

            if (chr->model->anim != NULL) {
                anim_hash = traceModelAnimationHeaderHash(chr->model->anim);
            }
            if (prop->stan != NULL) {
                room = prop->stan->room;
            }
            flags_onscreen = (prop->flags & PROPFLAG_ONSCREEN) != 0 ? 1 : 0;
            if (chr->lastseetarget60 > 0 &&
                (g_GlobalTimer - chr->lastseetarget60) < CHRLV_10_SEC_TIMER) {
                target_visible = 1;
            }
            health = chr->maxdamage - chr->damage;

            wrote = snprintf(out + used, out_size - used,
                             "%s{\"chrnum\":%d,\"pos\":[%.2f,%.2f,%.2f],"
                             "\"actiontype\":%d,\"aimode\":%d,\"health\":%.4f,"
                             "\"shotbondsum\":%.4f,\"flags_onscreen\":%d,"
                             "\"target_visible\":%d,\"anim_hash\":\"0x%016llX\",\"room\":%d}",
                             emitted == 0 ? "" : ",",
                             chr->chrnum,
                             prop->pos.x, prop->pos.y, prop->pos.z,
                             chr->actiontype,
                             chr->alertness,
                             health,
                             chr->shotbondsum,
                             flags_onscreen,
                             target_visible,
                             (unsigned long long)anim_hash,
                             room);
            if (wrote < 0 || (size_t)wrote >= out_size - used) {
                overflow = 1;
                break;
            }
            used += (size_t)wrote;
            emitted++;
        }
    }

    wrote = snprintf(out + used, out_size - used, "],\"guards_overflow\":%d,", overflow);
    if (wrote < 0 || (size_t)wrote >= out_size - used) {
        strncpy(out, "\"combat_oracle\":{\"guards\":[],\"guards_overflow\":1,"
                     "\"floor\":{\"stan_id\":-1,\"stan_room\":-1,\"stan_flags\":-1,\"height\":0.00},"
                     "\"combat\":{\"player_health\":0.0000,\"player_armor\":0.0000,"
                     "\"shots_fired_total\":0,\"hits_landed_total\":0,\"rng_seed\":\"0x00000000\"},"
                     "\"projectiles\":[],\"projectiles_overflow\":1},",
                out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    used += (size_t)wrote;

    /* floor{} — the player's current stan tile */
    {
        int stan_id = -1;
        int stan_room = -1;
        int stan_flags = -1;
        float height = 0.0f;
        if (has_player && g_CurrentPlayer != NULL) {
            StandTile *tile = g_CurrentPlayer->field_488.current_tile_ptr;
            if (tile != NULL) {
                stan_id = (int)(tile->id & 0x00FFFFFF);
                stan_room = (int)tile->room;
                stan_flags = (int)tile->mid.half;
            }
            height = g_CurrentPlayer->stanHeight;
        }
        wrote = snprintf(out + used, out_size - used,
                         "\"floor\":{\"stan_id\":%d,\"stan_room\":%d,\"stan_flags\":%d,\"height\":%.2f},",
                         stan_id, stan_room, stan_flags, height);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            strncpy(out + used, "\"floor\":{},\"combat\":{},\"projectiles\":[]},",
                    out_size - used - 1);
            out[out_size - 1] = '\0';
            return;
        }
        used += (size_t)wrote;
    }

    /* combat{} — scalar player combat summary */
    {
        float player_health = 0.0f;
        float player_armor = 0.0f;
        unsigned int rng_seed_low = (unsigned int)(g_randomSeed & 0xFFFFFFFFULL);
        if (has_player && g_CurrentPlayer != NULL) {
            player_health = g_CurrentPlayer->bondhealth;
            player_armor = g_CurrentPlayer->bondarmour;
        }
        wrote = snprintf(out + used, out_size - used,
                         "\"combat\":{\"player_health\":%.4f,\"player_armor\":%.4f,"
                         "\"shots_fired_total\":%d,\"hits_landed_total\":%d,"
                         "\"rng_seed\":\"0x%08X\"},",
                         player_health, player_armor,
                         shots_fired_total, s_combatOracleHitsTotal,
                         rng_seed_low);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            strncpy(out + used, "\"combat\":{},\"projectiles\":[]},", out_size - used - 1);
            out[out_size - 1] = '\0';
            return;
        }
        used += (size_t)wrote;
    }

    /* projectiles[] — live thrown/airborne ordnance (mines/grenades/rockets/explosions) */
    wrote = snprintf(out + used, out_size - used, "\"projectiles\":[");
    if (wrote < 0 || (size_t)wrote >= out_size - used) {
        strncpy(out + used, "\"projectiles\":[]},", out_size - used - 1);
        out[out_size - 1] = '\0';
        return;
    }
    used += (size_t)wrote;

    emitted = 0;
    overflow = 0;
    if (has_player) {
        PropRecord *prop;
        int guard_iter = 0;
        for (prop = get_ptr_obj_pos_list_current_entry();
             prop != NULL && guard_iter < 4096;
             prop = prop->prev, guard_iter++) {
            int kind;
            int owner_chrnum = -1;

            if (prop->type == PROP_TYPE_WEAPON) {
                ObjectRecord *obj = prop->obj;
                if (obj == NULL || obj->projectile == NULL) {
                    continue;
                }
                kind = (int)obj->projectile->droptype;
                if (obj->projectile->ownerprop != NULL &&
                    obj->projectile->ownerprop->type == PROP_TYPE_CHR &&
                    obj->projectile->ownerprop->chr != NULL) {
                    owner_chrnum = obj->projectile->ownerprop->chr->chrnum;
                }
            } else if (prop->type == PROP_TYPE_EXPLOSION) {
                kind = -1;
            } else {
                continue;
            }

            if (emitted >= TRACE_COMBAT_ORACLE_MAX_PROJECTILES) {
                overflow = 1;
                break;
            }

            wrote = snprintf(out + used, out_size - used,
                             "%s{\"kind\":%d,\"pos\":[%.2f,%.2f,%.2f],\"owner_chrnum\":%d}",
                             emitted == 0 ? "" : ",",
                             kind,
                             prop->pos.x, prop->pos.y, prop->pos.z,
                             owner_chrnum);
            if (wrote < 0 || (size_t)wrote >= out_size - used) {
                overflow = 1;
                break;
            }
            used += (size_t)wrote;
            emitted++;
        }
    }

    wrote = snprintf(out + used, out_size - used, "],\"projectiles_overflow\":%d},", overflow);
    if (wrote < 0 || (size_t)wrote >= out_size - used) {
        strncpy(out + used, "],\"projectiles_overflow\":1},", out_size - used - 1);
        out[out_size - 1] = '\0';
    }
}

static void traceBuildActorSummaryJson(char *out,
                                       size_t out_size,
                                       int has_player,
                                       float player_x,
                                       float player_y,
                                       float player_z)
{
    TraceActorSample samples[6];
    int sample_count = 0;
    int slots = 0;
    int live = 0;
    int alive_count = 0;
    int hidden_count = 0;
    int onscreen_count = 0;
    int rendered_count = 0;
    size_t used = 0;
    int i;

    if (out_size == 0) {
        return;
    }

    if (g_ChrSlots != NULL && g_NumChrSlots > 0 && g_NumChrSlots < 256) {
        slots = g_NumChrSlots;
        for (i = 0; i < g_NumChrSlots; i++) {
            ChrRecord *chr = &g_ChrSlots[i];
            PropRecord *prop;
            TraceActorSample sample;
            float dx;
            float dy;
            float dz;
            int room = -1;
            int rendered;

            if (chr == NULL || chr->model == NULL || chr->chrnum < 0 || chr->chrnum >= 1000) {
                continue;
            }

            live++;

            prop = chr->prop;
            rendered = tracePropRenderedInRooms(prop, &room);

            if (chr->damage < chr->maxdamage) {
                alive_count++;
            }

            if (chr->hidden != 0) {
                hidden_count++;
            }

            if (prop != NULL && (prop->flags & PROPFLAG_ONSCREEN) != 0) {
                onscreen_count++;
            }

            if (rendered) {
                rendered_count++;
            }

            if (prop == NULL || prop->type != PROP_TYPE_CHR) {
                continue;
            }

            dx = has_player ? prop->pos.x - player_x : 0.0f;
            dy = has_player ? prop->pos.y - player_y : 0.0f;
            dz = has_player ? prop->pos.z - player_z : 0.0f;

            memset(&sample, 0, sizeof(sample));
            sample.slot = i;
            sample.chrnum = chr->chrnum;
            sample.action = chr->actiontype;
            sample.alert = chr->alertness;
            sample.sleep = chr->sleep;
            sample.hidden = chr->hidden != 0 ? 1 : 0;
            sample.hidden_bits = chr->hidden;
            sample.alive = chr->damage < chr->maxdamage ? 1 : 0;
            sample.onscreen = (prop->flags & PROPFLAG_ONSCREEN) != 0 ? 1 : 0;
            sample.rendered = rendered;
            sample.room = room;
            sample.dist_sq = (dx * dx) + (dy * dy) + (dz * dz);
            sample.x = prop->pos.x;
            sample.y = prop->pos.y;
            sample.z = prop->pos.z;
            traceInsertActorSample(samples, &sample_count, &sample);
        }
    }

    {
        int wrote = snprintf(out + used,
                             out_size - used,
                             "{\"slots\":%d,\"live\":%d,\"alive\":%d,\"hidden\":%d,"
                             "\"onscreen\":%d,\"rendered\":%d,\"sample\":[",
                             slots,
                             live,
                             alive_count,
                             hidden_count,
                             onscreen_count,
                             rendered_count);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            snprintf(out,
                     out_size,
                     "{\"slots\":%d,\"live\":%d,\"alive\":%d,\"hidden\":%d,"
                     "\"onscreen\":%d,\"rendered\":%d,\"sample\":[]}",
                     slots,
                     live,
                     alive_count,
                     hidden_count,
                     onscreen_count,
                     rendered_count);
            return;
        }
        used += (size_t)wrote;
    }

    for (i = 0; i < sample_count && used < out_size; i++) {
        int wrote = snprintf(out + used,
                             out_size - used,
                             "%s{\"slot\":%d,\"chrnum\":%d,\"action\":%d,\"alert\":%d,\"sleep\":%d,"
                             "\"hidden\":%d,\"hidden_bits\":%d,\"alive\":%d,\"onscreen\":%d,"
                             "\"rendered\":%d,\"room\":%d,\"dist\":%.2f,\"pos\":[%.2f,%.2f,%.2f]}",
                             i == 0 ? "" : ",",
                             samples[i].slot,
                             samples[i].chrnum,
                             samples[i].action,
                             samples[i].alert,
                             samples[i].sleep,
                             samples[i].hidden,
                             samples[i].hidden_bits,
                             samples[i].alive,
                             samples[i].onscreen,
                             samples[i].rendered,
                             samples[i].room,
                             sqrtf(samples[i].dist_sq),
                             samples[i].x,
                             samples[i].y,
                             samples[i].z);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            used = out_size;
            break;
        }
        used += (size_t)wrote;
    }

    if (used + 3 <= out_size) {
        out[used++] = ']';
        out[used++] = '}';
        out[used] = '\0';
    } else {
        snprintf(out,
                 out_size,
                 "{\"slots\":%d,\"live\":%d,\"alive\":%d,\"hidden\":%d,"
                 "\"onscreen\":%d,\"rendered\":%d,\"sample\":[]}",
                 slots,
                 live,
                 alive_count,
                 hidden_count,
                 onscreen_count,
                 rendered_count);
    }
}

static void tracePropFloorSnapshot(PropRecord *prop, TracePropFloorSnapshot *out)
{
    StandTile *tile;

    memset(out, 0, sizeof(*out));
    out->room = -1;

    if (prop == NULL) {
        return;
    }

    tile = prop->stan;

    if (tile == NULL) {
        f32 pos[3];

        pos[0] = prop->pos.x;
        pos[1] = prop->pos.y;
        pos[2] = prop->pos.z;
        tile = sub_GAME_7F0AF20C(pos, 0, NULL);

        if (tile == NULL) {
            f32 x = prop->pos.x;
            f32 y = prop->pos.y;
            f32 z = prop->pos.z;

            tile = sub_GAME_7F0AFB78(&x, &y, &z, 0.0f);
            out->nearest = (tile != NULL) ? 1 : 0;
        }
    }

    if (tile == NULL) {
        return;
    }

    out->valid = 1;
    out->room = getTileRoom(tile);
    out->y = stanGetPositionYValue(tile, prop->pos.x, prop->pos.z);
    out->delta = prop->pos.y - out->y;
}

typedef struct TraceHeldPropSnapshot {
    int present;
    int obj;
    int item;
    int state;
    u32 runtime;
    int propflags;
    int has_mtx;
    int owner_type;
    int owner_chrnum;
    int owner_obj;
    char owner_rooms_json[64];
    int owner_room_count;
    int owner_first_room;
    int owner_stan_room;
    int owner_any_rendered;
    int owner_first_rendered;
    char rooms_json[64];
    int room_count;
    int first_room;
    int stan_room;
    int any_rendered;
    int first_rendered;
} TraceHeldPropSnapshot;

static void traceHeldPropSnapshot(PropRecord *prop, TraceHeldPropSnapshot *out)
{
    ObjectRecord *obj;
    PropRecord *owner;

    memset(out, 0, sizeof(*out));
    out->obj = -1;
    out->item = -1;
    out->owner_type = -1;
    out->owner_chrnum = -1;
    out->owner_obj = -1;
    out->owner_first_room = -1;
    out->owner_stan_room = -1;
    out->first_room = -1;
    out->stan_room = -1;
    snprintf(out->owner_rooms_json, sizeof(out->owner_rooms_json), "[]");
    snprintf(out->rooms_json, sizeof(out->rooms_json), "[]");

    traceBuildPropRoomsJson(prop,
                            out->rooms_json,
                            sizeof(out->rooms_json),
                            &out->room_count,
                            &out->first_room,
                            &out->stan_room,
                            &out->any_rendered,
                            &out->first_rendered);

    if (prop == NULL || prop->obj == NULL) {
        return;
    }

    owner = prop->parent;
    if (owner != NULL) {
        out->owner_type = owner->type;

        if (owner->type == PROP_TYPE_CHR && owner->chr != NULL) {
            out->owner_chrnum = owner->chr->chrnum;
        } else if ((owner->type == PROP_TYPE_OBJ
                    || owner->type == PROP_TYPE_DOOR
                    || owner->type == PROP_TYPE_WEAPON)
                   && owner->obj != NULL) {
            out->owner_obj = owner->obj->obj;
        }

        traceBuildPropRoomsJson(owner,
                                out->owner_rooms_json,
                                sizeof(out->owner_rooms_json),
                                &out->owner_room_count,
                                &out->owner_first_room,
                                &out->owner_stan_room,
                                &out->owner_any_rendered,
                                &out->owner_first_rendered);
    }

    obj = prop->obj;
    out->present = 1;
    out->obj = obj->obj;
    out->state = obj->state;
    out->runtime = obj->runtime_bitflags;
    out->propflags = prop->flags;

    if (prop->type == PROP_TYPE_WEAPON && prop->weapon != NULL) {
        out->item = prop->weapon->weaponnum;
    }

    if (obj->model != NULL
        && obj->model->render_pos != NULL
        && modelGetRenderPosCount(obj->model) > 0) {
        out->has_mtx = 1;
    }
}

static TraceGuardDropEvent *traceGuardDropEvent(const char *phase, const ChrRecord *chr)
{
    TraceGuardDropEvent *event;

    if (!traceJsonEventTraceEnabled()) {
        return NULL;
    }

    if (s_traceGuardDropEventCount >= TRACE_GUARD_DROP_EVENTS_MAX) {
        s_traceGuardDropOverflow++;
        return NULL;
    }

    event = &s_traceGuardDropEvents[s_traceGuardDropEventCount++];
    memset(event, 0, sizeof(*event));
    traceCopyToken(event->phase, sizeof(event->phase), phase);
    event->frame = g_GlobalTimer;
    event->chrnum = chr != NULL ? chr->chrnum : -1;
    event->action = chr != NULL ? chr->actiontype : -1;
    event->hidden = chr != NULL ? chr->hidden : 0;
    event->child_type = -1;
    event->detail = -1;
    event->dropped = -1;
    event->runtime = 0;
    event->owner_type = -1;
    event->owner_chrnum = -1;
    event->owner_obj = -1;
    event->next_type = -1;
    event->room_stan = -1;
    event->room_first = -1;
    snprintf(event->rooms_json, sizeof(event->rooms_json), "[]");

    if (chr != NULL) {
        event->damage = chr->damage;
        event->maxdamage = chr->maxdamage;
        if (chr->prop != NULL) {
            event->pos[0] = chr->prop->pos.x;
            event->pos[1] = chr->prop->pos.y;
            event->pos[2] = chr->prop->pos.z;
        }
    }

    return event;
}

void portTraceGuardDropHeldBegin(const ChrRecord *chr)
{
    traceGuardDropEvent("begin", chr);
}

void portTraceGuardDropHeldClear(const ChrRecord *chr)
{
    traceGuardDropEvent("clear", chr);
}

void portTraceGuardDropHeldChild(const ChrRecord *chr,
                                 PropRecord *child,
                                 PropRecord *owner,
                                 s32 dropped,
                                 PropRecord *next)
{
    TraceGuardDropEvent *event = traceGuardDropEvent("child", chr);

    if (event == NULL) {
        return;
    }

    event->dropped = dropped;

    if (next != NULL) {
        event->next_type = next->type;
    }

    if (owner != NULL) {
        event->owner_type = owner->type;

        if (owner->type == PROP_TYPE_CHR && owner->chr != NULL) {
            event->owner_chrnum = owner->chr->chrnum;
        } else if ((owner->type == PROP_TYPE_OBJ
                    || owner->type == PROP_TYPE_DOOR
                    || owner->type == PROP_TYPE_WEAPON)
                   && owner->obj != NULL) {
            event->owner_obj = owner->obj->obj;
        }
    }

    if (child == NULL) {
        return;
    }

    event->child_type = child->type;
    event->pos[0] = child->pos.x;
    event->pos[1] = child->pos.y;
    event->pos[2] = child->pos.z;

    traceBuildPropRoomsJson(child,
                            event->rooms_json,
                            sizeof(event->rooms_json),
                            &event->room_count,
                            &event->room_first,
                            &event->room_stan,
                            &event->room_any_rendered,
                            &event->room_first_rendered);

    if (child->obj != NULL) {
        event->detail = child->obj->obj;
        event->runtime = child->obj->runtime_bitflags;

        if (child->obj->model != NULL) {
            event->scale = child->obj->model->scale;
        }

        event->basis[0] = sqrtf(
            child->obj->mtx.m[0][0] * child->obj->mtx.m[0][0] +
            child->obj->mtx.m[0][1] * child->obj->mtx.m[0][1] +
            child->obj->mtx.m[0][2] * child->obj->mtx.m[0][2]);
        event->basis[1] = sqrtf(
            child->obj->mtx.m[1][0] * child->obj->mtx.m[1][0] +
            child->obj->mtx.m[1][1] * child->obj->mtx.m[1][1] +
            child->obj->mtx.m[1][2] * child->obj->mtx.m[1][2]);
        event->basis[2] = sqrtf(
            child->obj->mtx.m[2][0] * child->obj->mtx.m[2][0] +
            child->obj->mtx.m[2][1] * child->obj->mtx.m[2][1] +
            child->obj->mtx.m[2][2] * child->obj->mtx.m[2][2]);
    }

    if (child->type == PROP_TYPE_WEAPON && child->weapon != NULL) {
        event->detail = child->weapon->weaponnum;
    }
}

static void traceBuildGuardDropJson(char *out, size_t out_size)
{
    size_t used = 0;
    int wrote;
    int i;

    if (out_size == 0) {
        return;
    }

    wrote = snprintf(out,
                     out_size,
                     "\"drop\":{\"count\":%d,\"overflow\":%d,\"events\":[",
                     s_traceGuardDropEventCount,
                     s_traceGuardDropOverflow);
    if (wrote < 0 || (size_t)wrote >= out_size) {
        snprintf(out, out_size, "\"drop\":{\"count\":0,\"overflow\":1,\"events\":[]},");
        s_traceGuardDropEventCount = 0;
        s_traceGuardDropOverflow = 0;
        return;
    }
    used = (size_t)wrote;

    for (i = 0; i < s_traceGuardDropEventCount; i++) {
        TraceGuardDropEvent *event = &s_traceGuardDropEvents[i];

        wrote = snprintf(out + used,
                         out_size - used,
                         "%s{\"frame\":%d,\"phase\":\"%s\",\"chrnum\":%d,"
                         "\"state\":{\"action\":%d,\"hidden\":%d,"
                         "\"damage\":%.4f,\"maxdamage\":%.4f},"
                         "\"child\":{\"type\":%d,\"detail\":%d,\"dropped\":%d,"
                         "\"runtime\":\"0x%08X\",\"next_type\":%d,"
                         "\"pos\":[%.2f,%.2f,%.2f],\"scale\":%.4f,"
                         "\"basis\":[%.4f,%.4f,%.4f]},"
                         "\"owner\":{\"type\":%d,\"chrnum\":%d,\"obj\":%d},"
                         "\"room\":{\"stan\":%d,\"first\":%d,\"count\":%d,"
                         "\"any_rendered\":%d,\"first_rendered\":%d,\"rooms\":%s}}",
                         i == 0 ? "" : ",",
                         event->frame,
                         event->phase,
                         event->chrnum,
                         event->action,
                         event->hidden,
                         event->damage,
                         event->maxdamage,
                         event->child_type,
                         event->detail,
                         event->dropped,
                         (unsigned int)event->runtime,
                         event->next_type,
                         event->pos[0], event->pos[1], event->pos[2],
                         event->scale,
                         event->basis[0], event->basis[1], event->basis[2],
                         event->owner_type,
                         event->owner_chrnum,
                         event->owner_obj,
                         event->room_stan,
                         event->room_first,
                         event->room_count,
                         event->room_any_rendered,
                         event->room_first_rendered,
                         event->rooms_json);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            snprintf(out, out_size, "\"drop\":{\"count\":0,\"overflow\":1,\"events\":[]},");
            s_traceGuardDropEventCount = 0;
            s_traceGuardDropOverflow = 0;
            return;
        }
        used += (size_t)wrote;
    }

    if (used + 3 <= out_size) {
        out[used++] = ']';
        out[used++] = '}';
        out[used++] = ',';
        out[used] = '\0';
    } else {
        snprintf(out, out_size, "\"drop\":{\"count\":0,\"overflow\":1,\"events\":[]},");
    }

    s_traceGuardDropEventCount = 0;
    s_traceGuardDropOverflow = 0;
}

static void traceBuildGuardSpawnJson(char *out, size_t out_size)
{
    size_t used = 0;
    int wrote;
    int i;

    if (out_size == 0) {
        return;
    }

    wrote = snprintf(out,
                     out_size,
                     "\"spawn\":{\"count\":%d,\"overflow\":%d,\"events\":[",
                     s_traceGuardSpawnEventCount,
                     s_traceGuardSpawnOverflow);
    if (wrote < 0 || (size_t)wrote >= out_size) {
        snprintf(out, out_size, "\"spawn\":{\"count\":0,\"overflow\":1,\"events\":[]},");
        s_traceGuardSpawnEventCount = 0;
        s_traceGuardSpawnOverflow = 0;
        return;
    }

    used = (size_t)wrote;

    for (i = 0; i < s_traceGuardSpawnEventCount; i++) {
        TraceGuardSpawnEvent *event = &s_traceGuardSpawnEvents[i];

        wrote = snprintf(out + used,
                         out_size - used,
                         "%s{\"frame\":%d,\"source\":\"%s\",\"reason\":\"%s\",\"success\":%d,"
                         "\"body\":%d,\"head\":%d,\"source_chr\":%d,\"target_chr\":%d,"
                         "\"requested_pad\":%d,\"resolved_pad\":%d,\"flags\":\"0x%08X\",\"free\":%d,"
                         "\"ai\":{\"list\":%d,\"global\":%d},\"chrnum\":%d,"
                         "\"room\":{\"stan\":%d,\"first\":%d,\"count\":%d},"
                         "\"orig\":[%.2f,%.2f,%.2f],\"final_valid\":%d,"
                         "\"final\":[%.2f,%.2f,%.2f]}",
                         i == 0 ? "" : ",",
                         event->frame,
                         event->source,
                         event->reason,
                         event->success,
                         event->body,
                         event->head,
                         event->source_chrnum,
                         event->target_chrnum,
                         event->requested_pad,
                         event->resolved_pad,
                         (unsigned int)event->flags,
                         event->free_count,
                         event->ai_list,
                         event->ai_global,
                         event->chrnum,
                         event->stan_room,
                         event->prop_first_room,
                         event->prop_room_count,
                         event->original_pos[0],
                         event->original_pos[1],
                         event->original_pos[2],
                         event->has_final_pos,
                         event->final_pos[0],
                         event->final_pos[1],
                         event->final_pos[2]);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            snprintf(out, out_size, "\"spawn\":{\"count\":0,\"overflow\":1,\"events\":[]},");
            s_traceGuardSpawnEventCount = 0;
            s_traceGuardSpawnOverflow = 0;
            return;
        }

        used += (size_t)wrote;
    }

    wrote = snprintf(out + used, out_size - used, "]},");
    if (wrote < 0 || (size_t)wrote >= out_size - used) {
        snprintf(out, out_size, "\"spawn\":{\"count\":0,\"overflow\":1,\"events\":[]},");
    }

    s_traceGuardSpawnEventCount = 0;
    s_traceGuardSpawnOverflow = 0;
}

static void traceBuildShotJson(char *out, size_t out_size)
{
    size_t used = 0;
    int wrote;
    int i;

    if (out_size == 0) {
        return;
    }

    wrote = snprintf(out,
                     out_size,
                     "\"shot\":{\"count\":%d,\"overflow\":%d,\"events\":[",
                     s_traceShotEventCount,
                     s_traceShotOverflow);
    if (wrote < 0 || (size_t)wrote >= out_size) {
        snprintf(out, out_size, "\"shot\":{\"count\":0,\"overflow\":1,\"events\":[]},");
        s_traceShotEventCount = 0;
        s_traceShotOverflow = 0;
        return;
    }

    used = (size_t)wrote;

    for (i = 0; i < s_traceShotEventCount; i++) {
        TraceShotEvent *event = &s_traceShotEvents[i];

        wrote = snprintf(out + used,
                         out_size - used,
                         "%s{\"frame\":%d,\"shot_id\":%d,\"phase\":\"%s\",\"hand\":%d,\"weapon\":%d,"
                         "\"slot\":%d,\"prop\":{\"type\":%d,\"obj_type\":%d,\"chrnum\":%d,\"obj\":%d,\"item\":%d,\"pad\":%d},"
                         "\"obj_pad\":%d,\"hit_result\":%d,\"do_damage\":%d,\"distance\":%.2f,"
                         "\"mtx\":%d,\"texture\":%d,\"player_room\":%d,\"best_room\":%d,"
                         "\"hit_bg\":%d,\"hit_something\":%d,\"hit_count\":%d,\"shoot_through\":%d,"
                         "\"impact\":%d,\"threshold\":%.2f,"
                         "\"pos\":[%.6f,%.6f,%.6f],\"normal\":[%.6f,%.6f,%.6f],"
                         "\"ray\":{\"valid\":%d,\"screen_pos\":[%.6f,%.6f,%.6f],"
                         "\"screen_dir\":[%.8f,%.8f,%.8f],"
                         "\"attacker_pos\":[%.6f,%.6f,%.6f],"
                         "\"world_dir\":[%.8f,%.8f,%.8f],"
                         "\"dest_valid\":%d,\"dest\":[%.6f,%.6f,%.6f]}}",
                         i == 0 ? "" : ",",
                         event->frame,
                         event->shot_id,
                         event->phase,
                         event->hand,
                         event->weapon,
                         event->slot,
                         event->prop.type,
                         event->prop.obj_type,
                         event->prop.chrnum,
                         event->prop.obj,
                         event->prop.item,
                         event->prop.pad,
                         event->obj_pad,
                         event->hit_result,
                         event->do_damage,
                         event->distance,
                         event->mtx_index,
                         event->texture,
                         event->player_room,
                         event->best_room,
                         event->hit_bg,
                         event->hit_something,
                         event->hit_count,
                         event->shoot_through,
                         event->impact_type,
                         event->threshold,
                         event->pos[0],
                         event->pos[1],
                         event->pos[2],
                         event->normal[0],
                         event->normal[1],
                         event->normal[2],
                         event->ray_valid,
                         event->screen_pos[0],
                         event->screen_pos[1],
                         event->screen_pos[2],
                         event->screen_dir[0],
                         event->screen_dir[1],
                         event->screen_dir[2],
                         event->attacker_pos[0],
                         event->attacker_pos[1],
                         event->attacker_pos[2],
                         event->world_dir[0],
                         event->world_dir[1],
                         event->world_dir[2],
                         event->dest_valid,
                         event->dest[0],
                         event->dest[1],
                         event->dest[2]);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            snprintf(out, out_size, "\"shot\":{\"count\":0,\"overflow\":1,\"events\":[]},");
            s_traceShotEventCount = 0;
            s_traceShotOverflow = 0;
            return;
        }

        used += (size_t)wrote;
    }

    wrote = snprintf(out + used, out_size - used, "]},");
    if (wrote < 0 || (size_t)wrote >= out_size - used) {
        snprintf(out, out_size, "\"shot\":{\"count\":0,\"overflow\":1,\"events\":[]},");
    }

    s_traceShotEventCount = 0;
    s_traceShotOverflow = 0;
}

static void traceBuildBulletImpactJson(char *out, size_t out_size)
{
    size_t used = 0;
    int wrote;
    int i;

    if (out_size == 0) {
        return;
    }

    wrote = snprintf(out,
                     out_size,
                     "\"impact\":{\"create_count\":%d,\"render_count\":%d,\"overflow\":%d,\"creates\":[",
                     s_traceBulletImpactCreateCount,
                     s_traceBulletImpactRenderCount,
                     s_traceBulletImpactOverflow);
    if (wrote < 0 || (size_t)wrote >= out_size) {
        snprintf(out, out_size, "\"impact\":{\"create_count\":0,\"render_count\":0,\"overflow\":1,\"creates\":[],\"renders\":[]},");
        s_traceBulletImpactCreateCount = 0;
        s_traceBulletImpactRenderCount = 0;
        s_traceBulletImpactOverflow = 0;
        return;
    }

    used = (size_t)wrote;

    for (i = 0; i < s_traceBulletImpactCreateCount; i++) {
        TraceBulletImpactCreateEvent *event = &s_traceBulletImpactCreates[i];

        wrote = snprintf(out + used,
                         out_size - used,
                         "%s{\"frame\":%d,\"shot_id\":%d,\"slot\":%d,\"impact\":%d,\"room\":%d,"
                         "\"prop\":{\"type\":%d,\"obj_type\":%d,\"chrnum\":%d,\"obj\":%d,\"item\":%d,\"pad\":%d},"
                         "\"material\":{\"texture\":%d,\"hit_sound\":%d,\"impact\":%d},"
                         "\"model_pos\":%d,\"clear\":%d,\"image\":[%d,%d],"
                         "\"pos\":[%.6f,%.6f,%.6f],\"normal\":[%.6f,%.6f,%.6f],"
                         "\"size\":[%.6f,%.6f],\"normal_offset\":%.6f,"
                         "\"basis\":{\"normal_unit\":[%.8f,%.8f,%.8f],"
                         "\"axis_x\":[%.8f,%.8f,%.8f],"
                         "\"axis_y\":[%.8f,%.8f,%.8f]},"
                         "\"raw_v\":[[%.6f,%.6f,%.6f],[%.6f,%.6f,%.6f],"
                         "[%.6f,%.6f,%.6f],[%.6f,%.6f,%.6f]],"
                         "\"rounded_v\":[[%d,%d,%d],[%d,%d,%d],[%d,%d,%d],[%d,%d,%d]]}",
                         i == 0 ? "" : ",",
                         event->frame,
                         event->shot_id,
                         event->slot,
                         event->impact_type,
                         event->room,
                         event->prop.type,
                         event->prop.obj_type,
                         event->prop.chrnum,
                         event->prop.obj,
                         event->prop.item,
                         event->prop.pad,
                         event->material_texturenum,
                         event->material_hit_sound,
                         event->material_impact_type,
                         event->model_pos,
                         event->clear,
                         event->width,
                         event->height,
                         event->pos[0],
                         event->pos[1],
                         event->pos[2],
                         event->normal[0],
                         event->normal[1],
                         event->normal[2],
                         event->size[0],
                         event->size[1],
                         event->normal_offset,
                         event->normal_unit[0],
                         event->normal_unit[1],
                         event->normal_unit[2],
                         event->axis_x[0],
                         event->axis_x[1],
                         event->axis_x[2],
                         event->axis_y[0],
                         event->axis_y[1],
                         event->axis_y[2],
                         event->raw_vertices[0][0],
                         event->raw_vertices[0][1],
                         event->raw_vertices[0][2],
                         event->raw_vertices[1][0],
                         event->raw_vertices[1][1],
                         event->raw_vertices[1][2],
                         event->raw_vertices[2][0],
                         event->raw_vertices[2][1],
                         event->raw_vertices[2][2],
                         event->raw_vertices[3][0],
                         event->raw_vertices[3][1],
                         event->raw_vertices[3][2],
                         event->rounded_vertices[0][0],
                         event->rounded_vertices[0][1],
                         event->rounded_vertices[0][2],
                         event->rounded_vertices[1][0],
                         event->rounded_vertices[1][1],
                         event->rounded_vertices[1][2],
                         event->rounded_vertices[2][0],
                         event->rounded_vertices[2][1],
                         event->rounded_vertices[2][2],
                         event->rounded_vertices[3][0],
                         event->rounded_vertices[3][1],
                         event->rounded_vertices[3][2]);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            snprintf(out, out_size, "\"impact\":{\"create_count\":0,\"render_count\":0,\"overflow\":1,\"creates\":[],\"renders\":[]},");
            s_traceBulletImpactCreateCount = 0;
            s_traceBulletImpactRenderCount = 0;
            s_traceBulletImpactOverflow = 0;
            return;
        }

        used += (size_t)wrote;
    }

    wrote = snprintf(out + used, out_size - used, "],\"renders\":[");
    if (wrote < 0 || (size_t)wrote >= out_size - used) {
        snprintf(out, out_size, "\"impact\":{\"create_count\":0,\"render_count\":0,\"overflow\":1,\"creates\":[],\"renders\":[]},");
        s_traceBulletImpactCreateCount = 0;
        s_traceBulletImpactRenderCount = 0;
        s_traceBulletImpactOverflow = 0;
        return;
    }

    used += (size_t)wrote;

    for (i = 0; i < s_traceBulletImpactRenderCount; i++) {
        TraceBulletImpactRenderEvent *event = &s_traceBulletImpactRenders[i];

        wrote = snprintf(out + used,
                         out_size - used,
                         "%s{\"frame\":%d,\"world\":%d,\"alpha_pass\":%d,\"flat\":%d,"
                         "\"rendered\":%d,\"last_impact\":%d,\"current_slot\":%d,"
                         "\"prop\":{\"type\":%d,\"obj_type\":%d,\"chrnum\":%d,\"obj\":%d,\"item\":%d,\"pad\":%d}}",
                         i == 0 ? "" : ",",
                         event->frame,
                         event->world,
                         event->alpha_pass,
                         event->flat,
                         event->rendered,
                         event->last_impact,
                         event->current_slot,
                         event->prop.type,
                         event->prop.obj_type,
                         event->prop.chrnum,
                         event->prop.obj,
                         event->prop.item,
                         event->prop.pad);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            snprintf(out, out_size, "\"impact\":{\"create_count\":0,\"render_count\":0,\"overflow\":1,\"creates\":[],\"renders\":[]},");
            s_traceBulletImpactCreateCount = 0;
            s_traceBulletImpactRenderCount = 0;
            s_traceBulletImpactOverflow = 0;
            return;
        }

        used += (size_t)wrote;
    }

    wrote = snprintf(out + used, out_size - used, "]},");
    if (wrote < 0 || (size_t)wrote >= out_size - used) {
        snprintf(out, out_size, "\"impact\":{\"create_count\":0,\"render_count\":0,\"overflow\":1,\"creates\":[],\"renders\":[]},");
    }

    s_traceBulletImpactCreateCount = 0;
    s_traceBulletImpactRenderCount = 0;
    s_traceBulletImpactOverflow = 0;
    s_traceBulletImpactPendingMaterialValid = 0;
}

static int traceRoomLocalToWorld(int room,
                                 float local_x,
                                 float local_y,
                                 float local_z,
                                 float *world_x,
                                 float *world_y,
                                 float *world_z)
{
    if (room < 0 || room >= g_MaxNumRooms || ptr_bgdata_room_fileposition_list == NULL) {
        return 0;
    }
    if (world_x == NULL || world_y == NULL || world_z == NULL) {
        return 0;
    }
    if (!__builtin_isfinite(room_data_float2) || room_data_float2 == 0.0f) {
        return 0;
    }

    *world_x = (ptr_bgdata_room_fileposition_list[room].pos.f[0] + local_x) * room_data_float2;
    *world_y = (ptr_bgdata_room_fileposition_list[room].pos.f[1] + local_y) * room_data_float2;
    *world_z = (ptr_bgdata_room_fileposition_list[room].pos.f[2] + local_z) * room_data_float2;
    return __builtin_isfinite(*world_x) && __builtin_isfinite(*world_y) && __builtin_isfinite(*world_z);
}

static int traceWorldImpactProjectionInvVisEnabled(void)
{
    static int enabled = -1;

    if (enabled < 0) {
        const char *value = getenv("GE007_BULLET_IMPACT_INV_VIS_SCALE");
        enabled = (value != NULL && value[0] != '\0' && value[0] == '0') ? 0 : 1;
    }

    return enabled;
}

static int traceProjectRoomLocalQuad(int room,
                                     const int v[4][3],
                                     const float projection[4][4],
                                     float viewport_left,
                                     float viewport_top,
                                     float viewport_width,
                                     float viewport_height,
                                     float model_v[4][3],
                                     float screen_v[4][2],
                                     float clip_w[4],
                                     float bbox[4],
                                     float *area,
                                     int *behind,
                                     int *onscreen)
{
    float min_x = FLT_MAX;
    float min_y = FLT_MAX;
    float max_x = -FLT_MAX;
    float max_y = -FLT_MAX;
    float room_x;
    float room_y;
    float room_z;
    float scale;
    float model_scale = 1.0f;

    if (g_CurrentPlayer == NULL || ptr_bgdata_room_fileposition_list == NULL) {
        return 0;
    }
    if (room < 0 || room >= g_MaxNumRooms) {
        return 0;
    }
    if (!__builtin_isfinite(room_data_float2) || room_data_float2 == 0.0f) {
        return 0;
    }

    room_x = ptr_bgdata_room_fileposition_list[room].pos.f[0];
    room_y = ptr_bgdata_room_fileposition_list[room].pos.f[1];
    room_z = ptr_bgdata_room_fileposition_list[room].pos.f[2];
    scale = room_data_float2;
    if (traceWorldImpactProjectionInvVisEnabled()) {
        float vis_scale = bgGetLevelVisibilityScale();
        if (__builtin_isfinite(vis_scale) && vis_scale != 0.0f && vis_scale != 1.0f) {
            model_scale = 1.0f / vis_scale;
        }
    }
    *behind = 0;
    *onscreen = 0;

    for (int vi = 0; vi < 4; vi++) {
        float clip[4];
        float ndc_x;
        float ndc_y;

        model_v[vi][0] = ((room_x + (float)v[vi][0]) * scale - g_CurrentPlayer->current_model_pos.f[0]) * model_scale;
        model_v[vi][1] = ((room_y + (float)v[vi][1]) * scale - g_CurrentPlayer->current_model_pos.f[1]) * model_scale;
        model_v[vi][2] = ((room_z + (float)v[vi][2]) * scale - g_CurrentPlayer->current_model_pos.f[2]) * model_scale;

        traceTransform4x4(projection,
                          model_v[vi][0],
                          model_v[vi][1],
                          model_v[vi][2],
                          1.0f,
                          clip);
        clip_w[vi] = clip[3];
        if (!__builtin_isfinite(clip[0]) || !__builtin_isfinite(clip[1]) ||
            !__builtin_isfinite(clip[3]) || fabsf(clip[3]) < 0.000001f) {
            return 0;
        }
        if (clip[3] <= 0.0f) {
            *behind = 1;
        }

        ndc_x = clip[0] / clip[3];
        ndc_y = clip[1] / clip[3];
        screen_v[vi][0] = viewport_left + (ndc_x * 0.5f + 0.5f) * viewport_width;
        screen_v[vi][1] = viewport_top + (0.5f - ndc_y * 0.5f) * viewport_height;
        if (!__builtin_isfinite(screen_v[vi][0]) || !__builtin_isfinite(screen_v[vi][1])) {
            return 0;
        }

        if (screen_v[vi][0] < min_x) min_x = screen_v[vi][0];
        if (screen_v[vi][1] < min_y) min_y = screen_v[vi][1];
        if (screen_v[vi][0] > max_x) max_x = screen_v[vi][0];
        if (screen_v[vi][1] > max_y) max_y = screen_v[vi][1];
    }

    if (min_x == FLT_MAX || min_y == FLT_MAX || max_x == -FLT_MAX || max_y == -FLT_MAX) {
        return 0;
    }

    bbox[0] = min_x;
    bbox[1] = min_y;
    bbox[2] = max_x;
    bbox[3] = max_y;
    *area = (max_x - min_x) * (max_y - min_y);
    if (*area < 0.0f || !__builtin_isfinite(*area)) {
        *area = 0.0f;
    }
    *onscreen =
        max_x >= viewport_left &&
        max_y >= viewport_top &&
        min_x <= viewport_left + viewport_width &&
        min_y <= viewport_top + viewport_height;
    return 1;
}

static void traceBuildImpactStateJson(char *out, size_t out_size)
{
    u64 hash = 1469598103934665603ULL;
    char sample_json[32768];
    size_t sample_used = 1;
    int sample_count = 0;
    int occupied = 0;
    int first_index = -1;
    int first_room = -1;
    int first_impact = -1;
    float first_center_x = 0.0f;
    float first_center_y = 0.0f;
    float first_center_z = 0.0f;
    float first_world_center_x = 0.0f;
    float first_world_center_y = 0.0f;
    float first_world_center_z = 0.0f;
    int first_has_world = 0;
    float projection[4][4];
    int projection_valid = 0;
    int projection_is_float = 0;
    float viewport_left = 0.0f;
    float viewport_top = 0.0f;
    float viewport_width = 320.0f;
    float viewport_height = 240.0f;
    int i;

    if (out_size == 0) {
        return;
    }

    if (g_CurrentPlayer != NULL) {
        viewport_left = g_CurrentPlayer->c_screenleft;
        viewport_top = g_CurrentPlayer->c_screentop;
        viewport_width = g_CurrentPlayer->c_screenwidth;
        viewport_height = g_CurrentPlayer->c_screenheight;
    }
    traceLoadCurrentField10E0(projection, &projection_valid, &projection_is_float);

    sample_json[0] = '[';
    sample_json[1] = '\0';

    if (g_BulletImpactBuffer == NULL || BULLET_IMPACT_BUFFER_LEN <= 0) {
        snprintf(out, out_size,
                 "{\"present\":0,\"buffer_len\":%d,\"current_slot\":%d,"
                 "\"occupied\":0,\"first\":{\"index\":-1},\"sample\":[],"
                 "\"projection\":{\"valid\":%d,\"float\":%d,\"source\":\"room_matrix_field10e0\","
                 "\"viewport\":[%.2f,%.2f,%.2f,%.2f]},"
                 "\"hash\":\"0x%016llX\"}",
                 BULLET_IMPACT_BUFFER_LEN,
                 g_NumImpactEntries,
                 projection_valid,
                 projection_is_float,
                 viewport_left,
                 viewport_top,
                 viewport_width,
                 viewport_height,
                 (unsigned long long)hash);
        return;
    }

    for (i = 0; i < BULLET_IMPACT_BUFFER_LEN; i++) {
        struct BulletImpact *impact = &g_BulletImpactBuffer[i];
        int v[4][3];
        int tc[4][2];
        int cn[4][4];
        float world_v[4][3];
        float center_x;
        float center_y;
        float center_z;
        float world_center_x;
        float world_center_y;
        float world_center_z;
        int has_world = impact->prop == NULL;
        const PropRecord *impact_prop = impact->prop;
        int prop_type = -1;
        int prop_chrnum = -1;
        int prop_obj_type = -1;
        int prop_obj = -1;
        int prop_pad = -1;
        float model_v[4][3] = {{0.0f}};
        float screen_v[4][2] = {{0.0f}};
        float clip_w[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float screen_bbox[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float screen_area = 0.0f;
        int projected = 0;
        int projection_behind = 0;
        int projection_onscreen = 0;
        const char *projection_source = traceWorldImpactProjectionInvVisEnabled()
            ? "room_matrix_field10e0_inv_vis"
            : "room_matrix_field10e0";
        int vi;

        if (impact->room < 0) {
            continue;
        }

        if (impact_prop != NULL) {
            prop_type = impact_prop->type;
            if (impact_prop->type == PROP_TYPE_CHR && impact_prop->chr != NULL) {
                prop_chrnum = impact_prop->chr->chrnum;
            } else if ((impact_prop->type == PROP_TYPE_OBJ ||
                        impact_prop->type == PROP_TYPE_DOOR ||
                        impact_prop->type == PROP_TYPE_WEAPON) &&
                       impact_prop->obj != NULL) {
                const ObjectRecord *obj = impact_prop->obj;
                prop_obj_type = obj->type;
                prop_obj = obj->obj;
                prop_pad = obj->pad;
            }
        }

        for (vi = 0; vi < 4; vi++) {
            v[vi][0] = impact->vertex_list[vi].v.ob[0];
            v[vi][1] = impact->vertex_list[vi].v.ob[1];
            v[vi][2] = impact->vertex_list[vi].v.ob[2];
            tc[vi][0] = impact->vertex_list[vi].v.tc[0];
            tc[vi][1] = impact->vertex_list[vi].v.tc[1];
            cn[vi][0] = impact->vertex_list[vi].v.cn[0];
            cn[vi][1] = impact->vertex_list[vi].v.cn[1];
            cn[vi][2] = impact->vertex_list[vi].v.cn[2];
            cn[vi][3] = impact->vertex_list[vi].v.cn[3];
            if (has_world) {
                has_world = traceRoomLocalToWorld(impact->room,
                                                  (float)v[vi][0],
                                                  (float)v[vi][1],
                                                  (float)v[vi][2],
                                                  &world_v[vi][0],
                                                  &world_v[vi][1],
                                                  &world_v[vi][2]);
            }
        }

        center_x = (float)(v[0][0] + v[1][0] + v[2][0] + v[3][0]) * 0.25f;
        center_y = (float)(v[0][1] + v[1][1] + v[2][1] + v[3][1]) * 0.25f;
        center_z = (float)(v[0][2] + v[1][2] + v[2][2] + v[3][2]) * 0.25f;
        world_center_x = center_x;
        world_center_y = center_y;
        world_center_z = center_z;
        if (has_world) {
            world_center_x = (world_v[0][0] + world_v[1][0] + world_v[2][0] + world_v[3][0]) * 0.25f;
            world_center_y = (world_v[0][1] + world_v[1][1] + world_v[2][1] + world_v[3][1]) * 0.25f;
            world_center_z = (world_v[0][2] + world_v[1][2] + world_v[2][2] + world_v[3][2]) * 0.25f;
            if (projection_valid) {
                projected = traceProjectRoomLocalQuad(impact->room,
                                                      v,
                                                      projection,
                                                      viewport_left,
                                                      viewport_top,
                                                      viewport_width,
                                                      viewport_height,
                                                      model_v,
                                                      screen_v,
                                                      clip_w,
                                                      screen_bbox,
                                                      &screen_area,
                                                      &projection_behind,
                                                      &projection_onscreen);
            }
        } else {
            projection_source = "prop_matrix_not_traced";
            for (vi = 0; vi < 4; vi++) {
                world_v[vi][0] = (float)v[vi][0];
                world_v[vi][1] = (float)v[vi][1];
                world_v[vi][2] = (float)v[vi][2];
            }
        }

        if (first_index < 0) {
            first_index = i;
            first_room = impact->room;
            first_impact = impact->impact_type;
            first_center_x = center_x;
            first_center_y = center_y;
            first_center_z = center_z;
            first_world_center_x = world_center_x;
            first_world_center_y = world_center_y;
            first_world_center_z = world_center_z;
            first_has_world = has_world ? 1 : 0;
        }

        occupied++;
        hash = traceIntroHashU32(hash, (u32)i);
        hash = traceIntroHashU32(hash, (u32)(u16)impact->room);
        hash = traceIntroHashU32(hash, (u32)(u16)impact->impact_type);
        hash = traceIntroHashU32(hash, (u32)(u8)impact->model_render_pos_index);
        hash = traceIntroHashU32(hash, (u32)(u8)impact->room_clear_flag);
        hash = traceIntroHashU32(hash, impact->prop != NULL ? 1U : 0U);
        for (vi = 0; vi < 4; vi++) {
            hash = traceIntroHashU32(hash, traceReadU32Bytes(&impact->vertex_list[vi], 0x00));
            hash = traceIntroHashU32(hash, traceReadU32Bytes(&impact->vertex_list[vi], 0x04));
            hash = traceIntroHashU32(hash, traceReadU32Bytes(&impact->vertex_list[vi], 0x08));
            hash = traceIntroHashU32(hash, traceReadU32Bytes(&impact->vertex_list[vi], 0x0c));
        }

        if (sample_count < 4 && sample_used < sizeof(sample_json)) {
            int remaining = (int)(sizeof(sample_json) - sample_used);
            int wrote = snprintf(sample_json + sample_used,
                                 (size_t)remaining,
                                 "%s{\"index\":%d,\"room\":%d,\"impact\":%d,"
                                 "\"model_pos\":%d,\"clear\":%d,\"prop\":%d,"
                                 "\"prop_addr\":\"0x%llX\","
                                 "\"prop_type\":%d,\"prop_chrnum\":%d,"
                                 "\"prop_obj_type\":%d,\"prop_obj\":%d,\"prop_pad\":%d,"
                                 "\"center\":[%.2f,%.2f,%.2f],"
                                 "\"world\":%d,\"world_center\":[%.2f,%.2f,%.2f],"
                                 "\"v\":[[%d,%d,%d],[%d,%d,%d],[%d,%d,%d],[%d,%d,%d]],"
                                 "\"world_v\":[[%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f],"
                                 "[%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f]],"
                                 "\"tc\":[[%d,%d],[%d,%d],[%d,%d],[%d,%d]],"
                                 "\"rgba\":[[%d,%d,%d,%d],[%d,%d,%d,%d],"
                                 "[%d,%d,%d,%d],[%d,%d,%d,%d]],"
                                 "\"projection\":{\"valid\":%d,\"source\":\"%s\","
                                 "\"onscreen\":%d,\"behind\":%d,"
                                 "\"screen_bbox\":[%.2f,%.2f,%.2f,%.2f],"
                                 "\"screen_area\":%.2f,"
                                 "\"clip_w\":[%.4f,%.4f,%.4f,%.4f],"
                                 "\"model\":[[%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f],"
                                 "[%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f]],"
                                 "\"screen\":[[%.2f,%.2f],[%.2f,%.2f],"
                                 "[%.2f,%.2f],[%.2f,%.2f]]}}",
                                 sample_count == 0 ? "" : ",",
                                 i,
                                 impact->room,
                                 impact->impact_type,
                                 impact->model_render_pos_index,
                                 impact->room_clear_flag,
                                 impact->prop != NULL ? 1 : 0,
                                 (unsigned long long)(uintptr_t)impact->prop,
                                 prop_type,
                                 prop_chrnum,
                                 prop_obj_type,
                                 prop_obj,
                                 prop_pad,
                                 center_x, center_y, center_z,
                                 has_world ? 1 : 0,
                                 world_center_x, world_center_y, world_center_z,
                                 v[0][0], v[0][1], v[0][2],
                                 v[1][0], v[1][1], v[1][2],
                                 v[2][0], v[2][1], v[2][2],
                                 v[3][0], v[3][1], v[3][2],
                                 world_v[0][0], world_v[0][1], world_v[0][2],
                                 world_v[1][0], world_v[1][1], world_v[1][2],
                                 world_v[2][0], world_v[2][1], world_v[2][2],
                                 world_v[3][0], world_v[3][1], world_v[3][2],
                                 tc[0][0], tc[0][1],
                                 tc[1][0], tc[1][1],
                                 tc[2][0], tc[2][1],
                                 tc[3][0], tc[3][1],
                                 cn[0][0], cn[0][1], cn[0][2], cn[0][3],
                                 cn[1][0], cn[1][1], cn[1][2], cn[1][3],
                                 cn[2][0], cn[2][1], cn[2][2], cn[2][3],
                                 cn[3][0], cn[3][1], cn[3][2], cn[3][3],
                                 projected,
                                 projection_source,
                                 projection_onscreen,
                                 projection_behind,
                                 screen_bbox[0], screen_bbox[1], screen_bbox[2], screen_bbox[3],
                                 screen_area,
                                 clip_w[0], clip_w[1], clip_w[2], clip_w[3],
                                 model_v[0][0], model_v[0][1], model_v[0][2],
                                 model_v[1][0], model_v[1][1], model_v[1][2],
                                 model_v[2][0], model_v[2][1], model_v[2][2],
                                 model_v[3][0], model_v[3][1], model_v[3][2],
                                 screen_v[0][0], screen_v[0][1],
                                 screen_v[1][0], screen_v[1][1],
                                 screen_v[2][0], screen_v[2][1],
                                 screen_v[3][0], screen_v[3][1]);
            if (wrote > 0) {
                sample_used += (size_t)wrote < (size_t)remaining ? (size_t)wrote : (size_t)remaining - 1U;
            }
            sample_count++;
        }
    }

    if (sample_used < sizeof(sample_json) - 1U) {
        sample_json[sample_used++] = ']';
        sample_json[sample_used] = '\0';
    } else {
        sample_json[sizeof(sample_json) - 2U] = ']';
        sample_json[sizeof(sample_json) - 1U] = '\0';
    }

    if (first_index >= 0) {
        snprintf(out, out_size,
                 "{\"present\":1,\"buffer_len\":%d,\"current_slot\":%d,"
                 "\"occupied\":%d,\"first\":{\"index\":%d,\"room\":%d,"
                 "\"impact\":%d,\"center\":[%.2f,%.2f,%.2f],"
                 "\"world\":%d,\"world_center\":[%.2f,%.2f,%.2f]},"
                 "\"projection\":{\"valid\":%d,\"float\":%d,\"source\":\"room_matrix_field10e0\","
                 "\"viewport\":[%.2f,%.2f,%.2f,%.2f]},"
                 "\"sample\":%s,\"hash\":\"0x%016llX\"}",
                 BULLET_IMPACT_BUFFER_LEN,
                 g_NumImpactEntries,
                 occupied,
                 first_index,
                 first_room,
                 first_impact,
                 first_center_x, first_center_y, first_center_z,
                 first_has_world,
                 first_world_center_x, first_world_center_y, first_world_center_z,
                 projection_valid,
                 projection_is_float,
                 viewport_left,
                 viewport_top,
                 viewport_width,
                 viewport_height,
                 sample_json,
                 (unsigned long long)hash);
    } else {
        snprintf(out, out_size,
                 "{\"present\":1,\"buffer_len\":%d,\"current_slot\":%d,"
                 "\"occupied\":0,\"first\":{\"index\":-1},\"sample\":[],"
                 "\"projection\":{\"valid\":%d,\"float\":%d,\"source\":\"room_matrix_field10e0\","
                 "\"viewport\":[%.2f,%.2f,%.2f,%.2f]},"
                 "\"hash\":\"0x%016llX\"}",
                 BULLET_IMPACT_BUFFER_LEN,
                 g_NumImpactEntries,
                 projection_valid,
                 projection_is_float,
                 viewport_left,
                 viewport_top,
                 viewport_width,
                 viewport_height,
                 (unsigned long long)hash);
    }
}

static void traceBuildProjectileJson(char *out, size_t out_size)
{
    size_t used = 0;
    int wrote;
    int i;

    if (out_size == 0) {
        return;
    }

    wrote = snprintf(out,
                     out_size,
                     "\"projectile\":{\"motion_count\":%d,\"overflow\":%d,\"motions\":[",
                     s_traceProjectileMotionEventCount,
                     s_traceProjectileMotionOverflow);
    if (wrote < 0 || (size_t)wrote >= out_size) {
        snprintf(out, out_size, "\"projectile\":{\"motion_count\":0,\"overflow\":1,\"motions\":[]},");
        s_traceProjectileMotionEventCount = 0;
        s_traceProjectileMotionOverflow = 0;
        return;
    }

    used = (size_t)wrote;

    for (i = 0; i < s_traceProjectileMotionEventCount; i++) {
        TraceProjectileMotionEvent *event = &s_traceProjectileMotionEvents[i];

        wrote = snprintf(out + used,
                         out_size - used,
                         "%s{\"frame\":%d,\"phase\":\"%s\",\"obj\":%d,\"type\":%d,\"pad\":%d,\"result\":%d,"
                         "\"pos\":[%.2f,%.2f,%.2f],\"prop_pos\":[%.2f,%.2f,%.2f],"
                         "\"hit\":[%.2f,%.2f,%.2f],\"to\":[%.2f,%.2f,%.2f],"
                         "\"normal\":[%.4f,%.4f,%.4f],"
                         "\"hitprop\":{\"type\":%d,\"obj_type\":%d,\"chrnum\":%d,\"obj\":%d,\"item\":%d,"
                         "\"pad\":%d,\"flags\":%d,\"pos\":[%.2f,%.2f,%.2f]},"
                         "\"projflags\":%u,\"rooms\":[%d,%d,%d,%d]}",
                         i == 0 ? "" : ",",
                         event->frame,
                         event->phase,
                         event->obj,
                         event->type,
                         event->pad,
                         event->result,
                         event->pos[0],
                         event->pos[1],
                         event->pos[2],
                         event->prop_pos[0],
                         event->prop_pos[1],
                         event->prop_pos[2],
                         event->hit[0],
                         event->hit[1],
                         event->hit[2],
                         event->to[0],
                         event->to[1],
                         event->to[2],
                         event->normal[0],
                         event->normal[1],
                         event->normal[2],
                         event->hitprop.type,
                         event->hitprop.obj_type,
                         event->hitprop.chrnum,
                         event->hitprop.obj,
                         event->hitprop.item,
                         event->hitprop_pad,
                         event->hitprop_flags,
                         event->hitprop_pos[0],
                         event->hitprop_pos[1],
                         event->hitprop_pos[2],
                         (unsigned int)event->projflags,
                         event->rooms[0],
                         event->rooms[1],
                         event->rooms[2],
                         event->rooms[3]);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            snprintf(out, out_size, "\"projectile\":{\"motion_count\":0,\"overflow\":1,\"motions\":[]},");
            s_traceProjectileMotionEventCount = 0;
            s_traceProjectileMotionOverflow = 0;
            return;
        }

        used += (size_t)wrote;
    }

    wrote = snprintf(out + used, out_size - used, "]},");
    if (wrote < 0 || (size_t)wrote >= out_size - used) {
        snprintf(out, out_size, "\"projectile\":{\"motion_count\":0,\"overflow\":1,\"motions\":[]},");
    }

    s_traceProjectileMotionEventCount = 0;
    s_traceProjectileMotionOverflow = 0;
}

static void traceBuildGuardHitJson(char *out, size_t out_size)
{
    size_t used = 0;
    int wrote;
    int i;

    if (out_size == 0) {
        return;
    }

    wrote = snprintf(out,
                     out_size,
                     "\"hit\":{\"count\":%d,\"overflow\":%d,\"events\":[",
                     s_traceGuardHitEventCount,
                     s_traceGuardHitOverflow);
    if (wrote < 0 || (size_t)wrote >= out_size) {
        snprintf(out, out_size, "\"hit\":{\"count\":0,\"overflow\":1,\"events\":[]},");
        s_traceGuardHitEventCount = 0;
        s_traceGuardHitOverflow = 0;
        return;
    }

    used = (size_t)wrote;

    for (i = 0; i < s_traceGuardHitEventCount; i++) {
        TraceGuardHitEvent *event = &s_traceGuardHitEvents[i];

        wrote = snprintf(out + used,
                         out_size - used,
                         "%s{\"frame\":%d,\"shot_id\":%d,\"phase\":\"%s\",\"chrnum\":%d,"
                         "\"hit\":{\"initial\":%d,\"final\":%d,\"weapon\":%d,\"is_player\":%d,"
                         "\"accepted\":%d,\"reason\":%d,\"hat_action\":%d},"
                         "\"damage\":{\"before\":%.4f,\"after\":%.4f,\"delta\":%.4f,"
                         "\"max\":%.4f,\"lethal\":%d},"
                         "\"state\":{\"action_before\":%d,\"action_after\":%d,"
                         "\"hidden_before\":%d,\"hidden_after\":%d,"
                         "\"chrflags_before\":\"0x%08X\",\"chrflags_after\":\"0x%08X\","
                         "\"numarghs_before\":%d,\"numarghs_after\":%d,"
                         "\"numclose_before\":%d,\"numclose_after\":%d,\"preargh\":%d},"
                         "\"drop\":{\"right\":%d,\"left\":%d,\"hat\":%d},"
                         "\"impact\":{\"target\":%d,\"created\":%d,\"model_pos\":%d},"
                         "\"angle\":%.4f}",
                         i == 0 ? "" : ",",
                         event->frame,
                         event->shot_id,
                         event->phase,
                         event->chrnum,
                         event->initial_hitpart,
                         event->final_hitpart,
                         event->weapon,
                         event->is_player,
                         event->accepted,
                         event->reason,
                         event->hat_action,
                         event->damage_before,
                         event->damage_after,
                         event->damage_delta,
                         event->maxdamage,
                         event->lethal,
                         event->action_before,
                         event->action_after,
                         event->hidden_before,
                         event->hidden_after,
                         (unsigned int)event->chrflags_before,
                         (unsigned int)event->chrflags_after,
                         event->numarghs_before,
                         event->numarghs_after,
                         event->numclose_before,
                         event->numclose_after,
                         event->preargh,
                         event->dropped_right,
                         event->dropped_left,
                         event->dropped_hat,
                         event->impact_target,
                         event->impact_created,
                         event->model_pos,
                         event->angle);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            snprintf(out, out_size, "\"hit\":{\"count\":0,\"overflow\":1,\"events\":[]},");
            s_traceGuardHitEventCount = 0;
            s_traceGuardHitOverflow = 0;
            return;
        }

        used += (size_t)wrote;
    }

    wrote = snprintf(out + used, out_size - used, "]},");
    if (wrote < 0 || (size_t)wrote >= out_size - used) {
        snprintf(out, out_size, "\"hit\":{\"count\":0,\"overflow\":1,\"events\":[]},");
    }

    s_traceGuardHitEventCount = 0;
    s_traceGuardHitOverflow = 0;
}

static void traceBuildForcedGuardHitJson(char *out, size_t out_size)
{
    size_t used = 0;
    int wrote;
    int i;

    if (out_size == 0) {
        return;
    }

    wrote = snprintf(out,
                     out_size,
                     "\"forced_hit\":{\"count\":%d,\"overflow\":%d,\"events\":[",
                     s_traceForcedGuardHitEventCount,
                     s_traceForcedGuardHitOverflow);
    if (wrote < 0 || (size_t)wrote >= out_size) {
        snprintf(out, out_size, "\"forced_hit\":{\"count\":0,\"overflow\":1,\"events\":[]},");
        s_traceForcedGuardHitEventCount = 0;
        s_traceForcedGuardHitOverflow = 0;
        return;
    }

    used = (size_t)wrote;

    for (i = 0; i < s_traceForcedGuardHitEventCount; i++) {
        TraceForcedGuardHitEvent *event = &s_traceForcedGuardHitEvents[i];

        wrote = snprintf(out + used,
                         out_size - used,
                         "%s{\"frame\":%d,\"chrnum\":%d,\"hitpart\":%d,"
                         "\"source\":\"%s\",\"collision\":%d,\"texture\":%d,"
                         "\"mtx_index\":%d,\"queue_node\":%d,\"hit_node\":%d,\"model\":%d,"
                         "\"aim\":{\"valid\":%d,\"origin\":[%.2f,%.2f,%.2f],"
                         "\"point\":[%.2f,%.2f,%.2f],\"dir\":[%.5f,%.5f,%.5f],"
                         "\"world_origin\":[%.2f,%.2f,%.2f],"
                         "\"world_point\":[%.2f,%.2f,%.2f],"
                         "\"world_dir\":[%.5f,%.5f,%.5f]}}",
                         i == 0 ? "" : ",",
                         event->frame,
                         event->chrnum,
                         event->hitpart,
                         event->source,
                         event->collision,
                         event->texture,
                         event->mtx_index,
                         event->queue_node,
                         event->hit_node,
                         event->model,
                         event->aim_valid,
                         event->origin[0],
                         event->origin[1],
                         event->origin[2],
                         event->point[0],
                         event->point[1],
                         event->point[2],
                         event->dir[0],
                         event->dir[1],
                         event->dir[2],
                         event->world_origin[0],
                         event->world_origin[1],
                         event->world_origin[2],
                         event->world_point[0],
                         event->world_point[1],
                         event->world_point[2],
                         event->world_dir[0],
                         event->world_dir[1],
                         event->world_dir[2]);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            snprintf(out, out_size, "\"forced_hit\":{\"count\":0,\"overflow\":1,\"events\":[]},");
            s_traceForcedGuardHitEventCount = 0;
            s_traceForcedGuardHitOverflow = 0;
            return;
        }

        used += (size_t)wrote;
    }

    wrote = snprintf(out + used, out_size - used, "]},");
    if (wrote < 0 || (size_t)wrote >= out_size - used) {
        snprintf(out, out_size, "\"forced_hit\":{\"count\":0,\"overflow\":1,\"events\":[]},");
    }

    s_traceForcedGuardHitEventCount = 0;
    s_traceForcedGuardHitOverflow = 0;
}

static int traceProjectViewmodelPoint(const PortViewmodelTrace *trace,
                                      const f32 pos[3],
                                      f32 fovy,
                                      f32 out_screen[2])
{
    f32 aspect;
    f32 width;
    f32 height;
    f32 left;
    f32 top;
    f32 z;
    f32 focal;
    f32 ndc_x;
    f32 ndc_y;

    if (trace == NULL || pos == NULL || out_screen == NULL) {
        return 0;
    }

    width = trace->render_proj_view[2];
    height = trace->render_proj_view[3];
    left = trace->render_proj_view[0];
    top = trace->render_proj_view[1];
    aspect = trace->render_proj_aspect;
    z = -pos[2];

    if (width <= 0.0f || height <= 0.0f || aspect <= 0.0f ||
        fovy <= 0.0f || z <= 0.001f) {
        out_screen[0] = 0.0f;
        out_screen[1] = 0.0f;
        return 0;
    }

    focal = 1.0f / tanf(fovy * 0.00872664626f);
    if (!isFinite(focal)) {
        out_screen[0] = 0.0f;
        out_screen[1] = 0.0f;
        return 0;
    }

    ndc_x = (pos[0] * focal / aspect) / z;
    ndc_y = (pos[1] * focal) / z;
    out_screen[0] = left + (ndc_x + 1.0f) * 0.5f * width;
    out_screen[1] = top + (1.0f - ndc_y) * 0.5f * height;
    return 1;
}

static void traceFormatViewmodelMtxJson(char *out,
                                        size_t out_size,
                                        const char *key,
                                        const PortViewmodelTrace *trace)
{
    size_t used = 0;
    int wrote;
    s32 i;
    s32 sampled;

    if (out == NULL || out_size == 0 || key == NULL || trace == NULL) {
        return;
    }

    out[0] = '\0';
    sampled = trace->render_mtx_sampled;
    if (sampled < 0) {
        sampled = 0;
    }
    if (sampled > PORT_VIEWMODEL_TRACE_MTX_SAMPLES) {
        sampled = PORT_VIEWMODEL_TRACE_MTX_SAMPLES;
    }

    wrote = snprintf(out, out_size,
                     "\"%s\":{\"count\":%d,\"sampled\":%d,"
                     "\"projection\":{\"fovy\":%.4f,\"aspect\":%.6f,"
                     "\"view\":[%.2f,%.2f,%.2f,%.2f]},"
                     "\"pos\":[",
                     key,
                     trace->render_mtx_count,
                     sampled,
                     trace->render_proj_fovy,
                     trace->render_proj_aspect,
                     trace->render_proj_view[0],
                     trace->render_proj_view[1],
                     trace->render_proj_view[2],
                     trace->render_proj_view[3]);
    if (wrote < 0 || (size_t)wrote >= out_size) {
        snprintf(out, out_size, "\"%s\":{\"overflow\":1},", key);
        return;
    }
    used = (size_t)wrote;

    for (i = 0; i < sampled; i++) {
        const f32 *pos = trace->render_mtx[i];
        wrote = snprintf(out + used, out_size - used,
                         "%s[%.2f,%.2f,%.2f]",
                         i == 0 ? "" : ",",
                         pos[0], pos[1], pos[2]);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            snprintf(out, out_size, "\"%s\":{\"overflow\":1},", key);
            return;
        }
        used += (size_t)wrote;
    }

    wrote = snprintf(out + used, out_size - used, "],\"screen\":[");
    if (wrote < 0 || (size_t)wrote >= out_size - used) {
        snprintf(out, out_size, "\"%s\":{\"overflow\":1},", key);
        return;
    }
    used += (size_t)wrote;

    for (i = 0; i < sampled; i++) {
        f32 screen[2];
        int valid = traceProjectViewmodelPoint(trace, trace->render_mtx[i],
                                               trace->render_proj_fovy, screen);
        wrote = snprintf(out + used, out_size - used,
                         "%s{\"valid\":%d,\"xy\":[%.2f,%.2f]}",
                         i == 0 ? "" : ",",
                         valid, screen[0], screen[1]);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            snprintf(out, out_size, "\"%s\":{\"overflow\":1},", key);
            return;
        }
        used += (size_t)wrote;
    }

    wrote = snprintf(out + used, out_size - used, "],\"screen50\":[");
    if (wrote < 0 || (size_t)wrote >= out_size - used) {
        snprintf(out, out_size, "\"%s\":{\"overflow\":1},", key);
        return;
    }
    used += (size_t)wrote;

    for (i = 0; i < sampled; i++) {
        f32 screen[2];
        int valid = traceProjectViewmodelPoint(trace, trace->render_mtx[i],
                                               50.0f, screen);
        wrote = snprintf(out + used, out_size - used,
                         "%s{\"valid\":%d,\"xy\":[%.2f,%.2f]}",
                         i == 0 ? "" : ",",
                         valid, screen[0], screen[1]);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            snprintf(out, out_size, "\"%s\":{\"overflow\":1},", key);
            return;
        }
        used += (size_t)wrote;
    }

    wrote = snprintf(out + used, out_size - used, "]},");
    if (wrote < 0 || (size_t)wrote >= out_size - used) {
        snprintf(out, out_size, "\"%s\":{\"overflow\":1},", key);
    }
}

/* M1.4/R8: projected viewer-body geometry for the pixel-level intro validator.
 * Emits, for the current-player intro chr, the world root (feet) position, the
 * floor Y beneath it (grounding reference), the model height, and the body's
 * screen-space bbox -- projected with the same world->screen recipe the renderer
 * uses (world - current_model_pos, then the field_10E0 view/projection matrix;
 * see traceProjectRoomLocalQuad). The bbox is an axis-aligned world box around
 * the standing body projected corner-by-corner: it is a coverage/outlier gate
 * for the screenshot analyzer, not an exact joint hull. render_pos_count carries
 * the joint count so the analyzer can also gate on the de-alias render-position
 * count. */
static void traceBuildIntroBondBodyJson(char *out, size_t out_size, ChrRecord *intro_chr) {
    extern f32 sub_GAME_7F06C768(Model *objinst);
    float projection[4][4];
    int projection_valid = 0;
    int projection_is_float = 0;
    float vp_l = 0.0f, vp_t = 0.0f, vp_w = 320.0f, vp_h = 240.0f;
    float min_x = FLT_MAX, min_y = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX;
    float root_x, root_y, root_z;
    float root_y_report;
    float floor_y = 0.0f;
    int floor_valid = 0;
    float height = 0.0f;
    int render_pos_count = 0;
    int behind = 0;
    int onscreen = 0;
    int corner;
    /* projected root (feet) and head points for the grounding readout */
    float root_sx = 0.0f, root_sy = 0.0f, head_sx = 0.0f, head_sy = 0.0f;
    int root_screen_valid = 0, head_screen_valid = 0;

    if (out_size == 0) {
        return;
    }
    if (intro_chr == NULL || intro_chr->prop == NULL || intro_chr->model == NULL
        || g_CurrentPlayer == NULL) {
        snprintf(out, out_size, "{\"present\":0}");
        return;
    }

    /* Grounding fault hook (M1.4 negative control): GE007_INTRO_BODY_Y_OFFSET
     * adds a fixed Y to the REPORTED world_root only -- not to the projected
     * bbox -- so the validator's grounding check (world_root.y vs floor_y) can be
     * driven to fail on demand while presence/shard (which read the bbox) still
     * pass, proving the three checks are independent. Default 0 = no effect. */
    static int intro_body_yoff_init = 0;
    static float intro_body_yoff = 0.0f;
    if (!intro_body_yoff_init) {
        const char *yoff_env = getenv("GE007_INTRO_BODY_Y_OFFSET");
        intro_body_yoff = (yoff_env != NULL) ? (float)atof(yoff_env) : 0.0f;
        intro_body_yoff_init = 1;
    }

    root_x = intro_chr->prop->pos.x;
    root_y = intro_chr->prop->pos.y;
    root_z = intro_chr->prop->pos.z;
    root_y_report = root_y + intro_body_yoff;
    render_pos_count = modelGetRenderPosCount(intro_chr->model);
    height = sub_GAME_7F06C768(intro_chr->model);
    if (!__builtin_isfinite(height) || height <= 0.0f) {
        height = 180.0f;
    }

    if (intro_chr->prop->stan != NULL) {
        floor_y = stanGetPositionYValue(intro_chr->prop->stan, root_x, root_z);
        floor_valid = __builtin_isfinite(floor_y);
    }

    vp_l = g_CurrentPlayer->c_screenleft;
    vp_t = g_CurrentPlayer->c_screentop;
    vp_w = g_CurrentPlayer->c_screenwidth;
    vp_h = g_CurrentPlayer->c_screenheight;

    traceLoadCurrentField10E0(projection, &projection_valid, &projection_is_float);
    if (!projection_valid) {
        int wrote = snprintf(out, out_size,
                 "{\"present\":1,\"projected\":0,\"render_pos_count\":%d,"
                 "\"world_root\":[%.2f,%.2f,%.2f],\"floor_y\":%.2f,\"floor_valid\":%d,"
                 "\"height\":%.2f}",
                 render_pos_count, root_x, root_y_report, root_z, floor_y, floor_valid, height);
        if (wrote < 0 || (size_t)wrote >= out_size) {
            snprintf(out, out_size, "{\"present\":1,\"overflow\":1}");
        }
        return;
    }

    /* Diagnostic-only screen bbox from the per-joint render_pos translations.
     * Measured on the Dam frozen-intro camera these matrices sit in a space that
     * does NOT match field_10E0 (the bbox projects off-screen while the world
     * prop root projects near Bond), so screen_bbox is approximate/unreliable
     * there and the validator does not consume it -- presence uses an
     * empirically-measured route fixture and grounding uses the world-space
     * root/floor values below. Kept because the joint spread is still a useful
     * shred/collapse signal relative to itself frame-over-frame. Root(feet) and
     * head-top screen points come from the world prop position instead. */
    {
        RenderPosView *rp = intro_chr->model->render_pos;
        int ji;
        int jcount = (render_pos_count > 64) ? 64 : render_pos_count;
        for (ji = 0; rp != NULL && ji < jcount; ji++) {
            float clip[4], ndc_x, ndc_y, sx, sy;
            traceTransform4x4(projection, rp[ji].pos.m[3][0], rp[ji].pos.m[3][1],
                              rp[ji].pos.m[3][2], 1.0f, clip);
            if (!__builtin_isfinite(clip[0]) || !__builtin_isfinite(clip[1])
                || !__builtin_isfinite(clip[3]) || fabsf(clip[3]) < 0.000001f) {
                continue;
            }
            if (clip[3] <= 0.0f) {
                behind = 1;
            }
            ndc_x = clip[0] / clip[3];
            ndc_y = clip[1] / clip[3];
            sx = vp_l + (ndc_x * 0.5f + 0.5f) * vp_w;
            sy = vp_t + (0.5f - ndc_y * 0.5f) * vp_h;
            if (!__builtin_isfinite(sx) || !__builtin_isfinite(sy)) {
                continue;
            }
            if (sx < min_x) min_x = sx;
            if (sy < min_y) min_y = sy;
            if (sx > max_x) max_x = sx;
            if (sy > max_y) max_y = sy;
        }
    }

    for (corner = 8; corner < 10; corner++) {
        float wy = (corner == 8) ? root_y : (root_y + height);
        float mx = root_x - g_CurrentPlayer->current_model_pos.f[0];
        float my = wy - g_CurrentPlayer->current_model_pos.f[1];
        float mz = root_z - g_CurrentPlayer->current_model_pos.f[2];
        float clip[4], ndc_x, ndc_y, sx, sy;
        traceTransform4x4(projection, mx, my, mz, 1.0f, clip);
        if (!__builtin_isfinite(clip[0]) || !__builtin_isfinite(clip[1])
            || !__builtin_isfinite(clip[3]) || fabsf(clip[3]) < 0.000001f) {
            continue;
        }
        ndc_x = clip[0] / clip[3];
        ndc_y = clip[1] / clip[3];
        sx = vp_l + (ndc_x * 0.5f + 0.5f) * vp_w;
        sy = vp_t + (0.5f - ndc_y * 0.5f) * vp_h;
        if (!__builtin_isfinite(sx) || !__builtin_isfinite(sy)) {
            continue;
        }
        if (corner == 8) {
            root_sx = sx; root_sy = sy; root_screen_valid = 1;
        } else {
            head_sx = sx; head_sy = sy; head_screen_valid = 1;
        }
    }

    if (min_x == FLT_MAX || max_x == -FLT_MAX) {
        int wrote = snprintf(out, out_size,
                 "{\"present\":1,\"projected\":0,\"behind\":%d,\"render_pos_count\":%d,"
                 "\"world_root\":[%.2f,%.2f,%.2f],\"floor_y\":%.2f,\"floor_valid\":%d,"
                 "\"height\":%.2f}",
                 behind, render_pos_count, root_x, root_y_report, root_z, floor_y, floor_valid, height);
        if (wrote < 0 || (size_t)wrote >= out_size) {
            snprintf(out, out_size, "{\"present\":1,\"overflow\":1}");
        }
        return;
    }

    onscreen = (max_x >= vp_l && max_y >= vp_t
                && min_x <= vp_l + vp_w && min_y <= vp_t + vp_h);

    /* Truncation guard (house pattern, see traceFormatViewmodelMtxJson): this
     * fragment is spliced into the trace line via %s, so a truncated payload
     * would embed unbalanced JSON and corrupt the whole record for every trace
     * consumer. Off-screen joint projections can produce very wide %.2f values. */
    {
        int wrote = snprintf(out, out_size,
                 "{\"present\":1,\"projected\":1,\"behind\":%d,\"onscreen\":%d,"
                 "\"render_pos_count\":%d,\"world_root\":[%.2f,%.2f,%.2f],"
                 "\"floor_y\":%.2f,\"floor_valid\":%d,\"height\":%.2f,"
                 "\"screen_bbox\":[%.2f,%.2f,%.2f,%.2f],"
                 "\"root_screen\":[%.2f,%.2f],\"root_screen_valid\":%d,"
                 "\"head_screen\":[%.2f,%.2f],\"head_screen_valid\":%d,"
                 "\"vp\":[%.2f,%.2f,%.2f,%.2f]}",
                 behind, onscreen, render_pos_count, root_x, root_y_report, root_z,
                 floor_y, floor_valid, height,
                 min_x, min_y, max_x, max_y,
                 root_sx, root_sy, root_screen_valid,
                 head_sx, head_sy, head_screen_valid,
                 vp_l, vp_t, vp_w, vp_h);
        if (wrote < 0 || (size_t)wrote >= out_size) {
            snprintf(out, out_size, "{\"present\":1,\"overflow\":1}");
        }
    }
}

void portTraceFrame(void) {
    /* Fast bail: if tracing was never requested, do absolutely nothing.
     * This avoids touching any state that could be corrupted by DL overruns. */
    if (!g_traceStatePath && s_traceFd < 0 && !traceGuardOracleEnabled() &&
        !traceMissionGateDumpEnabled() && s_missionGateDumpFile == NULL &&
        !traceStagePadDumpEnabled() && s_stagePadDumpFile == NULL &&
        !traceStageChrDumpEnabled() && s_stageChrDumpFile == NULL) return;

    if (s_traceFrame == 0 && s_traceFd < 0 && s_missionGateDumpFile == NULL &&
        s_stagePadDumpFile == NULL && s_stageChrDumpFile == NULL &&
        (g_traceStatePath != NULL || traceGuardOracleEnabled() ||
         traceMissionGateDumpEnabled() || traceStagePadDumpEnabled() || traceStageChrDumpEnabled())) {
        traceInit();
    }
    s_traceFrame++;
    traceMissionGateDumpOnce();
    traceStagePadDumpOnce();
    traceStageChrDumpOnce();

    if (s_traceFd < 0) return;

    /* Player state — use offsets from struct player via known field positions.
     * g_CurrentPlayer may be NULL early in boot. */
    float px = 0, py = 0, pz = 0;
    float cam_x = 0, cam_y = 0, cam_z = 0;
    float cam_target_x = 0, cam_target_y = 0, cam_target_z = 0;
    float cam_up_x = 0, cam_up_y = 0, cam_up_z = 0;
    float cam_floor_x = 0, cam_floor_y = 0, cam_floor_z = 0;
    float render_cam_x = 0, render_cam_y = 0, render_cam_z = 0;
    float render_cam_target_x = 0, render_cam_target_y = 0, render_cam_target_z = 0;
    float render_cam_dx = 0, render_cam_dy = 0, render_cam_dz = 0;
    float room_basis_x = 0, room_basis_y = 0, room_basis_z = 0;
    float vv_theta = 0, vv_verta = 0, vv_verta360 = 0;
    float vv_cosverta = 0, vv_sinverta = 0;
    float field_70 = 0, stanHeight = 0;
    float col_x = 0, col_y = 0, col_z = 0;
    float cam_dx = 0, cam_dy = 0, cam_dz = 0;
    float theta0 = 0, theta1 = 0, theta2 = 0;
    float headlook_x = 0.0f, headlook_y = 0.0f, headlook_z = 0.0f;
    float headup_x = 0.0f, headup_y = 0.0f, headup_z = 0.0f;
    float move_speed_forwards = 0.0f, move_speed_sideways = 0.0f;
    float move_speed_go = 0.0f, move_speed_strafe = 0.0f, move_speed_boost = 0.0f;
    float move_speed_theta = 0.0f, move_speed_verta = 0.0f;
    float move_head_x = 0.0f, move_head_y = 0.0f, move_head_z = 0.0f;
    float move_prev_x = 0.0f, move_prev_y = 0.0f, move_prev_z = 0.0f;
    int move_speed_max_time60 = 0;
    int tile_room = -1, portal_room = -1, room_ptr_room = -1, render_room = -1, prop_room = -1;
    int lookup_room = -1, nearest_room = -1;
    int cam_lookup_room = -1, cam_nearest_room = -1;
    int cur_room = -1;
    int rendered_rooms_count = 0;
    int neighbor_rooms_count = 0;
    int loaded_rooms_count = 0;
    int room_render_fallback_active = 0;
    int room_render_fallback_rooms = 0;
    int room_render_fallback_total = 0;
    int rendered_rooms_sample[204];
    int rendered_rooms_sample_count = 0;
    int has_player = 0;
    /* 1-based current player number for the "p" field. 0 = no current player.
     * In split-screen MP this lets the trace distinguish per-player viewpoints
     * (the per-player move/render loop leaves g_CurrentPlayer on the player
     * whose state this frame reflects). */
    int trace_player_num = 0;
    int player_unknown = 0;
    int view_left = 0, view_top = 0, view_w = 0, view_h = 0;
    int vi_view_left = 0, vi_view_top = 0, vi_view_w = 0, vi_view_h = 0;
    int watch_state = 0;
    int watch_page = -1;
    int watch_brief = -1;
    int watch_pause = 0;
    int watch_open_req = 0;
    int pausing = 0;
    int hand_ready_left = 0;
    int hand_ready_right = 0;
    int hand_item_left = 0;
    int hand_item_right = 0;
    int hand_active_left = 0;
    int hand_active_right = 0;
    int hand_weapon_left = 0;
    int hand_weapon_right = 0;
    int hand_next_left = -1;
    int hand_next_right = -1;
    int hand_pending_left = 0;
    int hand_pending_right = 0;
    int hand_invis_left = 0;
    int hand_invis_right = 0;
    int det_state_right = 0;
    int force_disarm = 0;
    int aim_mode = 0;
    int autoaim_opt = 0;
    int autoaim_enabled_x = 0;
    int autoaim_enabled_y = 0;
    int autoaim_same_target = 0;
    int gunsightmode = 0;
    int autoaim_target_x_type = -1;
    int autoaim_target_x_chrnum = -1;
    int autoaim_target_x_hidden = 0;
    int autoaim_target_x_alive = 0;
    int autoaim_target_x_actiontype = -1;
    int autoaim_target_x_alertness = -1;
    int autoaim_target_x_firecount = 0;
    int autoaim_target_x_hidden_bits = 0;
    int autoaim_target_x_sleep = 0;
    int autoaim_target_x_ai_list = -1;
    int autoaim_target_x_ai_global = 0;
    int autoaim_target_x_ai_offset = -1;
    int autoaim_target_x_ai_return = -1;
    int autoaim_target_x_ai_cmd = -1;
    int autoaim_target_x_ai_arg0 = -1;
    int autoaim_target_x_seen_age = -1;
    int autoaim_target_x_heard_age = -1;
    int autoaim_target_x_seen_recent = 0;
    int autoaim_target_x_heard_recent = 0;
    int autoaim_target_x_line_clear = 0;
    int autoaim_target_x_same_stan = 0;
    int autoaim_target_x_could_see_bond = 0;
    int autoaim_target_y_type = -1;
    int autoaim_target_y_chrnum = -1;
    int autoaim_target_y_hidden = 0;
    int autoaim_target_y_alive = 0;
    int autoaim_target_y_actiontype = -1;
    int autoaim_target_y_alertness = -1;
    int autoaim_target_y_firecount = 0;
    int autoaim_target_y_hidden_bits = 0;
    int autoaim_target_y_sleep = 0;
    int autoaim_target_y_ai_list = -1;
    int autoaim_target_y_ai_global = 0;
    int autoaim_target_y_ai_offset = -1;
    int autoaim_target_y_ai_return = -1;
    int autoaim_target_y_ai_cmd = -1;
    int autoaim_target_y_ai_arg0 = -1;
    int autoaim_target_y_seen_age = -1;
    int autoaim_target_y_heard_age = -1;
    int autoaim_target_y_seen_recent = 0;
    int autoaim_target_y_heard_recent = 0;
    int autoaim_target_y_line_clear = 0;
    int autoaim_target_y_same_stan = 0;
    int autoaim_target_y_could_see_bond = 0;
    int nearest_guard_type = -1;
    int nearest_guard_chrnum = -1;
    int nearest_guard_hidden = 0;
    int nearest_guard_alive = 0;
    int nearest_guard_actiontype = -1;
    int nearest_guard_alertness = -1;
    int nearest_guard_firecount = 0;
    int nearest_guard_hidden_bits = 0;
    int nearest_guard_sleep = 0;
    int nearest_guard_ai_list = -1;
    int nearest_guard_ai_global = 0;
    int nearest_guard_ai_offset = -1;
    int nearest_guard_ai_return = -1;
    int nearest_guard_ai_cmd = -1;
    int nearest_guard_ai_arg0 = -1;
    int nearest_guard_seen_age = -1;
    int nearest_guard_heard_age = -1;
    int nearest_guard_seen_recent = 0;
    int nearest_guard_heard_recent = 0;
    int nearest_guard_line_clear = 0;
    int nearest_guard_same_stan = 0;
    int nearest_guard_could_see_bond = 0;
    int nearest_guard_line_clear_world = 0;
    int nearest_guard_line_clear_solid = 0;
    int nearest_guard_blocker_type = -1;
    int nearest_guard_blocker_chrnum = -1;
    int nearest_guard_blocker_self = 0;
    int autoxaimtime60 = -1;
    int autoyaimtime60 = -1;
    int damageshowtime = -1;
    int healthshowtime = -1;
    int damagetype = -1;
    int health_damage_type = -1;
    int colour_screen_red = 0;
    int colour_screen_green = 0;
    int colour_screen_blue = 0;
    int shot_count_total = 0;
    int shot_counts[7] = {0};
    float autoaimx = 0.0f;
    float autoaimy = 0.0f;
    float crosshair_x = 0.0f;
    float crosshair_y = 0.0f;
    float gunsight_x = 0.0f;
    float gunsight_y = 0.0f;
    float bondhealth = 0.0f;
    float bondarmour = 0.0f;
    float actual_health = 0.0f;
    float actual_armor = 0.0f;
    float colour_screen_frac = 0.0f;
    float autoaim_target_x_damage = 0.0f;
    float autoaim_target_x_maxdamage = 0.0f;
    float autoaim_target_x_distance = 0.0f;
    float autoaim_target_x_pos_x = 0.0f;
    float autoaim_target_x_pos_y = 0.0f;
    float autoaim_target_x_pos_z = 0.0f;
    float autoaim_target_x_delta_x = 0.0f;
    float autoaim_target_x_delta_y = 0.0f;
    float autoaim_target_x_delta_z = 0.0f;
    float autoaim_target_y_damage = 0.0f;
    float autoaim_target_y_maxdamage = 0.0f;
    float autoaim_target_y_distance = 0.0f;
    float autoaim_target_y_pos_x = 0.0f;
    float autoaim_target_y_pos_y = 0.0f;
    float autoaim_target_y_pos_z = 0.0f;
    float autoaim_target_y_delta_x = 0.0f;
    float autoaim_target_y_delta_y = 0.0f;
    float autoaim_target_y_delta_z = 0.0f;
    float nearest_guard_damage = 0.0f;
    float nearest_guard_maxdamage = 0.0f;
    float nearest_guard_distance = 0.0f;
    float nearest_guard_pos_x = 0.0f;
    float nearest_guard_pos_y = 0.0f;
    float nearest_guard_pos_z = 0.0f;
    float nearest_guard_delta_x = 0.0f;
    float nearest_guard_delta_y = 0.0f;
    float nearest_guard_delta_z = 0.0f;
    float nearest_guard_blocker_distance = 0.0f;
    float tank_world_x = 0.0f;
    float tank_world_y = 0.0f;
    float tank_world_z = 0.0f;
    float tank_player_x = 0.0f;
    float tank_player_y = 0.0f;
    float tank_player_z = 0.0f;
    int track_autoaim_x = 0;
    int track_autoaim_y = 0;
    int casing_count = 0;
    int tank_in = 0;
    int tank_can_enter = 0;
    int tank_world_present = 0;
    int tank_player_present = 0;
    int tank_world_obj = -1;
    int tank_world_pad = -1;
    int tank_player_obj = -1;
    int tank_player_pad = -1;
    int tank_stored_ammo = -1;
    int tank_reserve_ammo = -1;
    int tank_firing = 0;
    int inv_item_count = 0;
    int inv_has_ge_key = 0;
    int inv_copied_ge_key = 0;
    int objective_count_trace = 0;
    int objective_all_complete = 0;
    u32 inv_keyflags = 0;
    u32 snd_stub_hits_total = 0;
    u32 snd_stub_hits_play = 0;
    u32 snd_stub_legacy_overlap_hits = 0;
    u32 snd_stub_new_player_init = 0;
    u32 snd_stub_get_playing_state = 0;
    u32 snd_stub_deactivate = 0;
    u32 snd_stub_deactivate_all_1 = 0;
    u32 snd_stub_deactivate_all_11 = 0;
    u32 snd_stub_deactivate_all_3 = 0;
    u32 snd_stub_create_post_event = 0;
    u32 snd_stub_dispose_sound = 0;
    u32 snd_stub_set_priority = 0;
    u32 snd_stub_unlink_clear = 0;
    u32 snd_stub_count_alloc_list = 0;
    int mp_time_ticks = 0;
    int mp_player_count = 0;
    char objective_statuses_json[64];
    char objective_text_ids_json[96];
    char objective_difficulties_json[64];
    char objective_field_json[256];
    char tracked_chr_field_json[6144];
    char actor_summary_json[4096];
    char combat_oracle_json[16384];
    char glass_state_json[4096];
    char glass_projection_json[65536];
    char alarm_field_json[64];
    char spawn_field_json[4096];
    char shot_field_json[16384];
    char impact_field_json[32768];
    char impact_state_json[32768];
    char projectile_field_json[4096];
    char hit_field_json[4096];
    char forced_hit_field_json[4096];
    char drop_field_json[TRACE_GUARD_DROP_JSON_SIZE];
    char glass_props_json[16384];
    char save_field_json[512];
    char ramrom_field_json[1024];
    char weapon_right_mtx_json[4096];
    char weapon_left_mtx_json[4096];
    PortViewmodelTrace weapon_left;
    PortViewmodelTrace weapon_right;
    const PropRecord *nearest_guard_for_trace = NULL;
    int trace_live_stage_globals = traceLiveStageGlobalsSafe();

    objective_statuses_json[0] = '[';
    objective_statuses_json[1] = ']';
    objective_statuses_json[2] = '\0';
    objective_text_ids_json[0] = '[';
    objective_text_ids_json[1] = ']';
    objective_text_ids_json[2] = '\0';
    objective_difficulties_json[0] = '[';
    objective_difficulties_json[1] = ']';
    objective_difficulties_json[2] = '\0';
    objective_field_json[0] = '\0';
    tracked_chr_field_json[0] = '\0';
    snprintf(actor_summary_json, sizeof(actor_summary_json),
             "{\"slots\":0,\"live\":0,\"alive\":0,\"hidden\":0,\"onscreen\":0,\"rendered\":0,\"sample\":[]}");
    snprintf(combat_oracle_json, sizeof(combat_oracle_json),
             "\"combat_oracle\":{\"guards\":[],\"guards_overflow\":0,"
             "\"floor\":{\"stan_id\":-1,\"stan_room\":-1,\"stan_flags\":-1,\"height\":0.00},"
             "\"combat\":{\"player_health\":0.0000,\"player_armor\":0.0000,"
             "\"shots_fired_total\":0,\"hits_landed_total\":0,\"rng_seed\":\"0x00000000\"},"
             "\"projectiles\":[],\"projectiles_overflow\":0},");
    glass_state_json[0] = '\0';
    glass_projection_json[0] = '\0';
    alarm_field_json[0] = '\0';
    spawn_field_json[0] = '\0';
    shot_field_json[0] = '\0';
    impact_field_json[0] = '\0';
    impact_state_json[0] = '\0';
    projectile_field_json[0] = '\0';
    hit_field_json[0] = '\0';
    forced_hit_field_json[0] = '\0';
    drop_field_json[0] = '\0';
    glass_props_json[0] = '\0';
    save_field_json[0] = '\0';
    ramrom_field_json[0] = '\0';
    weapon_right_mtx_json[0] = '\0';
    weapon_left_mtx_json[0] = '\0';

    {
        int folder;
        size_t valid_out = 0;
        size_t level_out = 0;
        size_t difficulty_out = 0;
        char valid_json[32];
        char level_json[64];
        char difficulty_json[64];

        valid_json[0] = '[';
        level_json[0] = '[';
        difficulty_json[0] = '[';
        valid_out = 1;
        level_out = 1;
        difficulty_out = 1;

        for (folder = FOLDER1; folder < MAX_FOLDER_COUNT; folder++) {
            int wrote;
            int valid = fileGetSaveForFoldernum((u32)folder) != NULL ? 1 : 0;
            LEVEL_SOLO_SEQUENCE level = SP_LEVEL_NONE;
            DIFFICULTY difficulty = DIFFICULTY_MULTI;

            fileGetHighestStageDifficultyCompletedForFolder(folder, &level, &difficulty);

            wrote = snprintf(valid_json + valid_out,
                             sizeof(valid_json) - valid_out,
                             "%s%d",
                             folder == FOLDER1 ? "" : ",",
                             valid);
            if (wrote < 0 || (size_t)wrote >= sizeof(valid_json) - valid_out) {
                break;
            }
            valid_out += (size_t)wrote;

            wrote = snprintf(level_json + level_out,
                             sizeof(level_json) - level_out,
                             "%s%d",
                             folder == FOLDER1 ? "" : ",",
                             (int)level);
            if (wrote < 0 || (size_t)wrote >= sizeof(level_json) - level_out) {
                break;
            }
            level_out += (size_t)wrote;

            wrote = snprintf(difficulty_json + difficulty_out,
                             sizeof(difficulty_json) - difficulty_out,
                             "%s%d",
                             folder == FOLDER1 ? "" : ",",
                             (int)difficulty);
            if (wrote < 0 || (size_t)wrote >= sizeof(difficulty_json) - difficulty_out) {
                break;
            }
            difficulty_out += (size_t)wrote;
        }

        if (valid_out + 2 <= sizeof(valid_json)) {
            valid_json[valid_out++] = ']';
            valid_json[valid_out] = '\0';
        } else {
            valid_json[sizeof(valid_json) - 2] = ']';
            valid_json[sizeof(valid_json) - 1] = '\0';
        }

        if (level_out + 2 <= sizeof(level_json)) {
            level_json[level_out++] = ']';
            level_json[level_out] = '\0';
        } else {
            level_json[sizeof(level_json) - 2] = ']';
            level_json[sizeof(level_json) - 1] = '\0';
        }

        if (difficulty_out + 2 <= sizeof(difficulty_json)) {
            difficulty_json[difficulty_out++] = ']';
            difficulty_json[difficulty_out] = '\0';
        } else {
            difficulty_json[sizeof(difficulty_json) - 2] = ']';
            difficulty_json[sizeof(difficulty_json) - 1] = '\0';
        }

        snprintf(save_field_json,
                 sizeof(save_field_json),
                 "\"save\":{\"valid\":%s,\"level\":%s,\"difficulty\":%s,"
                 "\"copy_count\":%d,\"copy_source\":%d,\"copy_target\":%d,\"copy_result\":%d,"
                 "\"delete_count\":%d,\"delete_folder\":%d,\"delete_result\":%d},",
                 valid_json,
                 level_json,
                 difficulty_json,
#ifdef NATIVE_PORT
                 port_save_copy_count,
                 port_save_copy_source,
                 port_save_copy_target,
                 port_save_copy_result,
                 port_save_delete_count,
                 port_save_delete_folder,
                 port_save_delete_result
#else
                 0,
                 -1,
                 -1,
                 0,
                 0,
                 -1,
                 0
#endif
                 );
    }

    if (trace_live_stage_globals && g_CurrentPlayer) {
        int frozen_intro_camera = playerHasFrozenIntroCamera(g_CurrentPlayer);

        /* Access via struct — these are the canonical fields */
        has_player = 1;
        trace_player_num = get_cur_playernum() + 1;
        player_unknown = g_CurrentPlayer->unknown;
        if (g_CurrentPlayer->prop) {
            px = g_CurrentPlayer->prop->pos.x;
            py = g_CurrentPlayer->prop->pos.y;
            pz = g_CurrentPlayer->prop->pos.z;
        }
        vv_theta = g_CurrentPlayer->vv_theta;
        vv_verta = g_CurrentPlayer->vv_verta;
        vv_verta360 = g_CurrentPlayer->vv_verta360;
        vv_cosverta = g_CurrentPlayer->vv_cosverta;
        vv_sinverta = g_CurrentPlayer->vv_sinverta;
        field_70 = g_CurrentPlayer->field_70;
        stanHeight = g_CurrentPlayer->stanHeight;
        view_left = g_CurrentPlayer->viewleft;
        view_top = g_CurrentPlayer->viewtop;
        view_w = g_CurrentPlayer->viewx;
        view_h = g_CurrentPlayer->viewy;
        watch_state = g_CurrentPlayer->watch_animation_state;
        watch_page = (int)watch_screen_index;
        watch_brief = mission_brief_index;
        watch_pause = g_CurrentPlayer->watch_pause_time;
        watch_open_req = g_CurrentPlayer->open_close_solo_watch_menu;
        pausing = g_CurrentPlayer->pausing_flag;
        hand_ready_left = Gun_hand_without_item(GUNLEFT);
        hand_ready_right = Gun_hand_without_item(GUNRIGHT);
        hand_item_left = g_CurrentPlayer->hand_item[GUNLEFT];
        hand_item_right = g_CurrentPlayer->hand_item[GUNRIGHT];
        hand_active_left = get_item_in_hand_or_watch_menu(GUNLEFT);
        hand_active_right = get_item_in_hand_or_watch_menu(GUNRIGHT);
        hand_weapon_left = g_CurrentPlayer->hands[GUNLEFT].weaponnum;
        hand_weapon_right = g_CurrentPlayer->hands[GUNRIGHT].weaponnum;
        hand_next_left = g_CurrentPlayer->hands[GUNLEFT].weapon_next_weapon;
        hand_next_right = g_CurrentPlayer->hands[GUNRIGHT].weapon_next_weapon;
        hand_pending_left = g_CurrentPlayer->field_2A44[GUNLEFT];
        hand_pending_right = g_CurrentPlayer->field_2A44[GUNRIGHT];
        hand_invis_left = g_CurrentPlayer->hand_invisible[GUNLEFT];
        hand_invis_right = g_CurrentPlayer->hand_invisible[GUNRIGHT];
        det_state_right = g_CurrentPlayer->hands[GUNRIGHT].when_detonating_mines_is_0;
        force_disarm = g_bondviewForceDisarm;
        aim_mode = g_CurrentPlayer->insightaimmode;
        gunsightmode = g_CurrentPlayer->gunsightmode;
        autoaim_opt = g_playerPerm ? g_playerPerm->autoaim : 0;
        autoaim_enabled_x = g_CurrentPlayer->autoxaimenabled;
        autoaim_enabled_y = g_CurrentPlayer->autoyaimenabled;
        autoaimx = g_CurrentPlayer->autoaimx;
        autoaimy = g_CurrentPlayer->autoaimy;
        crosshair_x = g_CurrentPlayer->crosshair_angle.f[0];
        crosshair_y = g_CurrentPlayer->crosshair_angle.f[1];
        gunsight_x = g_CurrentPlayer->field_FFC.x;
        gunsight_y = g_CurrentPlayer->field_FFC.y;
        autoxaimtime60 = g_CurrentPlayer->autoxaimtime60;
        autoyaimtime60 = g_CurrentPlayer->autoyaimtime60;
        track_autoaim_x = (g_CurrentPlayer->autoaim_target_x != NULL) &&
                          (g_CurrentPlayer->autoxaimtime60 >= 0 || fabsf(g_CurrentPlayer->autoaimx) > 0.0001f);
        track_autoaim_y = (g_CurrentPlayer->autoaim_target_y != NULL) &&
                          (g_CurrentPlayer->autoyaimtime60 >= 0 || fabsf(g_CurrentPlayer->autoaimy) > 0.0001f);
        autoaim_same_target = track_autoaim_x && track_autoaim_y &&
                               g_CurrentPlayer->autoaim_target_x == g_CurrentPlayer->autoaim_target_y;
        bondhealth = g_CurrentPlayer->bondhealth;
        bondarmour = g_CurrentPlayer->bondarmour;
        actual_health = g_CurrentPlayer->actual_health;
        actual_armor = g_CurrentPlayer->actual_armor;
        damageshowtime = g_CurrentPlayer->damageshowtime;
        healthshowtime = g_CurrentPlayer->healthshowtime;
        damagetype = g_CurrentPlayer->damagetype;
        health_damage_type = g_CurrentPlayer->healthDamageType;
        colour_screen_red = g_CurrentPlayer->colourscreenred;
        colour_screen_green = g_CurrentPlayer->colourscreengreen;
        colour_screen_blue = g_CurrentPlayer->colourscreenblue;
        colour_screen_frac = g_CurrentPlayer->colourscreenfrac;
        col_x = g_CurrentPlayer->field_488.collision_position.x;
        col_y = g_CurrentPlayer->field_488.collision_position.y;
        col_z = g_CurrentPlayer->field_488.collision_position.z;
        move_speed_forwards = g_CurrentPlayer->speedforwards;
        move_speed_sideways = g_CurrentPlayer->speedsideways;
        move_speed_go = g_CurrentPlayer->speedgo;
        move_speed_strafe = g_CurrentPlayer->speedstrafe;
        move_speed_boost = g_CurrentPlayer->speedboost;
        move_speed_theta = g_CurrentPlayer->speedtheta;
        move_speed_verta = g_CurrentPlayer->speedverta;
        move_speed_max_time60 = g_CurrentPlayer->speedmaxtime60;
        move_head_x = g_CurrentPlayer->headpos.f[0];
        move_head_y = g_CurrentPlayer->headpos.f[1];
        move_head_z = g_CurrentPlayer->headpos.f[2];
        headlook_x = g_CurrentPlayer->headlook.f[0];
        headlook_y = g_CurrentPlayer->headlook.f[1];
        headlook_z = g_CurrentPlayer->headlook.f[2];
        headup_x = g_CurrentPlayer->headup.f[0];
        headup_y = g_CurrentPlayer->headup.f[1];
        headup_z = g_CurrentPlayer->headup.f[2];
        move_prev_x = g_CurrentPlayer->bondprevpos.f[0];
        move_prev_y = g_CurrentPlayer->bondprevpos.f[1];
        move_prev_z = g_CurrentPlayer->bondprevpos.f[2];
        if (frozen_intro_camera) {
            cam_x = g_CurrentPlayer->pos.x;
            cam_y = g_CurrentPlayer->pos.y;
            cam_z = g_CurrentPlayer->pos.z;
            cam_target_x = g_CurrentPlayer->pos2.x;
            cam_target_y = g_CurrentPlayer->pos2.y;
            cam_target_z = g_CurrentPlayer->pos2.z;
            cam_up_x = g_CurrentPlayer->offset.x;
            cam_up_y = g_CurrentPlayer->offset.y;
            cam_up_z = g_CurrentPlayer->offset.z;
            cam_floor_x = g_CurrentPlayer->pos3.x;
            cam_floor_y = g_CurrentPlayer->pos3.y;
            cam_floor_z = g_CurrentPlayer->pos3.z;
            render_cam_x = cam_x;
            render_cam_y = cam_y;
            render_cam_z = cam_z;
            render_cam_target_x = cam_target_x;
            render_cam_target_y = cam_target_y;
            render_cam_target_z = cam_target_z;
        } else {
            float applied_view_x = g_CurrentPlayer->field_488.applied_view.x;
            float applied_view_y = g_CurrentPlayer->field_488.applied_view.y;
            float applied_view_z = g_CurrentPlayer->field_488.applied_view.z;

            cam_x = g_CurrentPlayer->field_488.pos.x;
            cam_y = g_CurrentPlayer->field_488.pos.y;
            cam_z = g_CurrentPlayer->field_488.pos.z;
            cam_target_x = g_CurrentPlayer->field_488.pos.x + applied_view_x;
            cam_target_y = g_CurrentPlayer->field_488.pos.y + applied_view_y;
            cam_target_z = g_CurrentPlayer->field_488.pos.z + applied_view_z;
            cam_up_x = g_CurrentPlayer->field_488.applied_view2.x;
            cam_up_y = g_CurrentPlayer->field_488.applied_view2.y;
            cam_up_z = g_CurrentPlayer->field_488.applied_view2.z;
            cam_floor_x = g_CurrentPlayer->field_488.pos3.x;
            cam_floor_y = g_CurrentPlayer->field_488.pos3.y;
            cam_floor_z = g_CurrentPlayer->field_488.pos3.z;
#ifdef NATIVE_PORT
            render_cam_x = g_CurrentPlayer->field_488.collision_position.x;
            render_cam_y = g_CurrentPlayer->field_488.collision_position.y;
            render_cam_z = g_CurrentPlayer->field_488.collision_position.z;
#else
            render_cam_x = cam_x;
            render_cam_y = cam_y;
            render_cam_z = cam_z;
#endif
            render_cam_target_x = render_cam_x + applied_view_x;
            render_cam_target_y = render_cam_y + applied_view_y;
            render_cam_target_z = render_cam_z + applied_view_z;
        }
        cam_dx = cam_x - col_x;
        cam_dy = cam_y - col_y;
        cam_dz = cam_z - col_z;
        render_cam_dx = render_cam_x - col_x;
        render_cam_dy = render_cam_y - col_y;
        render_cam_dz = render_cam_z - col_z;
        if (frozen_intro_camera) {
            float look_x = g_CurrentPlayer->pos2.x - g_CurrentPlayer->pos.x;
            float look_y = g_CurrentPlayer->pos2.y - g_CurrentPlayer->pos.y;
            float look_z = g_CurrentPlayer->pos2.z - g_CurrentPlayer->pos.z;
            float look_len = sqrtf((look_x * look_x) + (look_y * look_y) + (look_z * look_z));
            if (look_len > 0.0001f) {
                theta0 = look_x / look_len;
                theta1 = look_y / look_len;
                theta2 = look_z / look_len;
            }
        } else {
            theta0 = g_CurrentPlayer->field_488.theta_transform.f[0];
            theta1 = g_CurrentPlayer->field_488.theta_transform.f[1];
            theta2 = g_CurrentPlayer->field_488.theta_transform.f[2];
        }
        if (g_CurrentPlayer->field_488.current_tile_ptr) {
            tile_room = g_CurrentPlayer->field_488.current_tile_ptr->room;
        }
        if (g_CurrentPlayer->field_488.current_tile_ptr_for_portals) {
            portal_room = g_CurrentPlayer->field_488.current_tile_ptr_for_portals->room;
        }
        if (g_CurrentPlayer->room_pointer) {
            room_ptr_room = g_CurrentPlayer->room_pointer->room;
        }
        if (g_CurrentPlayer->prop && g_CurrentPlayer->prop->stan) {
            prop_room = g_CurrentPlayer->prop->stan->room;
        }
        cur_room = g_CurrentPlayer->curRoomIndex;
        room_basis_x = g_CurrentPlayer->current_model_pos.x;
        room_basis_y = g_CurrentPlayer->current_model_pos.y;
        room_basis_z = g_CurrentPlayer->current_model_pos.z;
        if (track_autoaim_x) {
            traceCombatTarget(g_CurrentPlayer->autoaim_target_x,
                              px, py, pz,
                              &autoaim_target_x_type, &autoaim_target_x_chrnum,
                              &autoaim_target_x_hidden, &autoaim_target_x_alive,
                              &autoaim_target_x_actiontype, &autoaim_target_x_alertness, &autoaim_target_x_firecount,
                              &autoaim_target_x_hidden_bits, &autoaim_target_x_sleep,
                              &autoaim_target_x_ai_list, &autoaim_target_x_ai_global,
                              &autoaim_target_x_ai_offset, &autoaim_target_x_ai_return,
                              &autoaim_target_x_ai_cmd, &autoaim_target_x_ai_arg0,
                              &autoaim_target_x_seen_age, &autoaim_target_x_heard_age,
                              &autoaim_target_x_seen_recent, &autoaim_target_x_heard_recent,
                              &autoaim_target_x_line_clear, &autoaim_target_x_same_stan,
                              &autoaim_target_x_could_see_bond,
                              &autoaim_target_x_damage, &autoaim_target_x_maxdamage,
                              &autoaim_target_x_distance,
                              &autoaim_target_x_pos_x, &autoaim_target_x_pos_y, &autoaim_target_x_pos_z,
                              &autoaim_target_x_delta_x, &autoaim_target_x_delta_y, &autoaim_target_x_delta_z);
        }
        if (track_autoaim_y) {
            traceCombatTarget(g_CurrentPlayer->autoaim_target_y,
                              px, py, pz,
                              &autoaim_target_y_type, &autoaim_target_y_chrnum,
                              &autoaim_target_y_hidden, &autoaim_target_y_alive,
                              &autoaim_target_y_actiontype, &autoaim_target_y_alertness, &autoaim_target_y_firecount,
                              &autoaim_target_y_hidden_bits, &autoaim_target_y_sleep,
                              &autoaim_target_y_ai_list, &autoaim_target_y_ai_global,
                              &autoaim_target_y_ai_offset, &autoaim_target_y_ai_return,
                              &autoaim_target_y_ai_cmd, &autoaim_target_y_ai_arg0,
                              &autoaim_target_y_seen_age, &autoaim_target_y_heard_age,
                              &autoaim_target_y_seen_recent, &autoaim_target_y_heard_recent,
                              &autoaim_target_y_line_clear, &autoaim_target_y_same_stan,
                              &autoaim_target_y_could_see_bond,
                              &autoaim_target_y_damage, &autoaim_target_y_maxdamage,
                              &autoaim_target_y_distance,
                              &autoaim_target_y_pos_x, &autoaim_target_y_pos_y, &autoaim_target_y_pos_z,
                              &autoaim_target_y_delta_x, &autoaim_target_y_delta_y, &autoaim_target_y_delta_z);
        }
        if (traceCombatScanEnabled() || traceGuardOracleEnabled()) {
            nearest_guard_for_trace = traceFindNearestCombatGuard(px, py, pz);

            if (traceCombatScanEnabled()) {
                traceCombatTarget(nearest_guard_for_trace,
                                  px, py, pz,
                                  &nearest_guard_type, &nearest_guard_chrnum,
                                  &nearest_guard_hidden, &nearest_guard_alive,
                                  &nearest_guard_actiontype, &nearest_guard_alertness, &nearest_guard_firecount,
                                  &nearest_guard_hidden_bits, &nearest_guard_sleep,
                                  &nearest_guard_ai_list, &nearest_guard_ai_global,
                                  &nearest_guard_ai_offset, &nearest_guard_ai_return,
                                  &nearest_guard_ai_cmd, &nearest_guard_ai_arg0,
                                  &nearest_guard_seen_age, &nearest_guard_heard_age,
                                  &nearest_guard_seen_recent, &nearest_guard_heard_recent,
                                  &nearest_guard_line_clear, &nearest_guard_same_stan,
                                  &nearest_guard_could_see_bond,
                                  &nearest_guard_damage, &nearest_guard_maxdamage,
                                  &nearest_guard_distance,
                                  &nearest_guard_pos_x, &nearest_guard_pos_y, &nearest_guard_pos_z,
                                  &nearest_guard_delta_x, &nearest_guard_delta_y, &nearest_guard_delta_z);
            }

            if (nearest_guard_for_trace != NULL
                && nearest_guard_for_trace->type == PROP_TYPE_CHR
                && nearest_guard_for_trace->chr != NULL) {
                traceCombatVisibility(nearest_guard_for_trace->chr,
                                      &nearest_guard_line_clear,
                                      &nearest_guard_same_stan,
                                      &nearest_guard_could_see_bond,
                                      &nearest_guard_line_clear_world,
                                      &nearest_guard_line_clear_solid);
                traceCombatChrBlocker(nearest_guard_for_trace,
                                      px, pz,
                                      &nearest_guard_blocker_type,
                                      &nearest_guard_blocker_chrnum,
                                      &nearest_guard_blocker_self,
                                      &nearest_guard_blocker_distance);
            }
        }
        if (g_playerPerm) {
            int i;
            for (i = 0; i < 7; i++) {
                shot_counts[i] = g_playerPerm->shot_count[i];
                shot_count_total += shot_counts[i];
            }
        }
        {
            size_t i;
            for (i = 0; i < sizeof(g_Casings) / sizeof(g_Casings[0]); i++) {
                if (g_Casings[i].header != NULL) {
                    casing_count++;
                }
            }
        }
        if (traceInventoryStateEnabled()) {
            inv_has_ge_key = bondinvHasGEKey() ? 1 : 0;
            inv_copied_ge_key = g_CurrentPlayer->copiedgoldeneye ? 1 : 0;
            inv_keyflags |= bondinvGetHeldKeyFlags();
            {
                InvItem *item = g_CurrentPlayer->ptr_inventory_first_in_cycle;

                while (item != NULL) {
                    inv_item_count++;

                    item = item->next;

                    if (item == g_CurrentPlayer->ptr_inventory_first_in_cycle) {
                        break;
                    }
                }
            }
        }
        if (traceObjectiveStateEnabled()) {
            int i;
            int capped_count;
            size_t status_out = 0;
            size_t textid_out = 0;
            size_t difficulty_out = 0;

            objective_count_trace = objectiveGetCount();
            if (objective_count_trace < 0) {
                objective_count_trace = 0;
            }

            capped_count = objective_count_trace;
            if (capped_count > OBJECTIVES_MAX) {
                capped_count = OBJECTIVES_MAX;
            }

            objective_all_complete = objectiveIsAllComplete() ? 1 : 0;
            objective_statuses_json[0] = '[';
            objective_text_ids_json[0] = '[';
            objective_difficulties_json[0] = '[';
            status_out = 1;
            textid_out = 1;
            difficulty_out = 1;

            for (i = 0; i < capped_count; i++) {
                int wrote;
                int status = (int)get_status_of_objective(i);
                int textid = (int)objectiveGetTextId(i);
                int difficulty = get_difficulty_for_objective(i);

                wrote = snprintf(objective_statuses_json + status_out,
                                 sizeof(objective_statuses_json) - status_out,
                                 "%s%d",
                                 (i == 0) ? "" : ",",
                                 status);
                if (wrote < 0 || (size_t)wrote >= sizeof(objective_statuses_json) - status_out) {
                    break;
                }
                status_out += (size_t)wrote;

                wrote = snprintf(objective_text_ids_json + textid_out,
                                 sizeof(objective_text_ids_json) - textid_out,
                                 "%s%d",
                                 (i == 0) ? "" : ",",
                                 textid);
                if (wrote < 0 || (size_t)wrote >= sizeof(objective_text_ids_json) - textid_out) {
                    break;
                }
                textid_out += (size_t)wrote;

                wrote = snprintf(objective_difficulties_json + difficulty_out,
                                 sizeof(objective_difficulties_json) - difficulty_out,
                                 "%s%d",
                                 (i == 0) ? "" : ",",
                                 difficulty);
                if (wrote < 0 || (size_t)wrote >= sizeof(objective_difficulties_json) - difficulty_out) {
                    break;
                }
                difficulty_out += (size_t)wrote;
            }

            if (status_out + 2 <= sizeof(objective_statuses_json)) {
                objective_statuses_json[status_out++] = ']';
                objective_statuses_json[status_out] = '\0';
            } else {
                objective_statuses_json[sizeof(objective_statuses_json) - 2] = ']';
                objective_statuses_json[sizeof(objective_statuses_json) - 1] = '\0';
            }

            if (textid_out + 2 <= sizeof(objective_text_ids_json)) {
                objective_text_ids_json[textid_out++] = ']';
                objective_text_ids_json[textid_out] = '\0';
            } else {
                objective_text_ids_json[sizeof(objective_text_ids_json) - 2] = ']';
                objective_text_ids_json[sizeof(objective_text_ids_json) - 1] = '\0';
            }

            if (difficulty_out + 2 <= sizeof(objective_difficulties_json)) {
                objective_difficulties_json[difficulty_out++] = ']';
                objective_difficulties_json[difficulty_out] = '\0';
            } else {
                objective_difficulties_json[sizeof(objective_difficulties_json) - 2] = ']';
                objective_difficulties_json[sizeof(objective_difficulties_json) - 1] = '\0';
            }

            snprintf(objective_field_json,
                     sizeof(objective_field_json),
                     "\"obj\":{\"count\":%d,\"all_complete\":%d,\"flags\":\"0x%08X\","
                     "\"statuses\":%s,\"text_ids\":%s,\"difficulties\":%s},",
                     objective_count_trace,
                     objective_all_complete,
                     (unsigned int)objectiveregisters1,
                     objective_statuses_json,
                     objective_text_ids_json,
                     objective_difficulties_json);
        }

        tank_in = in_tank_flag ? 1 : 0;
        tank_can_enter = g_BondCanEnterTank ? 1 : 0;
        tank_world_present = (g_WorldTankProp != NULL) ? 1 : 0;
        tank_player_present = (g_PlayerTankProp != NULL) ? 1 : 0;
        tank_reserve_ammo = get_ammo_count_for_weapon(ITEM_TANKSHELLS);

        if (g_WorldTankProp != NULL) {
            tank_world_x = g_WorldTankProp->pos.x;
            tank_world_y = g_WorldTankProp->pos.y;
            tank_world_z = g_WorldTankProp->pos.z;

            if (g_WorldTankProp->obj != NULL) {
                tank_world_obj = g_WorldTankProp->obj->obj;
                tank_world_pad = g_WorldTankProp->obj->pad;
            }
        }

        if (g_PlayerTankProp != NULL) {
            tank_player_x = g_PlayerTankProp->pos.x;
            tank_player_y = g_PlayerTankProp->pos.y;
            tank_player_z = g_PlayerTankProp->pos.z;

            if (g_PlayerTankProp->obj != NULL) {
                tank_player_obj = g_PlayerTankProp->obj->obj;
                tank_player_pad = g_PlayerTankProp->obj->pad;

                if (g_PlayerTankProp->obj->type == PROPDEF_TANK) {
                    TankRecord *tank = (TankRecord *)g_PlayerTankProp->obj;
                    tank_stored_ammo = tank->unkD8;
                    tank_firing = tank->is_firing_tank ? 1 : 0;
                }
            }
        }

        snprintf(alarm_field_json,
                 sizeof(alarm_field_json),
                 "\"alarm\":{\"active\":%d,\"timer\":%d},",
                 alarmIsActive() ? 1 : 0,
                 alarm_timer);

        if (traceTrackedChrNum() >= 0 || traceGuardOracleEnabled()) {
            int requested_chrnum = traceTrackedChrNum();
            int tracked_source = 0;
            ChrRecord *tracked_chr = NULL;

            if (requested_chrnum >= 0) {
                tracked_chr = chrFindByLiteralId(requested_chrnum);
            } else if (nearest_guard_for_trace != NULL
                       && nearest_guard_for_trace->type == PROP_TYPE_CHR
                       && nearest_guard_for_trace->chr != NULL) {
                tracked_chr = nearest_guard_for_trace->chr;
                requested_chrnum = tracked_chr->chrnum;
                tracked_source = 1;
            }

            if (tracked_chr != NULL) {
                float anim_frame = 0.0f;
                float anim_end = 0.0f;
                float anim_speed = 0.0f;
                float dist_to_bond = 0.0f;
                int seen_age = -1;
                int heard_age = -1;
                int seen_recent = 0;
                int heard_recent = 0;
                bool tracked_ai_global = false;
                int tracked_ai_list = chraiGetAIListID(tracked_chr->ailist, &tracked_ai_global);
                int tracked_ai_cmd = -1;
                int tracked_ai_arg0 = -1;
                int tracked_patrol_mode = -1;
                int tracked_patrol_age = -1;
                int tracked_patrol_lastvisible = -1;
                int tracked_patrol_nextstep = -1;
                int tracked_patrol_forward = -1;
                float tracked_patrol_speed = 0.0f;
                float tracked_patrol_segdone = 0.0f;
                float tracked_patrol_segtotal = 0.0f;
                u8 tracked_react_latched = 0;
                char tracked_rooms_json[64];
                int tracked_room_count = 0;
                int tracked_first_room = -1;
                int tracked_stan_room = -1;
                int tracked_any_rendered = 0;
                int tracked_first_rendered = 0;
                int tracked_prop_flags = 0;
                int tracked_prop_onscreen = 0;
                int tracked_seen_onscreen = 0;
                int tracked_field20 = 0;
                int tracked_model_mtx = 0;
                int tracked_on_proplist = 0;
                TracePropFloorSnapshot tracked_floor;
                TraceHeldPropSnapshot held_right;
                TraceHeldPropSnapshot held_left;
                TraceHeldPropSnapshot held_hat;

                if (tracked_chr->chrnum >= 0 && tracked_chr->chrnum < TRACE_CHR_REACT_MAX) {
                    tracked_react_latched = s_traceChrReactLatched[tracked_chr->chrnum];
                }

                if (tracked_chr->ailist != NULL) {
                    tracked_ai_cmd = tracked_chr->ailist[tracked_chr->aioffset].cmd;
                    tracked_ai_arg0 = tracked_chr->ailist[tracked_chr->aioffset].val[0];
                }

                if (tracked_chr->actiontype == ACT_PATROL) {
                    tracked_patrol_mode = tracked_chr->act_patrol.waydata.mode;
                    tracked_patrol_age = tracked_chr->act_patrol.waydata.age;
                    tracked_patrol_lastvisible = tracked_chr->act_patrol.lastvisible60;
                    tracked_patrol_nextstep = tracked_chr->act_patrol.nextstep;
                    tracked_patrol_forward = tracked_chr->act_patrol.forward;
                    tracked_patrol_speed = tracked_chr->act_patrol.speed;
                    tracked_patrol_segdone = tracked_chr->act_patrol.waydata.segdistdone;
                    tracked_patrol_segtotal = tracked_chr->act_patrol.waydata.segdisttotal;
                }

                if (tracked_chr->model != NULL) {
                    anim_frame = objecthandlerGetModelField28(tracked_chr->model);
                    anim_end = sub_GAME_7F06F5C4(tracked_chr->model);
                    anim_speed = modelGetAbsAnimSpeed(tracked_chr->model);
                }

                if (tracked_chr->prop != NULL && g_CurrentPlayer != NULL && g_CurrentPlayer->prop != NULL) {
                    dist_to_bond = chrGetDistanceToBond(tracked_chr);
                }

                if (tracked_chr->lastseetarget60 > 0) {
                    seen_age = g_GlobalTimer - tracked_chr->lastseetarget60;
                    seen_recent = (seen_age < CHRLV_10_SEC_TIMER) ? 1 : 0;
                }

                if (tracked_chr->lastheartarget60 > 0) {
                    heard_age = g_GlobalTimer - tracked_chr->lastheartarget60;
                    heard_recent = (heard_age < CHRLV_10_SEC_TIMER) ? 1 : 0;
                }

                traceBuildPropRoomsJson(tracked_chr->prop,
                                        tracked_rooms_json,
                                        sizeof(tracked_rooms_json),
                                        &tracked_room_count,
                                        &tracked_first_room,
                                        &tracked_stan_room,
                                        &tracked_any_rendered,
                                        &tracked_first_rendered);

                if (tracked_chr->prop != NULL) {
                    tracked_prop_flags = tracked_chr->prop->flags;
                    tracked_prop_onscreen = ((tracked_chr->prop->flags & PROPFLAG_ONSCREEN) != 0) ? 1 : 0;

                    /* g_OnScreenPropList membership: the rebuilt-every-frame list
                     * that makes a prop auto-aim-targetable/hittable. This is the
                     * precise H1b observable (PROPFLAG_ONSCREEN alone latches and is
                     * managed by a separate camera pass). */
                    {
                        extern PropRecord **g_LastOnScreenProp;
                        extern PropRecord *g_OnScreenPropList[];
                        PropRecord **pp;
                        if (g_LastOnScreenProp != NULL) {
                            for (pp = g_OnScreenPropList; pp < g_LastOnScreenProp; pp++) {
                                if (*pp == tracked_chr->prop) {
                                    tracked_on_proplist = 1;
                                    break;
                                }
                            }
                        }
                    }
                }

                tracked_seen_onscreen = ((tracked_chr->chrflags & CHRFLAG_HAS_BEEN_ON_SCREEN) != 0) ? 1 : 0;
                tracked_field20 = (tracked_chr->field_20 != NULL) ? 1 : 0;

                if (tracked_chr->model != NULL && tracked_chr->model->render_pos != NULL) {
                    tracked_model_mtx = modelGetRenderPosCount(tracked_chr->model);
                }

                tracePropFloorSnapshot(tracked_chr->prop, &tracked_floor);
                traceHeldPropSnapshot(tracked_chr->weapons_held[GUNRIGHT], &held_right);
                traceHeldPropSnapshot(tracked_chr->weapons_held[GUNLEFT], &held_left);
                traceHeldPropSnapshot(tracked_chr->handle_positiondata_hat, &held_hat);

                snprintf(tracked_chr_field_json,
                         sizeof(tracked_chr_field_json),
                         "\"track\":{\"chrnum\":%d,\"source\":%d,\"present\":1,\"hidden\":%d,\"hidden_bits\":%d,"
                         "\"chrflags\":\"0x%08X\",\"alive\":%d,"
                         "\"flag_hidden\":%d,\"flag_update_action\":%d,\"bg_ai\":%d,"
                         "\"action\":%d,\"alert\":%d,\"sleep\":%d,\"firecount\":%d,"
                         "\"shotbondsum\":%.5f,\"accuracyrating\":%d,"
                         "\"damage\":%.4f,\"maxdamage\":%.4f,"
                         "\"padpreset\":%d,\"dist_to_bond\":%.2f,\"pos\":[%.2f,%.2f,%.2f],"
                         "\"floor\":{\"valid\":%d,\"nearest\":%d,\"room\":%d,\"y\":%.2f,\"delta\":%.2f},"
                         "\"react\":{\"near_miss\":%d,\"heard_now\":%d,\"seen_recent\":%d,\"heard_recent\":%d,"
                         "\"seen_age\":%d,\"heard_age\":%d,\"numarghs\":%d,\"numclosearghs\":%d,"
                         "\"saw_shot\":%d,\"saw_die\":%d},"
                         "\"ai\":{\"list\":%d,\"global\":%d,\"offset\":%d,\"return\":%d,\"cmd\":%d,\"arg0\":%d},"
                         "\"patrol\":{\"mode\":%d,\"age\":%d,\"lastvisible\":%d,\"nextstep\":%d,"
                         "\"forward\":%d,\"speed\":%.4f,\"segdistdone\":%.2f,\"segdisttotal\":%.2f},"
                         "\"room\":{\"stan\":%d,\"first\":%d,\"count\":%d,\"any_rendered\":%d,"
                         "\"first_rendered\":%d,\"rooms\":%s},"
                         "\"render\":{\"prop_flags\":\"0x%08X\",\"onscreen\":%d,\"seen_onscreen\":%d,"
                         "\"z\":%.2f,\"field20\":%d,\"model_mtx\":%d,\"on_proplist\":%d},"
                         "\"held\":{\"right\":{\"present\":%d,\"obj\":%d,\"item\":%d,\"state\":%d,"
                         "\"runtime\":\"0x%08X\",\"prop_flags\":\"0x%08X\",\"has_mtx\":%d,"
                         "\"room\":{\"stan\":%d,\"first\":%d,\"count\":%d,\"any_rendered\":%d,"
                         "\"first_rendered\":%d,\"rooms\":%s},"
                         "\"owner\":{\"type\":%d,\"chrnum\":%d,\"obj\":%d,"
                         "\"room\":{\"stan\":%d,\"first\":%d,\"count\":%d,\"any_rendered\":%d,"
                         "\"first_rendered\":%d,\"rooms\":%s}}},"
                         "\"left\":{\"present\":%d,\"obj\":%d,\"item\":%d,\"state\":%d,"
                         "\"runtime\":\"0x%08X\",\"prop_flags\":\"0x%08X\",\"has_mtx\":%d,"
                         "\"room\":{\"stan\":%d,\"first\":%d,\"count\":%d,\"any_rendered\":%d,"
                         "\"first_rendered\":%d,\"rooms\":%s},"
                         "\"owner\":{\"type\":%d,\"chrnum\":%d,\"obj\":%d,"
                         "\"room\":{\"stan\":%d,\"first\":%d,\"count\":%d,\"any_rendered\":%d,"
                         "\"first_rendered\":%d,\"rooms\":%s}}},"
                         "\"hat\":{\"present\":%d,\"obj\":%d,\"item\":%d,\"state\":%d,"
                         "\"runtime\":\"0x%08X\",\"prop_flags\":\"0x%08X\",\"has_mtx\":%d,"
                         "\"room\":{\"stan\":%d,\"first\":%d,\"count\":%d,\"any_rendered\":%d,"
                         "\"first_rendered\":%d,\"rooms\":%s},"
                         "\"owner\":{\"type\":%d,\"chrnum\":%d,\"obj\":%d,"
                         "\"room\":{\"stan\":%d,\"first\":%d,\"count\":%d,\"any_rendered\":%d,"
                         "\"first_rendered\":%d,\"rooms\":%s}}}},"
                         "\"anim_frame\":%.2f,\"anim_end\":%.2f,\"anim_speed\":%.4f},",
                         tracked_chr->chrnum,
                         tracked_source,
                         tracked_chr->hidden ? 1 : 0,
                         tracked_chr->hidden,
                         (unsigned int)tracked_chr->chrflags,
                         (tracked_chr->damage < tracked_chr->maxdamage) ? 1 : 0,
                         ((tracked_chr->chrflags & CHRFLAG_HIDDEN) != 0),
                         ((tracked_chr->chrflags & CHRFLAG_00040000) != 0),
                         ((tracked_chr->hidden & CHRHIDDEN_BACKGROUND_AI) != 0),
                         tracked_chr->actiontype,
                         tracked_chr->alertness,
                         tracked_chr->sleep,
                         tracked_chr->firecount[0] + tracked_chr->firecount[1],
                         tracked_chr->shotbondsum,
                         tracked_chr->accuracyrating,
                         tracked_chr->damage,
                         tracked_chr->maxdamage,
                         tracked_chr->padpreset1,
                         dist_to_bond,
                         tracked_chr->prop != NULL ? tracked_chr->prop->pos.x : 0.0f,
                         tracked_chr->prop != NULL ? tracked_chr->prop->pos.y : 0.0f,
                         tracked_chr->prop != NULL ? tracked_chr->prop->pos.z : 0.0f,
                         tracked_floor.valid,
                         tracked_floor.nearest,
                         tracked_floor.room,
                         tracked_floor.y,
                         tracked_floor.delta,
                         ((tracked_chr->chrflags & CHRFLAG_NEAR_MISS) != 0) || (tracked_react_latched & TRACE_CHR_REACT_NEAR_MISS),
                         ((tracked_chr->hidden & CHRHIDDEN_ALERT_GUARD_RELATED) != 0) || (tracked_react_latched & TRACE_CHR_REACT_ALERTED),
                         seen_recent,
                         heard_recent,
                         seen_age,
                         heard_age,
                         tracked_chr->numarghs,
                         tracked_chr->numclosearghs,
                         (tracked_chr->chrseeshot >= 0) || (tracked_react_latched & TRACE_CHR_REACT_SAW_SHOT),
                         (tracked_chr->chrseedie >= 0) || (tracked_react_latched & TRACE_CHR_REACT_SAW_DIE),
                         tracked_ai_list,
                         tracked_ai_global ? 1 : 0,
                         tracked_chr->aioffset,
                         tracked_chr->aireturnlist,
                         tracked_ai_cmd,
                         tracked_ai_arg0,
                         tracked_patrol_mode,
                         tracked_patrol_age,
                         tracked_patrol_lastvisible,
                         tracked_patrol_nextstep,
                         tracked_patrol_forward,
                         tracked_patrol_speed,
                         tracked_patrol_segdone,
                         tracked_patrol_segtotal,
                         tracked_stan_room,
                         tracked_first_room,
                         tracked_room_count,
                         tracked_any_rendered,
                         tracked_first_rendered,
                         tracked_rooms_json,
                         (unsigned int)tracked_prop_flags,
                         tracked_prop_onscreen,
                         tracked_seen_onscreen,
                         tracked_chr->prop != NULL ? tracked_chr->prop->zDepth : 0.0f,
                         tracked_field20,
                         tracked_model_mtx,
                         tracked_on_proplist,
                         held_right.present,
                         held_right.obj,
                         held_right.item,
                         held_right.state,
                         (unsigned int)held_right.runtime,
                         (unsigned int)held_right.propflags,
                         held_right.has_mtx,
                         held_right.stan_room,
                         held_right.first_room,
                         held_right.room_count,
                         held_right.any_rendered,
                         held_right.first_rendered,
                         held_right.rooms_json,
                         held_right.owner_type,
                         held_right.owner_chrnum,
                         held_right.owner_obj,
                         held_right.owner_stan_room,
                         held_right.owner_first_room,
                         held_right.owner_room_count,
                         held_right.owner_any_rendered,
                         held_right.owner_first_rendered,
                         held_right.owner_rooms_json,
                         held_left.present,
                         held_left.obj,
                         held_left.item,
                         held_left.state,
                         (unsigned int)held_left.runtime,
                         (unsigned int)held_left.propflags,
                         held_left.has_mtx,
                         held_left.stan_room,
                         held_left.first_room,
                         held_left.room_count,
                         held_left.any_rendered,
                         held_left.first_rendered,
                         held_left.rooms_json,
                         held_left.owner_type,
                         held_left.owner_chrnum,
                         held_left.owner_obj,
                         held_left.owner_stan_room,
                         held_left.owner_first_room,
                         held_left.owner_room_count,
                         held_left.owner_any_rendered,
                         held_left.owner_first_rendered,
                         held_left.owner_rooms_json,
                         held_hat.present,
                         held_hat.obj,
                         held_hat.item,
                         held_hat.state,
                         (unsigned int)held_hat.runtime,
                         (unsigned int)held_hat.propflags,
                         held_hat.has_mtx,
                         held_hat.stan_room,
                         held_hat.first_room,
                         held_hat.room_count,
                         held_hat.any_rendered,
                         held_hat.first_rendered,
                         held_hat.rooms_json,
                         held_hat.owner_type,
                         held_hat.owner_chrnum,
                         held_hat.owner_obj,
                         held_hat.owner_stan_room,
                         held_hat.owner_first_room,
                         held_hat.owner_room_count,
                         held_hat.owner_any_rendered,
                         held_hat.owner_first_rendered,
                         held_hat.owner_rooms_json,
                         anim_frame,
                         anim_end,
                         anim_speed);
            } else {
                snprintf(tracked_chr_field_json,
                         sizeof(tracked_chr_field_json),
                         "\"track\":{\"chrnum\":%d,\"source\":%d,\"present\":0},",
                         requested_chrnum,
                         tracked_source);
            }
        }
        if (current_menu == MENU_RUN_STAGE &&
            gamemode == GAMEMODE_SOLO &&
            g_StageNum != LEVELID_TITLE &&
            lvlGetCurrentStageToLoad() != LEVELID_TITLE &&
            g_CurrentPlayer->prop != NULL &&
            g_CurrentPlayer->prop->stan != NULL) {
            f32 lookup_pos[3];
            f32 cam_lookup_pos[3];
            StandTile *lookup_tile;
            StandTile *nearest_tile;
            StandTile *cam_lookup_tile;
            StandTile *cam_nearest_tile;

            /* These nearest-stan fallbacks can walk the entire stan table.
             * Keep them out of frontend/title transition frames where player
             * state may be stale after RAMROM playback returns to menus. */
            lookup_pos[0] = col_x;
            lookup_pos[1] = col_y;
            lookup_pos[2] = col_z;
            cam_lookup_pos[0] = cam_x;
            cam_lookup_pos[1] = frozen_intro_camera ? cam_y : cam_floor_y;
            cam_lookup_pos[2] = cam_z;

            lookup_tile = sub_GAME_7F0AF20C(lookup_pos, 0, NULL);
            if (lookup_tile) {
                lookup_room = lookup_tile->room;
            }

            nearest_tile = sub_GAME_7F0AFB78(&lookup_pos[0], &lookup_pos[1], &lookup_pos[2], 0.0f);
            if (nearest_tile) {
                nearest_room = nearest_tile->room;
            }

            cam_lookup_tile = sub_GAME_7F0AF20C(cam_lookup_pos, 0, NULL);
            if (cam_lookup_tile) {
                cam_lookup_room = cam_lookup_tile->room;
            }

            cam_nearest_tile = sub_GAME_7F0AFB78(&cam_lookup_pos[0], &cam_lookup_pos[1], &cam_lookup_pos[2], 0.0f);
            if (cam_nearest_tile) {
                cam_nearest_room = cam_nearest_tile->room;
            }
        }
    }

#ifdef PORT_FIXME_STUBS
    snd_stub_hits_total = sndStubCounterGetTotal();
    snd_stub_hits_play = sndStubCounterGet(SND_STUB_COUNTER_PLAY_SFX);
    snd_stub_legacy_overlap_hits = sndStubCounterGetLegacyOverlapFallbackHits();
    snd_stub_new_player_init = sndStubCounterGet(SND_STUB_COUNTER_NEW_PLAYER_INIT);
    snd_stub_get_playing_state = sndStubCounterGet(SND_STUB_COUNTER_GET_PLAYING_STATE);
    snd_stub_deactivate = sndStubCounterGet(SND_STUB_COUNTER_DEACTIVATE);
    snd_stub_deactivate_all_1 = sndStubCounterGet(SND_STUB_COUNTER_DEACTIVATE_ALL_1);
    snd_stub_deactivate_all_11 = sndStubCounterGet(SND_STUB_COUNTER_DEACTIVATE_ALL_11);
    snd_stub_deactivate_all_3 = sndStubCounterGet(SND_STUB_COUNTER_DEACTIVATE_ALL_3);
    snd_stub_create_post_event = sndStubCounterGet(SND_STUB_COUNTER_CREATE_POST_EVENT);
    snd_stub_dispose_sound = sndStubCounterGet(SND_STUB_COUNTER_DISPOSE_SOUND);
    snd_stub_set_priority = sndStubCounterGet(SND_STUB_COUNTER_SET_PRIORITY);
    snd_stub_unlink_clear = sndStubCounterGet(SND_STUB_COUNTER_UNLINK_CLEAR);
    snd_stub_count_alloc_list = sndStubCounterGet(SND_STUB_COUNTER_COUNT_ALLOC_LIST);
#endif

    mp_time_ticks = D_80048394;
    mp_player_count = getPlayerCount();

    /* Intro state — camera mode, intro camera index, after-cinema target */
    int cam_mode = (int)g_CameraMode;
    int cam_after = (int)g_CameraAfterCinema;
    int icam = intro_camera_index;
    float intro_timer = camera_transition_timer;
    vi_view_left = viGetViewLeft();
    vi_view_top = viGetViewTop();
    vi_view_w = viGetViewWidth();
    vi_view_h = viGetViewHeight();
    if (trace_live_stage_globals && has_player) {
        render_room = (int)bondviewGetCurrentPlayersRoom();
    }
    if (trace_live_stage_globals) {
        traceCameraMatricesIfRequested(cam_mode, player_unknown, render_room);
    }

    /* Render stats from gfx_pc.c */
    int tris = 0, bad_cmds = 0, render_frame = 0;
    int fog_r = 0, fog_g = 0, fog_b = 0, fog_mul = 0, fog_off = 0;
    unsigned int geom_mode = 0;
    int dl_mtx_fail = 0, dl_vtx_fail = 0, dl_fail = 0;
    int dl_movemem_fail = 0, dl_texture_fail = 0, dl_settimg_fail = 0;
    int dl_non_dl_skip_pc = 0, dl_non_dl_skip_n64 = 0, dl_unregistered_skip = 0;
    gfx_get_frame_stats(&tris, &render_frame, &fog_r, &fog_g, &fog_b,
                        &fog_mul, &fog_off, &geom_mode, &bad_cmds);
    gfx_get_frame_resolve_stats(&dl_mtx_fail, &dl_vtx_fail, &dl_fail,
                                &dl_movemem_fail, &dl_texture_fail, &dl_settimg_fail,
                                &dl_non_dl_skip_pc, &dl_non_dl_skip_n64,
                                &dl_unregistered_skip);
    room_render_fallback_active = (g_portRoomRenderFallbackFrame == g_frame_count_diag) ? 1 : 0;
    room_render_fallback_rooms = room_render_fallback_active ? g_portRoomRenderFallbackRooms : 0;
    room_render_fallback_total = g_portRoomRenderFallbackTotal;
    int rooms_drawn = (int)g_BgNumberOfRoomsDrawn;
    unsigned int seg_mask = gfx_get_segment_mask();
    int menu = (int)current_menu;
    int menu_pending = (int)menu_update;
    int menu_prev = (int)maybe_prev_menu;
    int menu_timer = (int)g_MenuTimer;
    int stage = (int)selected_stage;
    int difficulty = (int)selected_difficulty;
    int loaded_stage = (int)g_StageNum;
    int pending_stage = (int)g_MainStageNum;
    int active_stage = (int)lvlGetCurrentStageToLoad();
    int mission_state_id = (int)get_mission_state();
    int game_mode = (int)gamemode;
    float menu_cursor_x = cursor_h_pos;
    float menu_cursor_y = cursor_v_pos;
    int menu_selected_folder = selected_folder_num;
    int menu_hover_folder = port_front_hover_folder;
    int menu_folder_option = folder_selection_screen_option_icon;
    int menu_folder_delete = folder_selected_for_deletion;
    int menu_folder_delete_choice = folder_selected_for_deletion_choice;
    int menu_highlight = mission_difficulty_highlighted;
    int menu_briefing = briefingpage;
    int menu_briefing_page = current_menu_briefing_page;
    int mission_failed = mission_failed_or_aborted ? 1 : 0;
    int bond_kia = g_isBondKIA ? 1 : 0;
    int all_obj_complete = get_debug_all_obj_complete_flag() ? 1 : 0;
    int menu_entered = (s_prevMenu != menu);
    int title_gunbarrel_mode = (int)gunbarrel_mode;
    int title_eye_counter = intro_eye_counter;
    int title_blood_state = (int)intro_state_blood_animation;
    float title_x = g_TitleX;
    float title_y = g_TitleY;
    float title_transition_x = titleTransitionX;
    float title_transition_y = titleTransitionY;
    int title_wave = (int)word_CODE_bss_80069584;
    float title_rare_rotation = D_8002A89C;
    float title_nintendo_rotation = ninLogoRotRate;
    float title_nintendo_scale = ninLogoScale;
    int intro_bond_present = 0;
    int intro_bond_chrnum = -1;
    int intro_bond_action = -1;
    int intro_bond_sleep = -1;
    int intro_bond_field20 = 0;
    int intro_bond_model_mtx = 0;
    int intro_bond_onscreen = 0;
    int intro_bond_seen_onscreen = 0;
    int intro_bond_rendered = (trace_live_stage_globals && s_bondIntroLastTraceFrame == s_traceFrame) ? 1 : 0;
    int intro_bond_anim_valid = 0;
    float intro_bond_anim_frame = 0.0f;
    float intro_bond_anim_end = 0.0f;
    float intro_bond_anim_speed = 0.0f;
    float intro_bond_anim_abs_speed = 0.0f;
    int intro_bond_anim_looping = 0;
    int intro_bond_anim_gunhand = -1;
    int intro_bond_anim_offset = -1;
    int intro_bond_anim_frames = -1;
    int intro_bond_anim_entry_offset = -1;
    int intro_bond_anim_bits_offset = -1;
    u64 intro_bond_anim_hash = 0;
    int intro_setup_anim_index = trace_live_stage_globals ? g_IntroAnimationIndex : -1;
    int intro_swirl_present = (trace_live_stage_globals && g_IntroSwirl != NULL) ? 1 : 0;
    int intro_swirl_count = 0;
    u64 intro_swirl_hash = 0;
    int intro_swirl_current_index = -1;
    int intro_swirl_current_flags = 0;
    float intro_swirl_current_x = 0.0f;
    float intro_swirl_current_y = 0.0f;
    float intro_swirl_current_z = 0.0f;
    float intro_swirl_current_curve = 0.0f;
    float intro_swirl_current_duration = 0.0f;
    int intro_swirl_current_pad = -1;
    int intro_selected_camera_present = 0;
    int intro_selected_camera_index = -1;
    int intro_selected_camera_count = trace_live_stage_globals ? g_SetupIntroCameraCount : 0;
    float intro_selected_camera_x = 0.0f;
    float intro_selected_camera_y = 0.0f;
    float intro_selected_camera_z = 0.0f;
    float intro_selected_camera_yaw = 0.0f;
    float intro_selected_camera_pitch = 0.0f;
    int intro_selected_camera_pad = -1;
    int intro_selected_camera_pad_room = -1;
    float intro_selected_camera_pad_x = 0.0f;
    float intro_selected_camera_pad_y = 0.0f;
    float intro_selected_camera_pad_z = 0.0f;
    TraceHeldPropSnapshot intro_bond_held_right;
    TraceHeldPropSnapshot intro_bond_held_left;
    char intro_bond_body_json[512];
    pc_ramrom_trace_state ramrom_trace;

    memset(&intro_bond_held_right, 0, sizeof(intro_bond_held_right));
    memset(&intro_bond_held_left, 0, sizeof(intro_bond_held_left));
    intro_bond_held_right.obj = -1;
    intro_bond_held_right.item = -1;
    intro_bond_held_left.obj = -1;
    intro_bond_held_left.item = -1;
    snprintf(intro_bond_body_json, sizeof(intro_bond_body_json), "{\"present\":0}");

    if (trace_live_stage_globals && g_IntroSwirl != NULL) {
        int i;

        intro_swirl_hash = 1469598103934665603ULL;
        for (i = 0; i < 64 && g_IntroSwirl[i].type == INTROTYPE_SWIRL; i++) {
            const struct SetupIntroSwirl *entry = &g_IntroSwirl[i];

            intro_swirl_hash = traceIntroHashU32(intro_swirl_hash, (u32)entry->type);
            intro_swirl_hash = traceIntroHashU32(intro_swirl_hash, (u32)entry->unk04);
            intro_swirl_hash = traceIntroHashU32(intro_swirl_hash, (u32)entry->unk08.ival);
            intro_swirl_hash = traceIntroHashU32(intro_swirl_hash, (u32)entry->unk0C.ival);
            intro_swirl_hash = traceIntroHashU32(intro_swirl_hash, (u32)entry->unk10.ival);
            intro_swirl_hash = traceIntroHashU32(intro_swirl_hash, (u32)entry->unk14.ival);
            intro_swirl_hash = traceIntroHashU32(intro_swirl_hash, (u32)entry->unk18.ival);
            intro_swirl_hash = traceIntroHashU32(intro_swirl_hash, (u32)entry->unk1C);
            intro_swirl_count++;
        }

        if (intro_swirl_count > 0) {
            const struct SetupIntroSwirl *entry;

            intro_swirl_current_index = intro_camera_index;
            if (intro_swirl_current_index < 0 || intro_swirl_current_index >= intro_swirl_count) {
                intro_swirl_current_index = 0;
            }

            entry = &g_IntroSwirl[intro_swirl_current_index];
            intro_swirl_current_flags = entry->unk04;
            intro_swirl_current_x = entry->unk08.fval;
            intro_swirl_current_y = entry->unk0C.fval;
            intro_swirl_current_z = entry->unk10.fval;
            intro_swirl_current_curve = entry->unk14.fval;
            intro_swirl_current_duration = entry->unk18.fval;
            intro_swirl_current_pad = entry->unk1C;
        }
    }

    if (trace_live_stage_globals && ptr_random06cam_entry != NULL) {
        struct SetupIntroCamera *iter;
        int reverse_index = 0;

        intro_selected_camera_present = 1;
        intro_selected_camera_x = ptr_random06cam_entry->unk04.fval;
        intro_selected_camera_y = ptr_random06cam_entry->unk08.fval;
        intro_selected_camera_z = ptr_random06cam_entry->unk0C.fval;
        intro_selected_camera_yaw = ptr_random06cam_entry->unk10.fval;
        intro_selected_camera_pitch = ptr_random06cam_entry->unk14.fval;
        intro_selected_camera_pad = ptr_random06cam_entry->unk18;

        for (iter = g_CurrentSetupIntroCamera; iter != NULL; iter = iter->prev) {
            if (iter == ptr_random06cam_entry) {
                intro_selected_camera_index = (intro_selected_camera_count - 1) - reverse_index;
                break;
            }
            reverse_index++;
        }

        if (g_CurrentSetup.pads != NULL && intro_selected_camera_pad >= 0) {
            PadRecord *pad = &g_CurrentSetup.pads[intro_selected_camera_pad];
            intro_selected_camera_pad_x = pad->pos.f[0];
            intro_selected_camera_pad_y = pad->pos.f[1];
            intro_selected_camera_pad_z = pad->pos.f[2];
            intro_selected_camera_pad_room = pad->stan != NULL ? pad->stan->room : -1;
        }
    }

    pcRamromGetTraceState(&ramrom_trace);
    snprintf(ramrom_field_json,
             sizeof(ramrom_field_json),
             "\"ramrom\":{\"active\":%d,\"playback_pending\":%d,\"demo_related\":%d,\"recording\":%d,"
             "\"symbol\":\"%s\",\"stage\":%d,\"difficulty\":%d,\"size_cmds\":%d,\"total_time\":%d,\"file_size\":%d,"
             "\"base_offset\":%u,\"cursor_offset\":%u,\"input_stream_offset\":%u,"
             "\"block\":{\"count\":%d,\"speedframes\":%d,\"randseed\":%d,\"check\":%d},"
             "\"rng\":{\"call_count\":%llu,\"call_base\":%llu,\"calls_since_restore\":%llu,"
             "\"restored_seed\":\"0x%016llX\",\"seed_low\":%d},"
             "\"stop\":{\"reason\":\"%s\",\"event_count\":%d,\"input_stream_offset\":%u,"
             "\"random_call_count\":%llu,\"random_calls_since_restore\":%llu,"
             "\"random_seed\":\"0x%016llX\",\"abort_buttons\":%d,"
             "\"randseed_expected\":%d,\"randseed_actual\":%d,"
             "\"checksum_expected\":%d,\"checksum_actual\":%d}},",
             ramrom_trace.active,
             ramrom_trace.playback_pending,
             ramrom_trace.demo_related,
             ramrom_trace.recording,
             ramrom_trace.symbol != NULL ? ramrom_trace.symbol : "",
             ramrom_trace.stage,
             ramrom_trace.difficulty,
             ramrom_trace.size_cmds,
             ramrom_trace.total_time,
             ramrom_trace.file_size,
             ramrom_trace.base_offset,
             ramrom_trace.cursor_offset,
             ramrom_trace.input_stream_offset,
             ramrom_trace.current_block_count,
             ramrom_trace.current_block_speedframes,
             ramrom_trace.current_block_randseed,
             ramrom_trace.current_block_check,
             (unsigned long long)ramrom_trace.random_call_count,
             (unsigned long long)ramrom_trace.random_call_base,
             (unsigned long long)ramrom_trace.random_calls_since_restore,
             (unsigned long long)ramrom_trace.restored_random_seed,
             ramrom_trace.random_seed_low,
             ramrom_trace.last_stop_reason != NULL ? ramrom_trace.last_stop_reason : "",
             ramrom_trace.last_stop_event_count,
             ramrom_trace.last_stop_input_stream_offset,
             (unsigned long long)ramrom_trace.last_stop_random_call_count,
             (unsigned long long)ramrom_trace.last_stop_random_calls_since_restore,
             (unsigned long long)ramrom_trace.last_stop_random_seed,
             ramrom_trace.last_abort_buttons,
             ramrom_trace.last_randseed_expected,
             ramrom_trace.last_randseed_actual,
             ramrom_trace.last_checksum_expected,
             ramrom_trace.last_checksum_actual);

    if (!s_assertFileSelectSeen && menu == MENU_FILE_SELECT) {
        s_assertFileSelectSeen = 1;
    }

    if (!s_assertStageMenuSelected &&
        (menu == MENU_BRIEFING || menu == MENU_007_OPTIONS) &&
        stage != LEVELID_NONE &&
        game_mode == GAMEMODE_SOLO) {
        s_assertStageMenuSelected = 1;
    }

    if (!s_assertMissionStartAuthentic &&
        menu_entered &&
        menu == MENU_RUN_STAGE &&
        game_mode == GAMEMODE_SOLO &&
        stage != LEVELID_NONE &&
        (s_prevMenu == MENU_BRIEFING || s_prevMenu == MENU_SWITCH_SCREENS)) {
        s_assertMissionStartAuthentic = 1;
    }

    if (!s_assertPostMissionTransition &&
        menu_entered &&
        (s_prevMenu == MENU_MISSION_COMPLETE || s_prevMenu == MENU_MISSION_FAILED) &&
        (menu == MENU_MISSION_COMPLETE ||
         menu == MENU_MISSION_SELECT ||
         menu == MENU_BRIEFING ||
         menu == MENU_RUN_STAGE ||
         menu == MENU_DISPLAY_CAST)) {
        s_assertPostMissionTransition = 1;
    }

    if (traceFlowOnlyEnabled()) {
        char linebuf[8192];
        int nan_count = 0;

        if (has_player) {
            float *checks[] = {
                &px, &py, &pz, &vv_theta, &vv_verta, &vv_verta360,
                &vv_cosverta, &vv_sinverta, &field_70, &stanHeight,
                &col_x, &col_y, &col_z, &cam_x, &cam_y, &cam_z,
                &cam_target_x, &cam_target_y, &cam_target_z,
                &cam_up_x, &cam_up_y, &cam_up_z,
                &cam_floor_x, &cam_floor_y, &cam_floor_z,
                &render_cam_x, &render_cam_y, &render_cam_z,
                &render_cam_target_x, &render_cam_target_y, &render_cam_target_z,
                &render_cam_dx, &render_cam_dy, &render_cam_dz,
                &cam_dx, &cam_dy, &cam_dz,
                &theta0, &theta1, &theta2,
                &headlook_x, &headlook_y, &headlook_z,
                &headup_x, &headup_y, &headup_z,
                &move_speed_forwards, &move_speed_sideways,
                &move_speed_go, &move_speed_strafe, &move_speed_boost,
                &move_speed_theta, &move_speed_verta,
                &move_head_x, &move_head_y, &move_head_z,
                &move_prev_x, &move_prev_y, &move_prev_z,
                &room_basis_x, &room_basis_y, &room_basis_z
            };
            size_t check_count = sizeof(checks) / sizeof(checks[0]);
            for (size_t ci = 0; ci < check_count; ci++) {
                if (!isFinite(*checks[ci])) nan_count++;
            }
        }

        int len = snprintf(linebuf, sizeof(linebuf),
            "{\"f\":%d,\"p\":%d,"
            "\"pos\":[%.2f,%.2f,%.2f],"
            "\"move\":{\"speed\":[%.5f,%.5f],\"raw\":[%.5f,%.5f],\"boost\":%.5f,"
            "\"turn\":%.5f,\"pitch\":%.5f,\"max_t\":%d,"
            "\"head\":[%.3f,%.3f,%.3f],\"prev\":[%.2f,%.2f,%.2f],"
            "\"clock\":%d,\"dt\":%.2f,\"global\":%d},"
            "\"tris\":%d,\"rooms_drawn\":%d,\"crashes\":%d,\"bad_cmds\":%d,"
            "\"dl\":{\"mtx_fail\":%d,\"vtx_fail\":%d,\"dl_fail\":%d,\"movemem_fail\":%d,"
            "\"texture_fail\":%d,\"settimg_fail\":%d,\"non_dl_skip_pc\":%d,"
            "\"non_dl_skip_n64\":%d,\"unregistered_skip\":%d,\"dyn_overflow\":%d,\"hud_image_fault\":%d},"
            "\"rooms\":{\"fallback\":{\"active\":%d,\"rooms\":%d,\"total\":%d}},"
            "\"inv\":{\"count\":%d,\"keyflags\":\"0x%08X\",\"ge_key\":%d,\"ge_copied\":%d},"
            "%s"
            "%s"
            "%s"
            "\"front\":{\"menu\":%d,\"menu_pending\":%d,\"menu_prev\":%d,\"menu_timer\":%d,\"gamemode\":%d,\"stage\":%d,\"difficulty\":%d,"
            "\"loaded_stage\":%d,\"pending_stage\":%d,\"active_stage\":%d,\"mission_state\":%d,\"entered\":%d,"
            "\"cursor\":[%.2f,%.2f],\"folder\":%d,\"hover_folder\":%d,\"folder_option\":%d,\"folder_delete\":%d,\"folder_delete_choice\":%d,"
            "\"highlight\":%d,\"briefing\":%d,\"briefing_page\":%d,\"mission_failed\":%d,\"bond_kia\":%d,"
            "\"all_obj_complete\":%d},"
            "\"assert\":{\"file_select\":%d,\"stage_menu\":%d,\"mission_start\":%d,\"post_mission\":%d},"
            "\"nan\":%d}\n",
            s_traceFrame, trace_player_num,
            px, py, pz,
            move_speed_forwards, move_speed_sideways,
            move_speed_go, move_speed_strafe, move_speed_boost,
            move_speed_theta, move_speed_verta, move_speed_max_time60,
            move_head_x, move_head_y, move_head_z,
            move_prev_x, move_prev_y, move_prev_z,
            g_ClockTimer, g_GlobalTimerDelta, g_GlobalTimer,
            tris, rooms_drawn, g_crashRecoveryCount, bad_cmds,
            dl_mtx_fail, dl_vtx_fail, dl_fail, dl_movemem_fail,
            dl_texture_fail, dl_settimg_fail, dl_non_dl_skip_pc,
            dl_non_dl_skip_n64, dl_unregistered_skip, g_dyn_overflow_count,
            g_hud_image_fault_count,
            room_render_fallback_active, room_render_fallback_rooms, room_render_fallback_total,
            inv_item_count, inv_keyflags, inv_has_ge_key, inv_copied_ge_key,
            objective_field_json,
            save_field_json,
            ramrom_field_json,
            menu, menu_pending, menu_prev, menu_timer, game_mode, stage, difficulty,
            loaded_stage, pending_stage, active_stage, mission_state_id, menu_entered,
            menu_cursor_x, menu_cursor_y, menu_selected_folder, menu_hover_folder, menu_folder_option,
            menu_folder_delete, menu_folder_delete_choice, menu_highlight, menu_briefing, menu_briefing_page,
            mission_failed, bond_kia, all_obj_complete,
            s_assertFileSelectSeen, s_assertStageMenuSelected,
            s_assertMissionStartAuthentic, s_assertPostMissionTransition,
            nan_count);

        if (len > 0) {
            if ((size_t)len >= sizeof(linebuf)) {
                len = (int)sizeof(linebuf) - 1;
                linebuf[len - 1] = '\n';
                linebuf[len] = '\0';
            }
            traceWriteStateLine(linebuf, (size_t)len);
        }
        s_prevMenu = menu;
        return;
    }

    if (trace_live_stage_globals &&
        g_CurrentPlayer != NULL &&
        g_CurrentPlayer->prop != NULL &&
        g_CurrentPlayer->prop->chr != NULL) {
        ChrRecord *intro_chr = g_CurrentPlayer->prop->chr;

        intro_bond_present = 1;
        intro_bond_chrnum = intro_chr->chrnum;
        intro_bond_action = intro_chr->actiontype;
        intro_bond_sleep = intro_chr->sleep;
        intro_bond_field20 = intro_chr->field_20 != NULL ? 1 : 0;
        intro_bond_seen_onscreen =
            (intro_chr->chrflags & CHRFLAG_HAS_BEEN_ON_SCREEN) != 0 ? 1 : 0;

        if (intro_chr->prop != NULL) {
            intro_bond_onscreen =
                (intro_chr->prop->flags & PROPFLAG_ONSCREEN) != 0 ? 1 : 0;
        }

        if (intro_chr->model != NULL && intro_chr->model->render_pos != NULL) {
            intro_bond_model_mtx = modelGetRenderPosCount(intro_chr->model);
        }

        if (intro_chr->model != NULL && intro_chr->model->anim != NULL) {
            ModelAnimation *intro_anim = intro_chr->model->anim;

            intro_bond_anim_valid = 1;
            intro_bond_anim_offset = traceModelAnimationTableOffset(intro_anim);
            intro_bond_anim_frames = (int)traceAnimReadU16(intro_anim, 0x04);
            intro_bond_anim_entry_offset = (int)traceAnimReadU32(intro_anim, 0x08);
            intro_bond_anim_bits_offset = (int)traceAnimReadU32(intro_anim, 0x10);
            intro_bond_anim_hash = traceModelAnimationHeaderHash(intro_anim);
            intro_bond_anim_frame = objecthandlerGetModelField28(intro_chr->model);
            intro_bond_anim_end = sub_GAME_7F06F5C4(intro_chr->model);
            intro_bond_anim_speed = modelGetAnimSpeed(intro_chr->model);
            intro_bond_anim_abs_speed = modelGetAbsAnimSpeed(intro_chr->model);
            intro_bond_anim_looping = intro_chr->model->animlooping;
            intro_bond_anim_gunhand = objecthandlerGetModelGunhand(intro_chr->model);
        }

        traceHeldPropSnapshot(intro_chr->weapons_held[GUNRIGHT], &intro_bond_held_right);
        traceHeldPropSnapshot(intro_chr->weapons_held[GUNLEFT], &intro_bond_held_left);

        traceBuildIntroBondBodyJson(intro_bond_body_json, sizeof(intro_bond_body_json), intro_chr);
    }

    memset(&weapon_left, 0, sizeof(weapon_left));
    if (trace_live_stage_globals) {
        portGetViewmodelTrace(GUNLEFT, &weapon_left);
    }
    if (weapon_left.frame == 0) {
        weapon_left.valid = 0;
    }

    memset(&weapon_right, 0, sizeof(weapon_right));
    if (trace_live_stage_globals) {
        portGetViewmodelTrace(GUNRIGHT, &weapon_right);
    }
    if (weapon_right.frame == 0) {
        weapon_right.valid = 0;
    }
    traceFormatViewmodelMtxJson(weapon_right_mtx_json, sizeof(weapon_right_mtx_json),
                                "wr_mtx", &weapon_right);
    traceFormatViewmodelMtxJson(weapon_left_mtx_json, sizeof(weapon_left_mtx_json),
                                "wl_mtx", &weapon_left);

    if (trace_live_stage_globals) {
        for (int room = 1; room < g_MaxNumRooms; room++) {
            s_room_info *ri = &g_BgRoomInfo[room];

            if (ri->room_rendered) {
                rendered_rooms_count++;
                int rendered_sample_limit = traceFullRoomListsEnabled()
                    ? (int)(sizeof(rendered_rooms_sample) / sizeof(rendered_rooms_sample[0]))
                    : 16;
                if (rendered_rooms_sample_count < rendered_sample_limit) {
                    rendered_rooms_sample[rendered_rooms_sample_count++] = room;
                }
            }

            if (ri->room_neighbor_to_rendered) {
                neighbor_rooms_count++;
            }

            if (ri->model_bin_loaded) {
                loaded_rooms_count++;
            }
        }
    }

    /* NaN/Inf trap — check every float field we trace */
    int nan_count = 0;
    if (has_player) {
        float *checks[] = {
            &px, &py, &pz, &vv_theta, &vv_verta, &vv_verta360,
            &vv_cosverta, &vv_sinverta, &field_70, &stanHeight,
            &col_x, &col_y, &col_z, &cam_x, &cam_y, &cam_z,
            &cam_target_x, &cam_target_y, &cam_target_z,
            &cam_up_x, &cam_up_y, &cam_up_z,
            &cam_floor_x, &cam_floor_y, &cam_floor_z,
            &render_cam_x, &render_cam_y, &render_cam_z,
            &render_cam_target_x, &render_cam_target_y, &render_cam_target_z,
            &render_cam_dx, &render_cam_dy, &render_cam_dz,
            &cam_dx, &cam_dy, &cam_dz,
            &theta0, &theta1, &theta2,
            &headlook_x, &headlook_y, &headlook_z,
            &headup_x, &headup_y, &headup_z,
            &move_speed_forwards, &move_speed_sideways,
            &move_speed_go, &move_speed_strafe, &move_speed_boost,
            &move_speed_theta, &move_speed_verta,
            &move_head_x, &move_head_y, &move_head_z,
            &move_prev_x, &move_prev_y, &move_prev_z,
            &room_basis_x, &room_basis_y, &room_basis_z
        };
        size_t check_count = sizeof(checks) / sizeof(checks[0]);
        for (size_t ci = 0; ci < check_count; ci++) {
            if (!isFinite(*checks[ci])) nan_count++;
        }
    }

    if (trace_live_stage_globals) {
        traceBuildGuardSpawnJson(spawn_field_json, sizeof(spawn_field_json));
        traceBuildShotJson(shot_field_json, sizeof(shot_field_json));
        traceBuildBulletImpactJson(impact_field_json, sizeof(impact_field_json));
        traceBuildImpactStateJson(impact_state_json, sizeof(impact_state_json));
        traceBuildProjectileJson(projectile_field_json, sizeof(projectile_field_json));
        traceBuildGuardHitJson(hit_field_json, sizeof(hit_field_json));
        traceBuildForcedGuardHitJson(forced_hit_field_json, sizeof(forced_hit_field_json));
        traceBuildGuardDropJson(drop_field_json, sizeof(drop_field_json));
        traceBuildActorSummaryJson(actor_summary_json, sizeof(actor_summary_json), has_player, px, py, pz);
        traceBuildCombatOracleJson(combat_oracle_json, sizeof(combat_oracle_json), has_player, shot_count_total);
        traceBuildGlassStateJson(glass_state_json, sizeof(glass_state_json));
        traceBuildGlassProjectionJson(glass_projection_json, sizeof(glass_projection_json));
        traceBuildGlassPropsJson(glass_props_json, sizeof(glass_props_json), has_player, px, py, pz);
    }

    /* Format to a stack buffer first, then write through the direct state-trace
     * helper so crash recovery cannot splice records together through stdio. */
    {
        char linebuf[TRACE_FRAME_JSON_LINE_SIZE];
        char rendered_rooms_buf[1024];
        char draw_rooms_buf[1024];
        int rendered_rooms_len = 0;
        int draw_rooms_len = 0;

        rendered_rooms_buf[rendered_rooms_len++] = '[';
        for (int i = 0; i < rendered_rooms_sample_count; i++) {
            int written = snprintf(
                rendered_rooms_buf + rendered_rooms_len,
                sizeof(rendered_rooms_buf) - (size_t)rendered_rooms_len,
                "%s%d",
                i == 0 ? "" : ",",
                rendered_rooms_sample[i]);
            if (written < 0 || written >= (int)(sizeof(rendered_rooms_buf) - (size_t)rendered_rooms_len)) {
                rendered_rooms_len = 0;
                rendered_rooms_buf[rendered_rooms_len++] = '[';
                break;
            }
            rendered_rooms_len += written;
        }
        if (rendered_rooms_len < (int)sizeof(rendered_rooms_buf) - 1) {
            rendered_rooms_buf[rendered_rooms_len++] = ']';
            rendered_rooms_buf[rendered_rooms_len] = '\0';
        } else {
            strcpy(rendered_rooms_buf, "[]");
        }

        draw_rooms_buf[draw_rooms_len++] = '[';
        if (trace_live_stage_globals) {
            int draw_count = g_BgNumberOfRoomsDrawn;
            if (draw_count < 0) draw_count = 0;
            if (draw_count > (traceFullRoomListsEnabled() ? 204 : 16)) {
                draw_count = traceFullRoomListsEnabled() ? 204 : 16;
            }
            for (int i = 0; i < draw_count; i++) {
                int written = snprintf(
                    draw_rooms_buf + draw_rooms_len,
                    sizeof(draw_rooms_buf) - (size_t)draw_rooms_len,
                    "%s%d",
                    i == 0 ? "" : ",",
                    dword_CODE_bss_8007FFA0[i].roomid);
                if (written < 0 ||
                    written >= (int)(sizeof(draw_rooms_buf) - (size_t)draw_rooms_len)) {
                    draw_rooms_len = 0;
                    draw_rooms_buf[draw_rooms_len++] = '[';
                    break;
                }
                draw_rooms_len += written;
            }
        }
        if (draw_rooms_len < (int)sizeof(draw_rooms_buf) - 1) {
            draw_rooms_buf[draw_rooms_len++] = ']';
            draw_rooms_buf[draw_rooms_len] = '\0';
        } else {
            strcpy(draw_rooms_buf, "[]");
        }

        int len = snprintf(linebuf, sizeof(linebuf),
            "{\"f\":%d,\"p\":%d,"
            "\"pos\":[%.2f,%.2f,%.2f],"
            "\"cam_pos\":[%.4f,%.4f,%.4f],"
            "\"cam_target\":[%.4f,%.4f,%.4f],"
            "\"cam_up\":[%.6f,%.6f,%.6f],"
            "\"cam_floor\":[%.2f,%.2f,%.2f],"
            "\"cam_delta\":[%.2f,%.2f,%.2f],"
            "\"render_cam_pos\":[%.4f,%.4f,%.4f],"
            "\"render_cam_target\":[%.4f,%.4f,%.4f],"
            "\"render_cam_delta\":[%.2f,%.2f,%.2f],"
            "\"room_basis\":[%.2f,%.2f,%.2f],"
            "\"view\":[%d,%d,%d,%d],"
            "\"vi_view\":[%d,%d,%d,%d],"
            "\"theta\":%.4f,"
            "\"floor\":%.2f,\"stan_h\":%.2f,"
            "\"col\":[%.2f,%.2f,%.2f],"
            "\"facing\":[%.4f,%.4f,%.4f],"
            "\"view_basis\":{\"vv_verta\":%.4f,\"vv_verta360\":%.4f,"
            "\"vv_cosverta\":%.6f,\"vv_sinverta\":%.6f,"
            "\"headlook\":[%.4f,%.4f,%.4f],\"headup\":[%.4f,%.4f,%.4f]},"
            "\"move\":{\"speed\":[%.5f,%.5f],\"raw\":[%.5f,%.5f],\"boost\":%.5f,"
            "\"turn\":%.5f,\"pitch\":%.5f,\"max_t\":%d,"
            "\"head\":[%.3f,%.3f,%.3f],\"prev\":[%.2f,%.2f,%.2f],"
            "\"clock\":%d,\"dt\":%.2f,\"global\":%d},"
            "\"rng\":{\"seed\":\"0x%016llX\",\"seed_low\":%u,\"call_count\":%llu},"
            "\"rooms\":{\"tile\":%d,\"portal\":%d,\"room_ptr\":%d,\"prop\":%d,\"cur\":%d,\"render\":%d,\"lookup\":%d,\"nearest\":%d,\"cam_lookup\":%d,\"cam_nearest\":%d,"
            "\"vis\":{\"rendered\":%d,\"neighbor\":%d,\"loaded\":%d,\"sample\":%s,\"draw_sample\":%s},"
            "\"fallback\":{\"active\":%d,\"rooms\":%d,\"total\":%d}},"
            "\"actors\":%s,"
            "%s"
            "\"glass\":%s,"
            "\"glass_projection\":%s,"
            "\"glass_props\":%s,"
            "\"impact_state\":%s,"
            "\"tris\":%d,\"rooms_drawn\":%d,\"crashes\":%d,\"bad_cmds\":%d,"
            "\"dl\":{\"mtx_fail\":%d,\"vtx_fail\":%d,\"dl_fail\":%d,\"movemem_fail\":%d,"
            "\"texture_fail\":%d,\"settimg_fail\":%d,\"non_dl_skip_pc\":%d,"
            "\"non_dl_skip_n64\":%d,\"unregistered_skip\":%d,\"dyn_overflow\":%d,\"hud_image_fault\":%d},"
            "\"fog\":[%d,%d,%d],\"fog_mul\":%d,\"fog_off\":%d,"
            "\"geom\":\"0x%08X\","
            "\"segs\":\"0x%04X\","
            "\"watch\":{\"state\":%d,\"page\":%d,\"brief\":%d,\"pause\":%d,\"open_req\":%d,\"pausing\":%d,"
            "\"hands\":{\"ready\":[%d,%d],\"item\":[%d,%d],\"active\":[%d,%d],\"weapon\":[%d,%d],\"next\":[%d,%d],\"pending\":[%d,%d],\"invis\":[%d,%d],\"det\":%d,\"disarm\":%d}},"
            "\"combat\":{\"aim_mode\":%d,\"gunsightmode\":%d,\"autoaim_opt\":%d,\"enabled\":[%d,%d],"
            "\"autoaim\":[%.4f,%.4f],\"crosshair\":[%.2f,%.2f],\"gunsight\":[%.2f,%.2f],\"autoaim_time\":[%d,%d],\"target_same\":%d,"
            "\"health\":{\"bond\":%.4f,\"armor\":%.4f,\"actual_h\":%.4f,\"actual_a\":%.4f,"
            "\"damage_show\":%d,\"health_show\":%d,\"damage_type\":%d,\"health_type\":%d,"
            "\"fade_rgba\":[%d,%d,%d,%.4f]},"
            "\"shots\":{\"total\":%d,\"regs\":[%d,%d,%d,%d,%d,%d,%d]},\"casings\":%d,"
            "\"scan\":{\"nearest\":{\"type\":%d,\"chrnum\":%d,\"hidden\":%d,\"alive\":%d,"
            "\"action\":%d,\"alert\":%d,\"firecount\":%d,"
            "\"hidden_bits\":%d,\"sleep\":%d,"
            "\"ai\":{\"list\":%d,\"global\":%d,\"offset\":%d,\"return\":%d,\"cmd\":%d,\"arg0\":%d},"
            "\"sense\":{\"seen_age\":%d,\"heard_age\":%d,\"seen_recent\":%d,\"heard_recent\":%d},"
            "\"vis\":{\"line_clear\":%d,\"same_stan\":%d,\"could_see_bond\":%d,\"line_clear_world\":%d,\"line_clear_solid\":%d},"
            "\"blocker\":{\"type\":%d,\"chrnum\":%d,\"self\":%d,\"dist\":%.2f},"
            "\"damage\":%.4f,\"maxdamage\":%.4f,\"dist\":%.2f,"
            "\"pos\":[%.2f,%.2f,%.2f],\"delta\":[%.2f,%.2f,%.2f]}},"
            "\"target_x\":{\"type\":%d,\"chrnum\":%d,\"hidden\":%d,\"alive\":%d,"
            "\"action\":%d,\"alert\":%d,\"firecount\":%d,"
            "\"hidden_bits\":%d,\"sleep\":%d,"
            "\"ai\":{\"list\":%d,\"global\":%d,\"offset\":%d,\"return\":%d,\"cmd\":%d,\"arg0\":%d},"
            "\"sense\":{\"seen_age\":%d,\"heard_age\":%d,\"seen_recent\":%d,\"heard_recent\":%d},"
            "\"vis\":{\"line_clear\":%d,\"same_stan\":%d,\"could_see_bond\":%d},"
            "\"damage\":%.4f,\"maxdamage\":%.4f,\"dist\":%.2f,"
            "\"pos\":[%.2f,%.2f,%.2f],\"delta\":[%.2f,%.2f,%.2f]},"
            "\"target_y\":{\"type\":%d,\"chrnum\":%d,\"hidden\":%d,\"alive\":%d,"
            "\"action\":%d,\"alert\":%d,\"firecount\":%d,"
            "\"hidden_bits\":%d,\"sleep\":%d,"
            "\"ai\":{\"list\":%d,\"global\":%d,\"offset\":%d,\"return\":%d,\"cmd\":%d,\"arg0\":%d},"
            "\"sense\":{\"seen_age\":%d,\"heard_age\":%d,\"seen_recent\":%d,\"heard_recent\":%d},"
            "\"vis\":{\"line_clear\":%d,\"same_stan\":%d,\"could_see_bond\":%d},"
            "\"damage\":%.4f,\"maxdamage\":%.4f,\"dist\":%.2f,"
            "\"pos\":[%.2f,%.2f,%.2f],\"delta\":[%.2f,%.2f,%.2f]}},"
            "\"tank\":{\"in\":%d,\"can_enter\":%d,\"world_present\":%d,\"player_present\":%d,"
            "\"world_obj\":%d,\"world_pad\":%d,\"player_obj\":%d,\"player_pad\":%d,"
            "\"stored_ammo\":%d,\"reserve_ammo\":%d,\"firing\":%d,"
            "\"yoff\":%.2f,\"orient\":%.4f,\"turret_h\":%.4f,\"turret_v\":%.4f,"
            "\"world_pos\":[%.2f,%.2f,%.2f],\"player_pos\":[%.2f,%.2f,%.2f]},"
            "\"inv\":{\"count\":%d,\"keyflags\":\"0x%08X\",\"ge_key\":%d,\"ge_copied\":%d},"
            "%s"
            "%s"
            "%s"
            "%s"
            "%s"
            "%s"
            "%s"
            "%s"
            "%s"
            "%s"
            "%s"
            "%s"
            "\"front\":{\"menu\":%d,\"menu_pending\":%d,\"menu_prev\":%d,\"menu_timer\":%d,\"gamemode\":%d,\"stage\":%d,\"difficulty\":%d,"
            "\"loaded_stage\":%d,\"pending_stage\":%d,\"active_stage\":%d,\"mission_state\":%d,\"entered\":%d,"
            "\"cursor\":[%.2f,%.2f],\"folder\":%d,\"hover_folder\":%d,\"folder_option\":%d,\"folder_delete\":%d,\"folder_delete_choice\":%d,"
            "\"highlight\":%d,\"briefing\":%d,\"briefing_page\":%d,\"mission_failed\":%d,\"bond_kia\":%d,"
            "\"all_obj_complete\":%d},"
            "\"intro\":{\"timer\":%.2f,"
            "\"setup\":{\"anim_index\":%d,\"swirl\":{\"present\":%d,\"count\":%d,\"hash\":\"0x%016llX\","
            "\"current\":{\"index\":%d,\"flags\":%d,\"pos\":[%.4f,%.4f,%.4f],"
            "\"curve\":%.4f,\"duration\":%.4f,\"pad\":%d}}},"
            "\"bond_present\":%d,\"bond_chrnum\":%d,\"bond_action\":%d,\"bond_sleep\":%d,"
            "\"bond_field20\":%d,\"bond_model_mtx\":%d,\"bond_onscreen\":%d,\"bond_seen_onscreen\":%d,"
            "\"bond_rendered\":%d,\"bond_render_count\":%d,\"bond_render_chrnum\":%d,\"bond_render_pass\":%d,"
            "\"bond_anim\":{\"valid\":%d,\"offset\":%d,\"frames\":%d,\"hash\":\"0x%016llX\","
            "\"entry_offset\":%d,\"bits_offset\":%d,\"frame\":%.2f,\"end\":%.2f,\"speed\":%.4f,"
            "\"abs_speed\":%.4f,\"looping\":%d,\"gunhand\":%d},"
            "\"bond_held\":{\"right\":{\"present\":%d,\"item\":%d,\"obj\":%d,\"has_mtx\":%d},"
            "\"left\":{\"present\":%d,\"item\":%d,\"obj\":%d,\"has_mtx\":%d}},"
            "\"bond_body\":%s,"
            "\"selected_camera\":{\"present\":%d,\"index\":%d,\"count\":%d,"
            "\"pos\":[%.2f,%.2f,%.2f],\"yaw\":%.6f,\"pitch\":%.6f,"
            "\"pad\":%d,\"pad_room\":%d,\"pad_pos\":[%.2f,%.2f,%.2f]}},"
            "\"title\":{\"gunbarrel_mode\":%d,\"eye_counter\":%d,\"blood_state\":%d,"
            "\"title_x\":%.4f,\"title_y\":%.4f,\"transition_x\":%.4f,\"transition_y\":%.4f,"
            "\"wave\":%d,\"rare_rotation\":%.4f,\"nintendo_rotation\":%.4f,\"nintendo_scale\":%.6f},"
            "\"assert\":{\"file_select\":%d,\"stage_menu\":%d,\"mission_start\":%d,\"post_mission\":%d},"
            "\"nan\":%d,"
            "\"cam\":%d,\"cam_after\":%d,\"icam\":%d,\"p_unk\":%d,"
            "\"wr_valid\":%d,\"wr_item\":%d,\"wr_vis\":%d,\"wr_fire\":%d,\"wr_flash\":%d,\"wr_state\":%d,"
            "\"wr_hold\":%d,\"wr_switch1\":%d,\"wr_switches\":%d,\"wr_s060\":%d,\"wr_s078\":%d,"
            "\"wr_sxflash\":%d,\"wr_shell_l\":%d,\"wr_shell_r\":%d,"
            "\"wr_cuff\":[%d,%d,%d,%d,%d,%d],"
            "\"wr_raw\":{\"item\":%d,\"pending\":%d,\"invis\":%d,\"lock\":%d,\"mag\":%d,"
            "\"weaponnum\":%d,\"watchmenu\":%d,\"anim\":%d,"
            "\"ammo_type\":%d,\"reserve\":%d,\"mag_size\":%d,\"flags\":%u},"
            "\"wr_a84\":%.5f,\"wr_a88\":%.5f,"
            "\"wr_pose\":{\"smooth\":[%.4f,%.4f,%.4f],"
            "\"base\":[%.4f,%.4f,%.4f],"
            "\"target\":[%.4f,%.4f,%.4f],"
            "\"stats\":[%.4f,%.4f,%.4f],"
            "\"screen\":[%.4f,%.4f,%.4f,%.4f,%.4f],"
            "\"accum\":[%.4f,%.4f,%.4f],"
            "\"player\":[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f],"
            "\"blend_meta\":[%d,%d],"
            "\"blend_state\":[%.4f,%.4f,%.4f,%.4f,%.4f],"
            "\"blendpos\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]]},"
            "\"wr_root\":[%.2f,%.2f,%.2f],"
            "\"wr_render_root\":[%.2f,%.2f,%.2f],\"wr_render_root_diag\":[%.4f,%.4f,%.4f],"
            "\"wr_world\":[%.2f,%.2f,%.2f],\"wr_muzzle\":[%.2f,%.2f,%.2f],"
            "\"wr_sw2\":[%.2f,%.2f,%.2f],\"wr_sw3\":[%.2f,%.2f,%.2f],"
            "\"wr_sw4\":[%.2f,%.2f,%.2f],"
            "\"wr_sw6\":[%.2f,%.2f,%.2f],\"wr_sw7\":[%.2f,%.2f,%.2f],"
            "\"wr_sw2_basis\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]],"
            "\"wr_sw3_basis\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]],"
            "\"wr_sw4_basis\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]],"
            "\"wr_sw6_basis\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]],"
            "\"wr_sw7_basis\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]],"
            "\"wr_depth\":%.2f,"
            "%s"
            "\"wl_valid\":%d,\"wl_item\":%d,\"wl_vis\":%d,\"wl_fire\":%d,\"wl_flash\":%d,\"wl_state\":%d,"
            "\"wl_hold\":%d,\"wl_switch1\":%d,\"wl_switches\":%d,\"wl_s060\":%d,\"wl_s078\":%d,"
            "\"wl_sxflash\":%d,\"wl_shell_l\":%d,\"wl_shell_r\":%d,"
            "\"wl_cuff\":[%d,%d,%d,%d,%d,%d],"
            "\"wl_raw\":{\"item\":%d,\"pending\":%d,\"invis\":%d,\"lock\":%d,\"mag\":%d,"
            "\"weaponnum\":%d,\"watchmenu\":%d,\"anim\":%d,"
            "\"ammo_type\":%d,\"reserve\":%d,\"mag_size\":%d,\"flags\":%u},"
            "\"wl_a84\":%.5f,\"wl_a88\":%.5f,"
            "\"wl_pose\":{\"smooth\":[%.4f,%.4f,%.4f],"
            "\"base\":[%.4f,%.4f,%.4f],"
            "\"target\":[%.4f,%.4f,%.4f],"
            "\"stats\":[%.4f,%.4f,%.4f],"
            "\"screen\":[%.4f,%.4f,%.4f,%.4f,%.4f],"
            "\"accum\":[%.4f,%.4f,%.4f],"
            "\"player\":[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f],"
            "\"blend_meta\":[%d,%d],"
            "\"blend_state\":[%.4f,%.4f,%.4f,%.4f,%.4f],"
            "\"blendpos\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]]},"
            "\"wl_root\":[%.2f,%.2f,%.2f],"
            "\"wl_render_root\":[%.2f,%.2f,%.2f],\"wl_render_root_diag\":[%.4f,%.4f,%.4f],"
            "\"wl_world\":[%.2f,%.2f,%.2f],\"wl_muzzle\":[%.2f,%.2f,%.2f],"
            "\"wl_sw2\":[%.2f,%.2f,%.2f],\"wl_sw3\":[%.2f,%.2f,%.2f],"
            "\"wl_sw4\":[%.2f,%.2f,%.2f],"
            "\"wl_sw6\":[%.2f,%.2f,%.2f],\"wl_sw7\":[%.2f,%.2f,%.2f],"
            "\"wl_sw2_basis\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]],"
            "\"wl_sw3_basis\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]],"
            "\"wl_sw4_basis\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]],"
            "\"wl_sw6_basis\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]],"
            "\"wl_sw7_basis\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]],"
            "\"wl_depth\":%.2f,"
            "%s"
            "\"stub_hits\":{\"snd_total\":%u,\"snd_play\":%u,\"snd_legacy_overlap\":%u,"
            "\"snd_new_player_init\":%u,\"snd_get_playing_state\":%u,\"snd_deactivate\":%u,"
            "\"snd_deactivate_all_1\":%u,\"snd_deactivate_all_11\":%u,\"snd_deactivate_all_3\":%u,"
            "\"snd_create_post_event\":%u,\"snd_dispose_sound\":%u,\"snd_set_priority\":%u,"
            "\"snd_unlink_clear\":%u,\"snd_count_alloc_list\":%u},"
            "\"mp\":{\"time_ticks\":%d,\"player_count\":%d}}\n",
            s_traceFrame, trace_player_num,
            px, py, pz,
            cam_x, cam_y, cam_z,
            cam_target_x, cam_target_y, cam_target_z,
            cam_up_x, cam_up_y, cam_up_z,
            cam_floor_x, cam_floor_y, cam_floor_z,
            cam_dx, cam_dy, cam_dz,
            render_cam_x, render_cam_y, render_cam_z,
            render_cam_target_x, render_cam_target_y, render_cam_target_z,
            render_cam_dx, render_cam_dy, render_cam_dz,
            room_basis_x, room_basis_y, room_basis_z,
            view_left, view_top, view_w, view_h,
            vi_view_left, vi_view_top, vi_view_w, vi_view_h,
            vv_theta,
            field_70, stanHeight,
            col_x, col_y, col_z,
            theta0, theta1, theta2,
            vv_verta, vv_verta360, vv_cosverta, vv_sinverta,
            headlook_x, headlook_y, headlook_z,
            headup_x, headup_y, headup_z,
            move_speed_forwards, move_speed_sideways,
            move_speed_go, move_speed_strafe, move_speed_boost,
            move_speed_theta, move_speed_verta, move_speed_max_time60,
            move_head_x, move_head_y, move_head_z,
            move_prev_x, move_prev_y, move_prev_z,
            g_ClockTimer, g_GlobalTimerDelta, g_GlobalTimer,
            (unsigned long long)g_randomSeed,
            (unsigned int)(g_randomSeed & 0xFFFFFFFFULL),
            (unsigned long long)pcRandomGetNextCallCount(),
            tile_room, portal_room, room_ptr_room, prop_room, cur_room, render_room, lookup_room, nearest_room,
            cam_lookup_room, cam_nearest_room,
            rendered_rooms_count, neighbor_rooms_count, loaded_rooms_count, rendered_rooms_buf, draw_rooms_buf,
            room_render_fallback_active, room_render_fallback_rooms, room_render_fallback_total,
            actor_summary_json,
            combat_oracle_json,
            glass_state_json[0] ? glass_state_json : "{\"present\":0,\"buffer_len\":0,\"next\":0,\"active\":0,\"first\":{\"index\":-1},\"sample\":[],\"hash\":\"0x0000000000000000\"}",
            glass_projection_json[0] ? glass_projection_json : "{\"present\":0,\"emit_enabled\":0,\"projection_valid\":0,\"active\":0,\"projected\":0,\"onscreen\":0,\"behind\":0,\"sample\":[]}",
            glass_props_json[0] ? glass_props_json : "{\"present\":0,\"count\":0,\"live\":0,\"with_prop\":0,\"with_model\":0,\"destroyed\":0,\"remove\":0,\"truncated\":0,\"nearest\":{\"index\":-1},\"first_removed\":{\"index\":-1},\"first_destroyed\":{\"index\":-1},\"sample\":[],\"sample_truncated\":0,\"hash\":\"0x0000000000000000\"}",
            impact_state_json[0] ? impact_state_json : "{\"present\":0,\"buffer_len\":0,\"current_slot\":-1,\"occupied\":0,\"first\":{\"index\":-1},\"sample\":[],\"hash\":\"0x0000000000000000\"}",
            tris, rooms_drawn, g_crashRecoveryCount, bad_cmds,
            dl_mtx_fail, dl_vtx_fail, dl_fail, dl_movemem_fail,
            dl_texture_fail, dl_settimg_fail, dl_non_dl_skip_pc,
            dl_non_dl_skip_n64, dl_unregistered_skip, g_dyn_overflow_count,
            g_hud_image_fault_count,
            fog_r, fog_g, fog_b, fog_mul, fog_off,
            geom_mode,
            seg_mask,
            watch_state, watch_page, watch_brief, watch_pause, watch_open_req, pausing,
            hand_ready_left, hand_ready_right,
            hand_item_left, hand_item_right,
            hand_active_left, hand_active_right,
            hand_weapon_left, hand_weapon_right,
            hand_next_left, hand_next_right,
            hand_pending_left, hand_pending_right,
            hand_invis_left, hand_invis_right,
            det_state_right, force_disarm,
            aim_mode, gunsightmode, autoaim_opt, autoaim_enabled_x, autoaim_enabled_y,
            autoaimx, autoaimy, crosshair_x, crosshair_y, gunsight_x, gunsight_y,
            autoxaimtime60, autoyaimtime60, autoaim_same_target,
            bondhealth, bondarmour, actual_health, actual_armor,
            damageshowtime, healthshowtime, damagetype, health_damage_type,
            colour_screen_red, colour_screen_green, colour_screen_blue, colour_screen_frac,
            shot_count_total,
            shot_counts[0], shot_counts[1], shot_counts[2], shot_counts[3], shot_counts[4], shot_counts[5], shot_counts[6],
            casing_count,
            nearest_guard_type, nearest_guard_chrnum, nearest_guard_hidden, nearest_guard_alive,
            nearest_guard_actiontype, nearest_guard_alertness, nearest_guard_firecount,
            nearest_guard_hidden_bits, nearest_guard_sleep,
            nearest_guard_ai_list, nearest_guard_ai_global, nearest_guard_ai_offset, nearest_guard_ai_return,
            nearest_guard_ai_cmd, nearest_guard_ai_arg0,
            nearest_guard_seen_age, nearest_guard_heard_age, nearest_guard_seen_recent, nearest_guard_heard_recent,
            nearest_guard_line_clear, nearest_guard_same_stan, nearest_guard_could_see_bond,
            nearest_guard_line_clear_world, nearest_guard_line_clear_solid,
            nearest_guard_blocker_type, nearest_guard_blocker_chrnum, nearest_guard_blocker_self, nearest_guard_blocker_distance,
            nearest_guard_damage, nearest_guard_maxdamage, nearest_guard_distance,
            nearest_guard_pos_x, nearest_guard_pos_y, nearest_guard_pos_z,
            nearest_guard_delta_x, nearest_guard_delta_y, nearest_guard_delta_z,
            autoaim_target_x_type, autoaim_target_x_chrnum, autoaim_target_x_hidden, autoaim_target_x_alive,
            autoaim_target_x_actiontype, autoaim_target_x_alertness, autoaim_target_x_firecount,
            autoaim_target_x_hidden_bits, autoaim_target_x_sleep,
            autoaim_target_x_ai_list, autoaim_target_x_ai_global, autoaim_target_x_ai_offset, autoaim_target_x_ai_return,
            autoaim_target_x_ai_cmd, autoaim_target_x_ai_arg0,
            autoaim_target_x_seen_age, autoaim_target_x_heard_age, autoaim_target_x_seen_recent, autoaim_target_x_heard_recent,
            autoaim_target_x_line_clear, autoaim_target_x_same_stan, autoaim_target_x_could_see_bond,
            autoaim_target_x_damage, autoaim_target_x_maxdamage, autoaim_target_x_distance,
            autoaim_target_x_pos_x, autoaim_target_x_pos_y, autoaim_target_x_pos_z,
            autoaim_target_x_delta_x, autoaim_target_x_delta_y, autoaim_target_x_delta_z,
            autoaim_target_y_type, autoaim_target_y_chrnum, autoaim_target_y_hidden, autoaim_target_y_alive,
            autoaim_target_y_actiontype, autoaim_target_y_alertness, autoaim_target_y_firecount,
            autoaim_target_y_hidden_bits, autoaim_target_y_sleep,
            autoaim_target_y_ai_list, autoaim_target_y_ai_global, autoaim_target_y_ai_offset, autoaim_target_y_ai_return,
            autoaim_target_y_ai_cmd, autoaim_target_y_ai_arg0,
            autoaim_target_y_seen_age, autoaim_target_y_heard_age, autoaim_target_y_seen_recent, autoaim_target_y_heard_recent,
            autoaim_target_y_line_clear, autoaim_target_y_same_stan, autoaim_target_y_could_see_bond,
            autoaim_target_y_damage, autoaim_target_y_maxdamage, autoaim_target_y_distance,
            autoaim_target_y_pos_x, autoaim_target_y_pos_y, autoaim_target_y_pos_z,
            autoaim_target_y_delta_x, autoaim_target_y_delta_y, autoaim_target_y_delta_z,
            tank_in, tank_can_enter, tank_world_present, tank_player_present,
            tank_world_obj, tank_world_pad, tank_player_obj, tank_player_pad,
            tank_stored_ammo, tank_reserve_ammo, tank_firing,
            g_PlayerTankYOffset, g_TankOrientationAngle, g_TankTurretOrientationAngleRad, g_TankTurretVerticalAngle,
            tank_world_x, tank_world_y, tank_world_z,
            tank_player_x, tank_player_y, tank_player_z,
            inv_item_count, inv_keyflags, inv_has_ge_key, inv_copied_ge_key,
            objective_field_json,
            alarm_field_json,
            tracked_chr_field_json,
            spawn_field_json,
            shot_field_json,
            impact_field_json,
            projectile_field_json,
            hit_field_json,
            forced_hit_field_json,
            drop_field_json,
            save_field_json,
            ramrom_field_json,
            menu, menu_pending, menu_prev, menu_timer, game_mode, stage, difficulty,
            loaded_stage, pending_stage, active_stage, mission_state_id, menu_entered,
            menu_cursor_x, menu_cursor_y, menu_selected_folder, menu_hover_folder, menu_folder_option,
            menu_folder_delete, menu_folder_delete_choice, menu_highlight, menu_briefing, menu_briefing_page,
            mission_failed, bond_kia, all_obj_complete,
            intro_timer,
            intro_setup_anim_index,
            intro_swirl_present, intro_swirl_count, (unsigned long long)intro_swirl_hash,
            intro_swirl_current_index, intro_swirl_current_flags,
            intro_swirl_current_x, intro_swirl_current_y, intro_swirl_current_z,
            intro_swirl_current_curve, intro_swirl_current_duration, intro_swirl_current_pad,
            intro_bond_present, intro_bond_chrnum, intro_bond_action, intro_bond_sleep,
            intro_bond_field20, intro_bond_model_mtx, intro_bond_onscreen,
            intro_bond_seen_onscreen, intro_bond_rendered, s_bondIntroRenderCount,
            s_bondIntroLastChrnum, s_bondIntroLastPass,
            intro_bond_anim_valid, intro_bond_anim_offset, intro_bond_anim_frames,
            (unsigned long long)intro_bond_anim_hash,
            intro_bond_anim_entry_offset, intro_bond_anim_bits_offset,
            intro_bond_anim_frame, intro_bond_anim_end,
            intro_bond_anim_speed, intro_bond_anim_abs_speed, intro_bond_anim_looping,
            intro_bond_anim_gunhand,
            intro_bond_held_right.present, intro_bond_held_right.item,
            intro_bond_held_right.obj, intro_bond_held_right.has_mtx,
            intro_bond_held_left.present, intro_bond_held_left.item,
            intro_bond_held_left.obj, intro_bond_held_left.has_mtx,
            intro_bond_body_json,
            intro_selected_camera_present, intro_selected_camera_index,
            intro_selected_camera_count,
            intro_selected_camera_x, intro_selected_camera_y, intro_selected_camera_z,
            intro_selected_camera_yaw, intro_selected_camera_pitch,
            intro_selected_camera_pad, intro_selected_camera_pad_room,
            intro_selected_camera_pad_x, intro_selected_camera_pad_y, intro_selected_camera_pad_z,
            title_gunbarrel_mode, title_eye_counter, title_blood_state,
            title_x, title_y, title_transition_x, title_transition_y,
            title_wave, title_rare_rotation, title_nintendo_rotation, title_nintendo_scale,
            s_assertFileSelectSeen, s_assertStageMenuSelected, s_assertMissionStartAuthentic, s_assertPostMissionTransition,
            nan_count,
            cam_mode, cam_after, icam, player_unknown,
            weapon_right.valid, weapon_right.item, weapon_right.visible,
            weapon_right.firing, weapon_right.flash, weapon_right.state,
            weapon_right.hold_time, weapon_right.switch1, weapon_right.switch_count,
            weapon_right.suppress_0x60, weapon_right.suppress_0x78,
            weapon_right.suppress_extra_flash, weapon_right.shell_left_mask,
            weapon_right.shell_right_mask,
            weapon_right.cuff[0], weapon_right.cuff[1], weapon_right.cuff[2],
            weapon_right.cuff[3], weapon_right.cuff[4], weapon_right.cuff[5],
            weapon_right.raw_hand_item, weapon_right.raw_pending,
            weapon_right.raw_invis, weapon_right.raw_lock, weapon_right.raw_mag,
            weapon_right.raw_weaponnum, weapon_right.raw_watchmenu,
            weapon_right.raw_animation,
            weapon_right.raw_ammo_type, weapon_right.raw_ammo_reserve,
            weapon_right.raw_mag_size, weapon_right.raw_flags,
            weapon_right.recoil_angle, weapon_right.bolt_recoil,
            weapon_right.pose_smooth[0], weapon_right.pose_smooth[1], weapon_right.pose_smooth[2],
            weapon_right.pose_base[0], weapon_right.pose_base[1], weapon_right.pose_base[2],
            weapon_right.pose_target[0], weapon_right.pose_target[1], weapon_right.pose_target[2],
            weapon_right.pose_stats[0], weapon_right.pose_stats[1], weapon_right.pose_stats[2],
            weapon_right.pose_screen[0], weapon_right.pose_screen[1], weapon_right.pose_screen[2],
            weapon_right.pose_screen[3], weapon_right.pose_screen[4],
            weapon_right.pose_accum[0], weapon_right.pose_accum[1], weapon_right.pose_accum[2],
            weapon_right.pose_player[0], weapon_right.pose_player[1], weapon_right.pose_player[2],
            weapon_right.pose_player[3], weapon_right.pose_player[4], weapon_right.pose_player[5],
            weapon_right.pose_player[6],
            weapon_right.pose_blend_meta[0], weapon_right.pose_blend_meta[1],
            weapon_right.pose_blend_state[0], weapon_right.pose_blend_state[1],
            weapon_right.pose_blend_state[2], weapon_right.pose_blend_state[3],
            weapon_right.pose_blend_state[4],
            weapon_right.pose_blendpos[0][0], weapon_right.pose_blendpos[0][1],
            weapon_right.pose_blendpos[0][2],
            weapon_right.pose_blendpos[1][0], weapon_right.pose_blendpos[1][1],
            weapon_right.pose_blendpos[1][2],
            weapon_right.pose_blendpos[2][0], weapon_right.pose_blendpos[2][1],
            weapon_right.pose_blendpos[2][2],
            weapon_right.pose_blendpos[3][0], weapon_right.pose_blendpos[3][1],
            weapon_right.pose_blendpos[3][2],
            weapon_right.root[0], weapon_right.root[1], weapon_right.root[2],
            weapon_right.render_root[0], weapon_right.render_root[1], weapon_right.render_root[2],
            weapon_right.render_root_diag[0], weapon_right.render_root_diag[1], weapon_right.render_root_diag[2],
            weapon_right.world[0], weapon_right.world[1], weapon_right.world[2],
            weapon_right.muzzle[0], weapon_right.muzzle[1], weapon_right.muzzle[2],
            weapon_right.switch2[0], weapon_right.switch2[1], weapon_right.switch2[2],
            weapon_right.switch3[0], weapon_right.switch3[1], weapon_right.switch3[2],
            weapon_right.switch4[0], weapon_right.switch4[1], weapon_right.switch4[2],
            weapon_right.switch6[0], weapon_right.switch6[1], weapon_right.switch6[2],
            weapon_right.switch7[0], weapon_right.switch7[1], weapon_right.switch7[2],
            weapon_right.switch2_basis[0][0], weapon_right.switch2_basis[0][1], weapon_right.switch2_basis[0][2],
            weapon_right.switch2_basis[1][0], weapon_right.switch2_basis[1][1], weapon_right.switch2_basis[1][2],
            weapon_right.switch2_basis[2][0], weapon_right.switch2_basis[2][1], weapon_right.switch2_basis[2][2],
            weapon_right.switch3_basis[0][0], weapon_right.switch3_basis[0][1], weapon_right.switch3_basis[0][2],
            weapon_right.switch3_basis[1][0], weapon_right.switch3_basis[1][1], weapon_right.switch3_basis[1][2],
            weapon_right.switch3_basis[2][0], weapon_right.switch3_basis[2][1], weapon_right.switch3_basis[2][2],
            weapon_right.switch4_basis[0][0], weapon_right.switch4_basis[0][1], weapon_right.switch4_basis[0][2],
            weapon_right.switch4_basis[1][0], weapon_right.switch4_basis[1][1], weapon_right.switch4_basis[1][2],
            weapon_right.switch4_basis[2][0], weapon_right.switch4_basis[2][1], weapon_right.switch4_basis[2][2],
            weapon_right.switch6_basis[0][0], weapon_right.switch6_basis[0][1], weapon_right.switch6_basis[0][2],
            weapon_right.switch6_basis[1][0], weapon_right.switch6_basis[1][1], weapon_right.switch6_basis[1][2],
            weapon_right.switch6_basis[2][0], weapon_right.switch6_basis[2][1], weapon_right.switch6_basis[2][2],
            weapon_right.switch7_basis[0][0], weapon_right.switch7_basis[0][1], weapon_right.switch7_basis[0][2],
            weapon_right.switch7_basis[1][0], weapon_right.switch7_basis[1][1], weapon_right.switch7_basis[1][2],
            weapon_right.switch7_basis[2][0], weapon_right.switch7_basis[2][1], weapon_right.switch7_basis[2][2],
            weapon_right.depth,
            weapon_right_mtx_json,
            weapon_left.valid, weapon_left.item, weapon_left.visible,
            weapon_left.firing, weapon_left.flash, weapon_left.state,
            weapon_left.hold_time, weapon_left.switch1, weapon_left.switch_count,
            weapon_left.suppress_0x60, weapon_left.suppress_0x78,
            weapon_left.suppress_extra_flash, weapon_left.shell_left_mask,
            weapon_left.shell_right_mask,
            weapon_left.cuff[0], weapon_left.cuff[1], weapon_left.cuff[2],
            weapon_left.cuff[3], weapon_left.cuff[4], weapon_left.cuff[5],
            weapon_left.raw_hand_item, weapon_left.raw_pending,
            weapon_left.raw_invis, weapon_left.raw_lock, weapon_left.raw_mag,
            weapon_left.raw_weaponnum, weapon_left.raw_watchmenu,
            weapon_left.raw_animation,
            weapon_left.raw_ammo_type, weapon_left.raw_ammo_reserve,
            weapon_left.raw_mag_size, weapon_left.raw_flags,
            weapon_left.recoil_angle, weapon_left.bolt_recoil,
            weapon_left.pose_smooth[0], weapon_left.pose_smooth[1], weapon_left.pose_smooth[2],
            weapon_left.pose_base[0], weapon_left.pose_base[1], weapon_left.pose_base[2],
            weapon_left.pose_target[0], weapon_left.pose_target[1], weapon_left.pose_target[2],
            weapon_left.pose_stats[0], weapon_left.pose_stats[1], weapon_left.pose_stats[2],
            weapon_left.pose_screen[0], weapon_left.pose_screen[1], weapon_left.pose_screen[2],
            weapon_left.pose_screen[3], weapon_left.pose_screen[4],
            weapon_left.pose_accum[0], weapon_left.pose_accum[1], weapon_left.pose_accum[2],
            weapon_left.pose_player[0], weapon_left.pose_player[1], weapon_left.pose_player[2],
            weapon_left.pose_player[3], weapon_left.pose_player[4], weapon_left.pose_player[5],
            weapon_left.pose_player[6],
            weapon_left.pose_blend_meta[0], weapon_left.pose_blend_meta[1],
            weapon_left.pose_blend_state[0], weapon_left.pose_blend_state[1],
            weapon_left.pose_blend_state[2], weapon_left.pose_blend_state[3],
            weapon_left.pose_blend_state[4],
            weapon_left.pose_blendpos[0][0], weapon_left.pose_blendpos[0][1],
            weapon_left.pose_blendpos[0][2],
            weapon_left.pose_blendpos[1][0], weapon_left.pose_blendpos[1][1],
            weapon_left.pose_blendpos[1][2],
            weapon_left.pose_blendpos[2][0], weapon_left.pose_blendpos[2][1],
            weapon_left.pose_blendpos[2][2],
            weapon_left.pose_blendpos[3][0], weapon_left.pose_blendpos[3][1],
            weapon_left.pose_blendpos[3][2],
            weapon_left.root[0], weapon_left.root[1], weapon_left.root[2],
            weapon_left.render_root[0], weapon_left.render_root[1], weapon_left.render_root[2],
            weapon_left.render_root_diag[0], weapon_left.render_root_diag[1], weapon_left.render_root_diag[2],
            weapon_left.world[0], weapon_left.world[1], weapon_left.world[2],
            weapon_left.muzzle[0], weapon_left.muzzle[1], weapon_left.muzzle[2],
            weapon_left.switch2[0], weapon_left.switch2[1], weapon_left.switch2[2],
            weapon_left.switch3[0], weapon_left.switch3[1], weapon_left.switch3[2],
            weapon_left.switch4[0], weapon_left.switch4[1], weapon_left.switch4[2],
            weapon_left.switch6[0], weapon_left.switch6[1], weapon_left.switch6[2],
            weapon_left.switch7[0], weapon_left.switch7[1], weapon_left.switch7[2],
            weapon_left.switch2_basis[0][0], weapon_left.switch2_basis[0][1], weapon_left.switch2_basis[0][2],
            weapon_left.switch2_basis[1][0], weapon_left.switch2_basis[1][1], weapon_left.switch2_basis[1][2],
            weapon_left.switch2_basis[2][0], weapon_left.switch2_basis[2][1], weapon_left.switch2_basis[2][2],
            weapon_left.switch3_basis[0][0], weapon_left.switch3_basis[0][1], weapon_left.switch3_basis[0][2],
            weapon_left.switch3_basis[1][0], weapon_left.switch3_basis[1][1], weapon_left.switch3_basis[1][2],
            weapon_left.switch3_basis[2][0], weapon_left.switch3_basis[2][1], weapon_left.switch3_basis[2][2],
            weapon_left.switch4_basis[0][0], weapon_left.switch4_basis[0][1], weapon_left.switch4_basis[0][2],
            weapon_left.switch4_basis[1][0], weapon_left.switch4_basis[1][1], weapon_left.switch4_basis[1][2],
            weapon_left.switch4_basis[2][0], weapon_left.switch4_basis[2][1], weapon_left.switch4_basis[2][2],
            weapon_left.switch6_basis[0][0], weapon_left.switch6_basis[0][1], weapon_left.switch6_basis[0][2],
            weapon_left.switch6_basis[1][0], weapon_left.switch6_basis[1][1], weapon_left.switch6_basis[1][2],
            weapon_left.switch6_basis[2][0], weapon_left.switch6_basis[2][1], weapon_left.switch6_basis[2][2],
            weapon_left.switch7_basis[0][0], weapon_left.switch7_basis[0][1], weapon_left.switch7_basis[0][2],
            weapon_left.switch7_basis[1][0], weapon_left.switch7_basis[1][1], weapon_left.switch7_basis[1][2],
            weapon_left.switch7_basis[2][0], weapon_left.switch7_basis[2][1], weapon_left.switch7_basis[2][2],
            weapon_left.depth,
            weapon_left_mtx_json,
            snd_stub_hits_total, snd_stub_hits_play, snd_stub_legacy_overlap_hits,
            snd_stub_new_player_init, snd_stub_get_playing_state, snd_stub_deactivate,
            snd_stub_deactivate_all_1, snd_stub_deactivate_all_11, snd_stub_deactivate_all_3,
            snd_stub_create_post_event, snd_stub_dispose_sound, snd_stub_set_priority,
            snd_stub_unlink_clear, snd_stub_count_alloc_list,
            mp_time_ticks, mp_player_count);
        if (len > 0 && len < (int)sizeof(linebuf)) {
            traceSanitizeJsonNonFiniteNumbers(linebuf);
            traceWriteStateLine(linebuf, (size_t)len);
        }
    }

    s_prevMenu = menu;

    memset(s_traceChrReactLatched, 0, sizeof(s_traceChrReactLatched));

    /* Direct descriptor writes are already handed to the kernel every frame. */
}

/* ===== Audio PCM Dump ===== */

typedef struct AudioDumpState {
    FILE *file;
    int frames;
    int limit;
    int inited;
} AudioDumpState;

static AudioDumpState s_audioDumpState = {0};
static AudioDumpState s_musicAudioDumpState = {0};

static int portAudioDumpLimit(const char *envname) {
    char limitenv[96];
    const char *value;
    char *end;
    long parsed;

    snprintf(limitenv, sizeof(limitenv), "%s_FRAMES", envname);
    value = getenv(limitenv);
    if (!value || !*value) return 300;

    parsed = strtol(value, &end, 10);
    if (end == value || parsed <= 0 || parsed > 100000) {
        fprintf(stderr, "[TRACE] Ignoring invalid %s=%s (using 300 frames)\n",
                limitenv, value);
        return 300;
    }

    return (int)parsed;
}

static void portAudioDumpTo(AudioDumpState *state, const char *envname,
                            const char *label, const void *buf,
                            unsigned int size) {
    /* Fast bail: check the completed/inactive state first to avoid
     * touching any globals that could be corrupted by DL overruns. */
    if (state->inited && !state->file) return;

    if (!state->inited) {
        const char *path;
        state->inited = 1;
        state->limit = portAudioDumpLimit(envname);
        path = getenv(envname);
        if (path) {
            state->file = fopen(path, "wb");
            if (state->file) {
                fprintf(stderr, "[TRACE] %s PCM dump -> %s (%d frames)\n",
                        label, path, state->limit);
            } else {
                /* AUDIT-0069: an explicitly-requested capture that can't open its
                 * file must be reported, not silently skipped. */
                fprintf(stderr, "[TRACE] %s PCM dump FAILED to open %s: %s\n",
                        label, path, strerror(errno));
            }
        }
        if (!state->file) return;
    }
    if (!state->file) return;
    if (state->frames >= state->limit) return;
    if (!buf || size == 0) return;

    /* AUDIT-0069: a short write must abort the capture and be reported, not
     * counted as a completed frame. */
    if (fwrite(buf, 1, size, state->file) != size) {
        fprintf(stderr, "[TRACE] %s PCM dump write failed after %d frame(s): %s\n",
                label, state->frames, strerror(errno));
        fclose(state->file);
        state->file = NULL;
        return;
    }
    state->frames++;
    if (state->frames == state->limit) {
        /* Only announce completion if the final flush/close actually succeeded. */
        if (fclose(state->file) != 0) {
            fprintf(stderr, "[TRACE] %s PCM dump close failed after %d frame(s): %s\n",
                    label, state->frames, strerror(errno));
        } else {
            fprintf(stderr, "[TRACE] %s dump complete (%d frames)\n",
                    label, state->limit);
        }
        state->file = NULL;
    }
}

void portAudioDump(const void *buf, unsigned int size) {
    portAudioDumpTo(&s_audioDumpState, "GE007_AUDIO_DUMP", "Audio", buf, size);
}

void portMusicAudioDump(const void *buf, unsigned int size) {
    portAudioDumpTo(&s_musicAudioDumpState, "GE007_MUSIC_AUDIO_DUMP", "Music audio", buf, size);
}

/* ===== Shutdown ===== */

void portTraceShutdown(void) {
    /* Only touch file pointers if tracing/dumping was actually requested.
     * DL buffer overruns can corrupt these statics — if tracing was never
     * enabled, these pointers should be NULL but may contain garbage. */
    if (s_traceFileOpened) traceClose();
    /* Audio dump managed by its own init flag — only close if we opened it */
    if (s_audioDumpState.inited && s_audioDumpState.file) {
        fclose(s_audioDumpState.file);
        s_audioDumpState.file = NULL;
    }
    if (s_musicAudioDumpState.inited && s_musicAudioDumpState.file) {
        fclose(s_musicAudioDumpState.file);
        s_musicAudioDumpState.file = NULL;
    }
}

static int traceDisplayCastEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        enabled = (getenv("GE007_TRACE_DISPLAYCAST") != NULL) ? 1 : 0;
    }
    return enabled;
}

static int traceGoldenEyeLogoRenderEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        enabled = (getenv("GE007_TRACE_GOLDENEYE_LOGO_RENDER") != NULL) ? 1 : 0;
    }
    return enabled;
}

void portTraceDisplayCastLoad(const char *phase,
                              s32 intro_character_index,
                              s32 body,
                              s32 head,
                              s32 load_item,
                              s32 load_result,
                              s32 load_size)
{
    if (!traceDisplayCastEnabled()) {
        return;
    }

    fprintf(stderr,
            "[DISPLAYCAST_LOAD] phase=%s idx=%d body=%d head=%d item=%d result=%d size=%d\n",
            phase ? phase : "?",
            intro_character_index,
            body,
            head,
            load_item,
            load_result,
            load_size);
    fflush(stderr);
}

void portTraceDisplayCastModel(const char *phase,
                               s32 intro_character_index,
                               s32 body,
                               s32 head,
                               s32 weapon,
                               s32 weapon_hand,
                               void *cast_model)
{
    if (!traceDisplayCastEnabled()) {
        return;
    }

    fprintf(stderr,
            "[DISPLAYCAST_MODEL] phase=%s idx=%d body=%d head=%d weapon=%d hand=%d model=%p\n",
            phase ? phase : "?",
            intro_character_index,
            body,
            head,
            weapon,
            weapon_hand,
            cast_model);
    fflush(stderr);
}

void portTraceDisplayCastState(const char *phase,
                               s32 intro_character_index,
                               s32 anim_index,
                               s32 anim_count,
                               s32 anim_id,
                               s32 camera_preset,
                               s32 full_actor_intro,
                               s32 menu_timer,
                               f32 camera_distance_current,
                               f32 camera_distance_start,
                               f32 camera_distance_end,
                               f32 camera_yaw_current,
                               f32 camera_yaw_start,
                               f32 camera_yaw_end,
                               f32 camera_height_current,
                               f32 camera_height_start,
                               f32 camera_height_end)
{
    if (!traceDisplayCastEnabled()) {
        return;
    }

    fprintf(stderr,
            "[DISPLAYCAST_STATE] phase=%s idx=%d anim_index=%d anim_count=%d "
            "anim_id=%d camera_preset=%d full=%d timer=%d "
            "dist=%.6f/%.6f/%.6f yaw=%.6f/%.6f/%.6f height=%.6f/%.6f/%.6f\n",
            phase ? phase : "?",
            intro_character_index,
            anim_index,
            anim_count,
            anim_id,
            camera_preset,
            full_actor_intro,
            menu_timer,
            camera_distance_current,
            camera_distance_start,
            camera_distance_end,
            camera_yaw_current,
            camera_yaw_start,
            camera_yaw_end,
            camera_height_current,
            camera_height_start,
            camera_height_end);
    fflush(stderr);
}

void portTraceDisplayCastFollow(const char *phase,
                                s32 intro_character_index,
                                s32 menu_timer,
                                const coord3d *root_offset,
                                const coord3d *smooth_offset,
                                const coord3d *follow_delta,
                                const coord3d *follow_velocity,
                                const coord3d *smooth_velocity,
                                const coord3d *target_velocity,
                                const coord3d *eye,
                                const coord3d *at)
{
    if (!traceDisplayCastEnabled()) {
        return;
    }

    fprintf(stderr,
            "[DISPLAYCAST_FOLLOW] phase=%s idx=%d timer=%d "
            "clock=%d global_delta=%.6f "
            "root=(%.6f,%.6f,%.6f) smooth=(%.6f,%.6f,%.6f) "
            "delta=(%.6f,%.6f,%.6f) velocity=(%.6f,%.6f,%.6f) "
            "smooth_velocity=(%.6f,%.6f,%.6f) target_velocity=(%.6f,%.6f,%.6f) "
            "eye=(%.6f,%.6f,%.6f) at=(%.6f,%.6f,%.6f)\n",
            phase ? phase : "?",
            intro_character_index,
            menu_timer,
            g_ClockTimer,
            g_GlobalTimerDelta,
            root_offset ? root_offset->f[0] : 0.0f,
            root_offset ? root_offset->f[1] : 0.0f,
            root_offset ? root_offset->f[2] : 0.0f,
            smooth_offset ? smooth_offset->f[0] : 0.0f,
            smooth_offset ? smooth_offset->f[1] : 0.0f,
            smooth_offset ? smooth_offset->f[2] : 0.0f,
            follow_delta ? follow_delta->f[0] : 0.0f,
            follow_delta ? follow_delta->f[1] : 0.0f,
            follow_delta ? follow_delta->f[2] : 0.0f,
            follow_velocity ? follow_velocity->f[0] : 0.0f,
            follow_velocity ? follow_velocity->f[1] : 0.0f,
            follow_velocity ? follow_velocity->f[2] : 0.0f,
            smooth_velocity ? smooth_velocity->f[0] : 0.0f,
            smooth_velocity ? smooth_velocity->f[1] : 0.0f,
            smooth_velocity ? smooth_velocity->f[2] : 0.0f,
            target_velocity ? target_velocity->f[0] : 0.0f,
            target_velocity ? target_velocity->f[1] : 0.0f,
            target_velocity ? target_velocity->f[2] : 0.0f,
            eye ? eye->f[0] : 0.0f,
            eye ? eye->f[1] : 0.0f,
            eye ? eye->f[2] : 0.0f,
            at ? at->f[0] : 0.0f,
            at ? at->f[1] : 0.0f,
            at ? at->f[2] : 0.0f);
    fflush(stderr);
}

void portTraceDisplayCastFollowStep(const char *phase,
                                    s32 intro_character_index,
                                    s32 menu_timer,
                                    s32 reset_flag,
                                    s32 step_ticks,
                                    const coord3d *root_velocity,
                                    const coord3d *smooth_before,
                                    const coord3d *target_velocity_before,
                                    const coord3d *smooth_after,
                                    const coord3d *transformed,
                                    const coord3d *secondary_input,
                                    const coord3d *follow_velocity_before,
                                    const coord3d *follow_velocity_after,
                                    const coord3d *follow_delta_after)
{
    if (!traceDisplayCastEnabled()) {
        return;
    }

    fprintf(stderr,
            "[DISPLAYCAST_FOLLOW_STEP] phase=%s idx=%d timer=%d "
            "clock=%d global_delta=%.6f reset=%d step_ticks=%d "
            "root_velocity=(%.6f,%.6f,%.6f) "
            "smooth_before=(%.6f,%.6f,%.6f) "
            "target_velocity_before=(%.6f,%.6f,%.6f) "
            "smooth_after=(%.6f,%.6f,%.6f) "
            "transformed=(%.6f,%.6f,%.6f) "
            "secondary_input=(%.6f,%.6f,%.6f) "
            "velocity_before=(%.6f,%.6f,%.6f) "
            "velocity_after=(%.6f,%.6f,%.6f) "
            "delta_after=(%.6f,%.6f,%.6f)\n",
            phase ? phase : "?",
            intro_character_index,
            menu_timer,
            g_ClockTimer,
            g_GlobalTimerDelta,
            reset_flag,
            step_ticks,
            root_velocity ? root_velocity->f[0] : 0.0f,
            root_velocity ? root_velocity->f[1] : 0.0f,
            root_velocity ? root_velocity->f[2] : 0.0f,
            smooth_before ? smooth_before->f[0] : 0.0f,
            smooth_before ? smooth_before->f[1] : 0.0f,
            smooth_before ? smooth_before->f[2] : 0.0f,
            target_velocity_before ? target_velocity_before->f[0] : 0.0f,
            target_velocity_before ? target_velocity_before->f[1] : 0.0f,
            target_velocity_before ? target_velocity_before->f[2] : 0.0f,
            smooth_after ? smooth_after->f[0] : 0.0f,
            smooth_after ? smooth_after->f[1] : 0.0f,
            smooth_after ? smooth_after->f[2] : 0.0f,
            transformed ? transformed->f[0] : 0.0f,
            transformed ? transformed->f[1] : 0.0f,
            transformed ? transformed->f[2] : 0.0f,
            secondary_input ? secondary_input->f[0] : 0.0f,
            secondary_input ? secondary_input->f[1] : 0.0f,
            secondary_input ? secondary_input->f[2] : 0.0f,
            follow_velocity_before ? follow_velocity_before->f[0] : 0.0f,
            follow_velocity_before ? follow_velocity_before->f[1] : 0.0f,
            follow_velocity_before ? follow_velocity_before->f[2] : 0.0f,
            follow_velocity_after ? follow_velocity_after->f[0] : 0.0f,
            follow_velocity_after ? follow_velocity_after->f[1] : 0.0f,
            follow_velocity_after ? follow_velocity_after->f[2] : 0.0f,
            follow_delta_after ? follow_delta_after->f[0] : 0.0f,
            follow_delta_after ? follow_delta_after->f[1] : 0.0f,
            follow_delta_after ? follow_delta_after->f[2] : 0.0f);
    fflush(stderr);
}

void portTraceDisplayCastMatrix(const char *phase,
                                s32 intro_character_index,
                                s32 menu_timer,
                                const Mtxf *matrix)
{
    if (!traceDisplayCastEnabled()) {
        return;
    }

    fprintf(stderr,
            "[DISPLAYCAST_MATRIX] phase=%s idx=%d timer=%d "
            "diag=(%.6f,%.6f,%.6f,%.6f) "
            "row0=(%.6f,%.6f,%.6f,%.6f) "
            "row1=(%.6f,%.6f,%.6f,%.6f) "
            "row2=(%.6f,%.6f,%.6f,%.6f) "
            "row3=(%.6f,%.6f,%.6f,%.6f)\n",
            phase ? phase : "?",
            intro_character_index,
            menu_timer,
            matrix ? matrix->m[0][0] : 0.0f,
            matrix ? matrix->m[1][1] : 0.0f,
            matrix ? matrix->m[2][2] : 0.0f,
            matrix ? matrix->m[3][3] : 0.0f,
            matrix ? matrix->m[0][0] : 0.0f,
            matrix ? matrix->m[0][1] : 0.0f,
            matrix ? matrix->m[0][2] : 0.0f,
            matrix ? matrix->m[0][3] : 0.0f,
            matrix ? matrix->m[1][0] : 0.0f,
            matrix ? matrix->m[1][1] : 0.0f,
            matrix ? matrix->m[1][2] : 0.0f,
            matrix ? matrix->m[1][3] : 0.0f,
            matrix ? matrix->m[2][0] : 0.0f,
            matrix ? matrix->m[2][1] : 0.0f,
            matrix ? matrix->m[2][2] : 0.0f,
            matrix ? matrix->m[2][3] : 0.0f,
            matrix ? matrix->m[3][0] : 0.0f,
            matrix ? matrix->m[3][1] : 0.0f,
            matrix ? matrix->m[3][2] : 0.0f,
            matrix ? matrix->m[3][3] : 0.0f);
    fflush(stderr);
}

void portTraceDisplayCastAnim(const char *phase,
                              s32 intro_character_index,
                              s32 menu_timer,
                              const Model *model)
{
    s32 anim_frames = -1;

    if (!traceDisplayCastEnabled()) {
        return;
    }

    if (model != NULL && model->anim != NULL) {
        anim_frames = model->anim->unk04;
    }

    fprintf(stderr,
            "[DISPLAYCAST_ANIM] phase=%s idx=%d timer=%d "
            "clock=%d global_delta=%.6f "
            "frame=%.6f framea=%d frameb=%d end=%.6f "
            "speed=%.6f playspeed=%.6f animrate=%.6f loop=%d "
            "loopframe=%.6f frame2=%.6f frame2a=%d frame2b=%d "
            "speed2=%.6f anim_frames=%d anim=%p anim2=%p\n",
            phase ? phase : "?",
            intro_character_index,
            menu_timer,
            g_ClockTimer,
            g_GlobalTimerDelta,
            model ? model->unk28 : 0.0f,
            model ? model->framea : 0,
            model ? model->frameb : 0,
            model ? model->endframe : 0.0f,
            model ? model->speed : 0.0f,
            model ? model->playspeed : 0.0f,
            model ? model->animrate : 0.0f,
            model ? model->animlooping : 0,
            model ? model->animloopframe : 0.0f,
            model ? model->unk58 : 0.0f,
            model ? model->frame2a : 0,
            model ? model->frame2b : 0,
            model ? model->speed2 : 0.0f,
            anim_frames,
            model ? (void *)model->anim : NULL,
            model ? (void *)model->anim2 : NULL);
    fflush(stderr);
}

void portTraceDisplayCastRootData(const char *phase,
                                  s32 intro_character_index,
                                  s32 menu_timer,
                                  const ModelRwData_HeaderRecord *root_data)
{
    if (!traceDisplayCastEnabled()) {
        return;
    }

    fprintf(stderr,
            "[DISPLAYCAST_ROOTDATA] phase=%s idx=%d timer=%d "
            "flags=(%d,%d,%d) ground=%.6f "
            "pos=(%.6f,%.6f,%.6f) unk14=%.6f unk18=%.6f unk1c=%.6f unk20=%.6f "
            "unk24=(%.6f,%.6f,%.6f) unk30=%.6f "
            "unk34=(%.6f,%.6f,%.6f) unk40=(%.6f,%.6f,%.6f) "
            "unk4c=(%.6f,%.6f,%.6f) unk58=%.6f unk5c=%.6f\n",
            phase ? phase : "?",
            intro_character_index,
            menu_timer,
            root_data ? root_data->unk00 : 0,
            root_data ? root_data->unk01 : 0,
            root_data ? root_data->unk02 : 0,
            root_data ? root_data->ground : 0.0f,
            root_data ? root_data->pos.f[0] : 0.0f,
            root_data ? root_data->pos.f[1] : 0.0f,
            root_data ? root_data->pos.f[2] : 0.0f,
            root_data ? root_data->unk14 : 0.0f,
            root_data ? root_data->unk18 : 0.0f,
            root_data ? root_data->unk1c : 0.0f,
            root_data ? root_data->unk20 : 0.0f,
            root_data ? root_data->unk24.f[0] : 0.0f,
            root_data ? root_data->unk24.f[1] : 0.0f,
            root_data ? root_data->unk24.f[2] : 0.0f,
            root_data ? root_data->unk30 : 0.0f,
            root_data ? root_data->unk34.f[0] : 0.0f,
            root_data ? root_data->unk34.f[1] : 0.0f,
            root_data ? root_data->unk34.f[2] : 0.0f,
            root_data ? root_data->unk40.f[0] : 0.0f,
            root_data ? root_data->unk40.f[1] : 0.0f,
            root_data ? root_data->unk40.f[2] : 0.0f,
            root_data ? root_data->unk4c.f[0] : 0.0f,
            root_data ? root_data->unk4c.f[1] : 0.0f,
            root_data ? root_data->unk4c.f[2] : 0.0f,
            root_data ? root_data->unk58 : 0.0f,
            root_data ? root_data->unk5c : 0.0f);
    fflush(stderr);
}

void portTraceGoldenEyeLogoRender(const char *phase,
                                  s32 menu_timer,
                                  const Mtxf *matrix,
                                  const Lights1 *light,
                                  const LookAt *lookat,
                                  const ModelRenderData *renderdata,
                                  const Model *model)
{
    const Ambient_t *ambient = light ? &light->a.l : NULL;
    const Light_t *dir_light = light ? &light->l[0].l : NULL;
    const Light_t *lookat_x = lookat ? &lookat->l[0].l : NULL;
    const Light_t *lookat_y = lookat ? &lookat->l[1].l : NULL;

    if (!traceGoldenEyeLogoRenderEnabled()) {
        return;
    }

    fprintf(stderr,
            "[GELOGO_RENDER] phase=%s frame=%d menu=%d timer=%d clock=%d "
            "global_delta=%.6f model=%p model_obj=%p model_scale=%.6f "
            "renderdata=%p flags=0x%08X zbuf=%d gdl=%p mtxlist=%p "
            "env=(%u,%u,%u,%u) fog=(%u,%u,%u,%u) cull=%u "
            "ambient=(%u,%u,%u) light0_col=(%u,%u,%u) light0_dir=(%d,%d,%d) "
            "lookatX=(%d,%d,%d) lookatY=(%d,%d,%d) "
            "diag=(%.6f,%.6f,%.6f,%.6f) "
            "row0=(%.6f,%.6f,%.6f,%.6f) row1=(%.6f,%.6f,%.6f,%.6f) "
            "row2=(%.6f,%.6f,%.6f,%.6f) row3=(%.6f,%.6f,%.6f,%.6f)\n",
            phase ? phase : "?",
            s_traceFrame,
            current_menu,
            menu_timer,
            g_ClockTimer,
            g_GlobalTimerDelta,
            model ? (void *)model : NULL,
            model ? (void *)model->obj : NULL,
            model ? model->scale : 0.0f,
            renderdata ? (void *)renderdata : NULL,
            renderdata ? renderdata->flags : 0,
            renderdata ? renderdata->zbufferenabled : 0,
            renderdata ? (void *)renderdata->gdl : NULL,
            renderdata ? (void *)renderdata->mtxlist : NULL,
            renderdata ? renderdata->envcolour.r : 0,
            renderdata ? renderdata->envcolour.g : 0,
            renderdata ? renderdata->envcolour.b : 0,
            renderdata ? renderdata->envcolour.a : 0,
            renderdata ? renderdata->fogcolour.r : 0,
            renderdata ? renderdata->fogcolour.g : 0,
            renderdata ? renderdata->fogcolour.b : 0,
            renderdata ? renderdata->fogcolour.a : 0,
            renderdata ? renderdata->cullmode : 0,
            ambient ? ambient->col[0] : 0,
            ambient ? ambient->col[1] : 0,
            ambient ? ambient->col[2] : 0,
            dir_light ? dir_light->col[0] : 0,
            dir_light ? dir_light->col[1] : 0,
            dir_light ? dir_light->col[2] : 0,
            dir_light ? dir_light->dir[0] : 0,
            dir_light ? dir_light->dir[1] : 0,
            dir_light ? dir_light->dir[2] : 0,
            lookat_x ? lookat_x->dir[0] : 0,
            lookat_x ? lookat_x->dir[1] : 0,
            lookat_x ? lookat_x->dir[2] : 0,
            lookat_y ? lookat_y->dir[0] : 0,
            lookat_y ? lookat_y->dir[1] : 0,
            lookat_y ? lookat_y->dir[2] : 0,
            matrix ? matrix->m[0][0] : 0.0f,
            matrix ? matrix->m[1][1] : 0.0f,
            matrix ? matrix->m[2][2] : 0.0f,
            matrix ? matrix->m[3][3] : 0.0f,
            matrix ? matrix->m[0][0] : 0.0f,
            matrix ? matrix->m[0][1] : 0.0f,
            matrix ? matrix->m[0][2] : 0.0f,
            matrix ? matrix->m[0][3] : 0.0f,
            matrix ? matrix->m[1][0] : 0.0f,
            matrix ? matrix->m[1][1] : 0.0f,
            matrix ? matrix->m[1][2] : 0.0f,
            matrix ? matrix->m[1][3] : 0.0f,
            matrix ? matrix->m[2][0] : 0.0f,
            matrix ? matrix->m[2][1] : 0.0f,
            matrix ? matrix->m[2][2] : 0.0f,
            matrix ? matrix->m[2][3] : 0.0f,
            matrix ? matrix->m[3][0] : 0.0f,
            matrix ? matrix->m[3][1] : 0.0f,
            matrix ? matrix->m[3][2] : 0.0f,
            matrix ? matrix->m[3][3] : 0.0f);
    fflush(stderr);
}

#endif /* NATIVE_PORT */

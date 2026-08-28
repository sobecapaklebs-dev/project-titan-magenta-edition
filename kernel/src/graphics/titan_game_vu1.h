#ifndef PROJECT_TITAN_GAME_VU1_H
#define PROJECT_TITAN_GAME_VU1_H

#include "titan_game_sdk.h"
#include "titan_video_game_texture.h"

#define TITAN_GAME_VU1_MAX_VERTICES 96u
#define TITAN_GAME_VU1_RESULT_NOT_ELIGIBLE (-2)
#define TITAN_GAME_VU1_RESULT_SUBMIT_FAILED (-3)

/* Native Titan Game geometry backend.
 *
 * EE owns GBI/state decoding and coarse validity. VU1 owns perspective divide,
 * viewport/depth mapping, STQ generation and packed PATH1 vertex emission.
 * The public Titan Game ABI stays unchanged. */
int TitanGameVu1_Initialize(void);
/* Disable the game VU1 owner. If a VIF1 submission previously faulted, this
 * succeeds only after DMAC, VIF execution/FIFO and GIF/PATH1 are proven idle;
 * only then is VIF1 reset and ownership safe to hand back to the shell waves. */
int TitanGameVu1_Reset(void);
int TitanGameVu1_IsAvailable(void);
int TitanGameVu1_HasFault(void);

/* Returns triangles submitted on success. NOT_ELIGIBLE means no PATH1 work
 * was sent and the caller may replay the complete batch through EE.
 * SUBMIT_FAILED means a VIF1 transfer may have partially executed and MUST NOT
 * be replayed in the same frame (that could double-blend partial geometry). */
int TitanGameVu1_DrawMaterialPass(
    const TitanGameMaterialVertex *vertices,
    unsigned int vertex_count,
    const TitanGameRenderState *state,
    const TitanVideoGameTexture *texture,
    unsigned int texture_arena_address,
    unsigned int zbuffer_address,
    int second_pass
);

#endif

#ifndef PROJECT_TITAN_INGAME_OVERLAY_H
#define PROJECT_TITAN_INGAME_OVERLAY_H

#include "titan_session_service.h"
#include "titan_unified_input.h"
#include <stdint.h>

#define TITAN_OVERLAY_EE_RAM_BUDGET_BYTES 8192u
#define TITAN_OVERLAY_IOP_RAM_BUDGET_BYTES 0u
#define TITAN_OVERLAY_VRAM_BUDGET_BYTES 0u

typedef enum TitanOverlayAction {
    TITAN_OVERLAY_ACTION_NONE = 0,
    TITAN_OVERLAY_ACTION_RESUME,
    TITAN_OVERLAY_ACTION_RETURN
} TitanOverlayAction;

typedef struct TitanOverlayMetrics {
    uint32_t open_count;
    uint32_t close_count;
    uint32_t rendered_frames;
    uint32_t closed_update_count;
    uint32_t open_latency_us;
    uint32_t close_latency_us;
    uint32_t resume_latency_us;
    uint32_t ee_ram_bytes;
    uint32_t iop_ram_bytes;
    uint32_t vram_bytes;
} TitanOverlayMetrics;

void TitanOverlay_Initialize(void);
void TitanOverlay_Reset(void);
int TitanOverlay_IsOpen(void);
int TitanOverlay_Open(void);
TitanOverlayAction TitanOverlay_Update(const TitanNormalizedInputState *input);
void TitanOverlay_Draw(void);
const TitanOverlayMetrics *TitanOverlay_GetMetrics(void);
unsigned int TitanOverlay_GetSelection(void);

#endif

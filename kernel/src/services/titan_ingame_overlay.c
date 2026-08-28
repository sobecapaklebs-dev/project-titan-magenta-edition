#include "titan_ingame_overlay.h"
#include "titan_video.h"
#include "titan_font.h"
#include <string.h>

typedef struct TitanOverlayState {
    uint8_t open;
    uint8_t selection;
    uint8_t suppress_until_release;
    uint8_t reserved;
    TitanOverlayMetrics metrics;
} TitanOverlayState;

static TitanOverlayState g_overlay;

void TitanOverlay_Initialize(void)
{
    memset(&g_overlay, 0, sizeof(g_overlay));
    g_overlay.metrics.ee_ram_bytes = (uint32_t)sizeof(g_overlay);
    g_overlay.metrics.iop_ram_bytes = TITAN_OVERLAY_IOP_RAM_BUDGET_BYTES;
    g_overlay.metrics.vram_bytes = TITAN_OVERLAY_VRAM_BUDGET_BYTES;
}

void TitanOverlay_Reset(void)
{
    uint32_t opens = g_overlay.metrics.open_count;
    uint32_t closes = g_overlay.metrics.close_count;
    TitanOverlay_Initialize();
    g_overlay.metrics.open_count = opens;
    g_overlay.metrics.close_count = closes;
}

int TitanOverlay_IsOpen(void) { return g_overlay.open != 0u; }
unsigned int TitanOverlay_GetSelection(void) { return g_overlay.selection; }

int TitanOverlay_Open(void)
{
    if (g_overlay.open || !TitanSession_EnterOverlay()) return 0;
    g_overlay.open = 1u;
    g_overlay.selection = 0u;
    g_overlay.suppress_until_release = 1u;
    g_overlay.metrics.open_count++;
    return 1;
}

static TitanOverlayAction TitanOverlay_Close(TitanOverlayAction action)
{
    g_overlay.open = 0u;
    g_overlay.suppress_until_release = 1u;
    g_overlay.metrics.close_count++;
    if (action == TITAN_OVERLAY_ACTION_RESUME) (void)TitanSession_Resume();
    return action;
}

TitanOverlayAction TitanOverlay_Update(const TitanNormalizedInputState *input)
{
    if (!g_overlay.open) {
        g_overlay.metrics.closed_update_count++;
        if (input != 0 &&
            (((input->pressed & TITAN_NBTN_SYSTEM) != 0u) ||
             (((input->buttons & TITAN_NBTN_SELECT) != 0u) &&
              ((input->pressed & TITAN_NBTN_START) != 0u))))
            (void)TitanOverlay_Open();
        return TITAN_OVERLAY_ACTION_NONE;
    }
    if (input == 0) return TITAN_OVERLAY_ACTION_NONE;
    if (g_overlay.suppress_until_release) {
        if ((input->buttons & (TITAN_NBTN_SELECT|TITAN_NBTN_START|TITAN_NBTN_CROSS|TITAN_NBTN_CIRCLE|TITAN_NBTN_SYSTEM)) == 0u)
            g_overlay.suppress_until_release = 0u;
        return TITAN_OVERLAY_ACTION_NONE;
    }
    if ((input->pressed & TITAN_NBTN_SYSTEM) != 0u)
        return TitanOverlay_Close(TITAN_OVERLAY_ACTION_RESUME);
    if ((input->pressed & (TITAN_NBTN_UP|TITAN_NBTN_DOWN)) != 0u)
        g_overlay.selection ^= 1u;
    if ((input->pressed & TITAN_NBTN_CIRCLE) != 0u)
        return TitanOverlay_Close(TITAN_OVERLAY_ACTION_RESUME);
    if ((input->pressed & TITAN_NBTN_CROSS) != 0u) {
        if (g_overlay.selection == 0u)
            return TitanOverlay_Close(TITAN_OVERLAY_ACTION_RESUME);
        if (TitanSession_RequestReturn())
            return TitanOverlay_Close(TITAN_OVERLAY_ACTION_RETURN);
    }
    return TITAN_OVERLAY_ACTION_NONE;
}

void TitanOverlay_Draw(void)
{
    const TitanSessionDescriptor *s;
    if (!g_overlay.open) return;
    s = TitanSession_GetDescriptor();
    g_overlay.metrics.rendered_frames++;
    TitanVideo_DrawRectAlpha(0.0f, 0.0f, 854.0f, 448.0f, 0, 0, 0, 116);
    TitanVideo_DrawRoundedRectAlpha(226.0f, 92.0f, 402.0f, 264.0f, 14.0f, 14, 15, 22, 244);
    TitanFont_DrawTextShadow(258.0f, 132.0f, 0.80f, "PROJECT TITAN", 255,255,255,255);
    if (s != 0)
        TitanFont_DrawTextAlpha(258.0f, 166.0f, 0.50f, s->title_id[0] ? s->title_id : "Current session", 205,208,218,255);
    if (g_overlay.selection == 0u)
        TitanVideo_DrawRoundedRectAlpha(250.0f, 210.0f, 354.0f, 42.0f, 7.0f, 222,43,82,210);
    TitanFont_DrawTextAlpha(272.0f, 236.0f, 0.58f, "Resume",255,255,255,255);
    if (g_overlay.selection == 1u)
        TitanVideo_DrawRoundedRectAlpha(250.0f, 266.0f, 354.0f, 42.0f, 7.0f, 222,43,82,210);
    TitanFont_DrawTextAlpha(272.0f, 292.0f, 0.58f, "Return to Titan",255,255,255,255);
}

const TitanOverlayMetrics *TitanOverlay_GetMetrics(void) { return &g_overlay.metrics; }

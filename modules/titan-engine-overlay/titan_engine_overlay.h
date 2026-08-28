#ifndef TITAN_ENGINE_OVERLAY_H
#define TITAN_ENGINE_OVERLAY_H

#ifdef TITAN_ENGINE_OVERLAY_HOST
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#else
#include <tamtypes.h>
#endif

#define TITAN_ENGINE_OVERLAY_CAP_CONTEXT2_SAFE  (1u << 0)
#define TITAN_ENGINE_OVERLAY_CAP_RETURN         (1u << 1)
#define TITAN_ENGINE_OVERLAY_CAP_RESUME         (1u << 2)

#define TITAN_ENGINE_BTN_SELECT   (1u << 0)
#define TITAN_ENGINE_BTN_START    (1u << 3)
#define TITAN_ENGINE_BTN_UP       (1u << 4)
#define TITAN_ENGINE_BTN_DOWN     (1u << 6)
#define TITAN_ENGINE_BTN_CIRCLE   (1u << 13)
#define TITAN_ENGINE_BTN_CROSS    (1u << 14)

typedef enum TitanEngineOverlayAction {
    TITAN_ENGINE_OVERLAY_NONE = 0,
    TITAN_ENGINE_OVERLAY_RESUME = 1,
    TITAN_ENGINE_OVERLAY_RETURN = 2,
    TITAN_ENGINE_OVERLAY_UNSAFE = 3
} TitanEngineOverlayAction;

typedef struct TitanEngineOverlay {
    u32 capabilities;
    u32 open;
    u32 selection;
    u32 previous_buttons;
    u32 render_count;
    char title_id[16];
} TitanEngineOverlay;

void TitanEngineOverlay_Init(TitanEngineOverlay *overlay, u32 capabilities, const char *title_id);
int TitanEngineOverlay_Open(TitanEngineOverlay *overlay);
TitanEngineOverlayAction TitanEngineOverlay_Update(TitanEngineOverlay *overlay, u32 buttons);
void TitanEngineOverlay_Close(TitanEngineOverlay *overlay);
int TitanEngineOverlay_Render(TitanEngineOverlay *overlay);

#endif

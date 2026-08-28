#include <assert.h>
#include <stdio.h>
#include "../../modules/titan-engine-overlay/titan_engine_overlay.h"
int main(void){
 TitanEngineOverlay o; TitanEngineOverlayAction a;
 TitanEngineOverlay_Init(&o,TITAN_ENGINE_OVERLAY_CAP_CONTEXT2_SAFE|TITAN_ENGINE_OVERLAY_CAP_RETURN|TITAN_ENGINE_OVERLAY_CAP_RESUME,"SLUS_000.00");
 assert(TitanEngineOverlay_Open(&o)); assert(TitanEngineOverlay_Render(&o));
 a=TitanEngineOverlay_Update(&o,TITAN_ENGINE_BTN_DOWN); assert(a==TITAN_ENGINE_OVERLAY_NONE && o.selection==1);
 TitanEngineOverlay_Update(&o,0); a=TitanEngineOverlay_Update(&o,TITAN_ENGINE_BTN_CROSS); assert(a==TITAN_ENGINE_OVERLAY_RETURN);
 TitanEngineOverlay_Init(&o,TITAN_ENGINE_OVERLAY_CAP_RETURN|TITAN_ENGINE_OVERLAY_CAP_RESUME,"X"); assert(!TitanEngineOverlay_Open(&o));
 TitanEngineOverlay_Init(&o,TITAN_ENGINE_OVERLAY_CAP_CONTEXT2_SAFE|TITAN_ENGINE_OVERLAY_CAP_RETURN|TITAN_ENGINE_OVERLAY_CAP_RESUME,"X"); assert(TitanEngineOverlay_Open(&o));
 a=TitanEngineOverlay_Update(&o,TITAN_ENGINE_BTN_CIRCLE); assert(a==TITAN_ENGINE_OVERLAY_RESUME && !o.open);
 puts("buildfix28-engine-overlay: OK"); return 0;
}

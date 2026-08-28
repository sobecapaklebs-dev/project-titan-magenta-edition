#include <assert.h>
#include <string.h>
#include "../src/services/titan_session_service.h"
#include "../src/services/titan_igr_compatibility.h"
/* Overlay renderer itself is target-coupled; this host gate validates the
   state/resource policy that controls whether rendering may ever activate. */
int main(void){TitanSessionDescriptor d; TitanOverlayLevel l; uint32_t p=0; memset(&d,0,sizeof(d)); TitanSession_Initialize(); strcpy(d.title_id,"TITAN-NES"); d.engine=TITAN_SESSION_ENGINE_NATIVE; d.runtime_capabilities=TITAN_SESSION_CAP_SAFE_RETURN|TITAN_SESSION_CAP_OVERLAY_MINIMAL|TITAN_SESSION_CAP_OVERLAY_FULL|TITAN_SESSION_CAP_GS_STATE_RESTORE|TITAN_SESSION_CAP_NATIVE_SAFEPOINT; d.igr_compatibility=TitanIgrCompatibility_Evaluate(d.title_id,d.engine,d.runtime_capabilities,0,&l,&p); d.overlay_level=l; assert(TitanSession_Begin(&d)); assert(TitanSession_Transition(TITAN_SESSION_LAUNCHING)); assert(TitanSession_Transition(TITAN_SESSION_RUNNING)); assert(TitanSession_CanOverlay()); assert(TitanSession_EnterOverlay()); assert(TitanSession_Resume()); assert(TitanSession_RequestReturn()); assert(TitanSession_Transition(TITAN_SESSION_RETURNING)); TitanSession_End(); return 0;}

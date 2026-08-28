#include <assert.h>
#include <string.h>
#include "../src/services/titan_session_service.h"
#include "../src/services/titan_igr_compatibility.h"
int main(void){
 TitanSessionDescriptor d; TitanOverlayLevel level; uint32_t profile=0; memset(&d,0,sizeof(d));
 TitanSession_Initialize(); assert(TitanSession_GetState()==TITAN_SESSION_IDLE);
 strcpy(d.session_id,"s1"); strcpy(d.title_id,"TITAN-NES"); d.engine=TITAN_SESSION_ENGINE_NATIVE; d.start_time_ms=100;
 d.runtime_capabilities=TITAN_SESSION_CAP_SAFE_RETURN|TITAN_SESSION_CAP_OVERLAY_MINIMAL|TITAN_SESSION_CAP_OVERLAY_FULL|TITAN_SESSION_CAP_GS_STATE_RESTORE|TITAN_SESSION_CAP_NATIVE_SAFEPOINT;
 d.igr_compatibility=TitanIgrCompatibility_Evaluate(d.title_id,d.engine,d.runtime_capabilities,0,&level,&profile); d.overlay_level=level; d.compatibility_profile=profile;
 assert(d.igr_compatibility==TITAN_IGR_SUPPORTED); assert(level==TITAN_OVERLAY_FULL); assert(profile==1);
 assert(TitanSession_Begin(&d)); assert(TitanSession_Transition(TITAN_SESSION_LAUNCHING)); assert(TitanSession_Transition(TITAN_SESSION_RUNNING));
 assert(TitanSession_CanOverlay()); assert(TitanSession_EnterOverlay()); assert(TitanSession_Resume()); assert(TitanSession_RequestReturn()); assert(TitanSession_Transition(TITAN_SESSION_RETURNING)); TitanSession_End();
 assert(TitanSession_GetState()==TITAN_SESSION_IDLE);
 assert(TitanIgrCompatibility_Evaluate("SLUS_000.00",TITAN_SESSION_ENGINE_NEUTRINO,d.runtime_capabilities,0,&level,&profile)==TITAN_IGR_UNKNOWN);
 assert(level==TITAN_OVERLAY_DISABLED);
 memset(&d,0,sizeof(d)); strcpy(d.session_id,"external"); strcpy(d.title_id,"SLUS_000.00");
 d.engine=TITAN_SESSION_ENGINE_NEUTRINO; d.start_time_ms=200; d.runtime_capabilities=TITAN_SESSION_CAP_SAFE_RETURN|TITAN_SESSION_CAP_INPUT_NORMALIZED;
 d.igr_compatibility=TITAN_IGR_UNKNOWN; d.overlay_level=TITAN_OVERLAY_RETURN_ONLY; TitanSession_Initialize();
 assert(TitanSession_Begin(&d)); assert(TitanSession_Transition(TITAN_SESSION_LAUNCHING)); assert(TitanSession_Transition(TITAN_SESSION_RUNNING));
 assert(!TitanSession_CanOverlay()); assert(TitanSession_CanReturn()); assert(TitanSession_RequestReturn());
 return 0;
}

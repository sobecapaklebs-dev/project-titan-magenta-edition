#include "titan_controller_ux.h"
#include "titan_unified_input.h"
#if defined(TITAN_CONTROLLER_UX_HOST_TEST)
typedef enum TitanNotificationKind { TITAN_NOTIFICATION_INFO=0, TITAN_NOTIFICATION_ACTIVITY, TITAN_NOTIFICATION_SUCCESS, TITAN_NOTIFICATION_WARNING, TITAN_NOTIFICATION_ERROR } TitanNotificationKind;
int TitanNotifications_Push(TitanNotificationKind kind,const char *title,const char *message);
#else
#include "titan_notifications.h"
#endif
#include <stdio.h>
#include <string.h>
#define UX_SLOTS TITAN_UNIFIED_INPUT_MAX_DEVICES
typedef struct PendingController { uint32_t id,generation;TitanInputDeviceType type;uint8_t player,battery,battery_valid,dirty,disconnect; } PendingController;
static PendingController g_pending[UX_SLOTS]; static TitanControllerUxMetrics g_metrics;
static PendingController *slot_for(uint32_t id){unsigned i;PendingController*freep=0;for(i=0;i<UX_SLOTS;i++){if(g_pending[i].id==id)return &g_pending[i];if(!g_pending[i].id&&!freep)freep=&g_pending[i];}if(freep){freep->id=id;return freep;}return &g_pending[id%UX_SLOTS];}
static void absorb(const TitanInputEvent*e){PendingController*p;if(!e||!e->device_id)return;p=slot_for(e->device_id);if(p->dirty&&p->generation==e->generation)g_metrics.duplicates_suppressed++;p->generation=e->generation;p->type=e->device_type;p->player=e->player;p->battery=e->battery_percent;p->battery_valid=e->battery_valid;p->dirty=1;p->disconnect=(e->type==TITAN_INPUT_EVENT_DISCONNECTED);if(p->disconnect)g_metrics.disconnects++;}
static void emit(PendingController*p){char msg[96];const char*name;if(!p||!p->dirty)return;name=TitanUnifiedInput_DeviceName(p->type);if(p->disconnect){snprintf(msg,sizeof(msg),"Controller %u disconnected",p->player<TITAN_UNIFIED_INPUT_MAX_PLAYERS?(unsigned)p->player+1u:0u);TitanNotifications_Push(TITAN_NOTIFICATION_INFO,name,msg);}else if(p->player<TITAN_UNIFIED_INPUT_MAX_PLAYERS&&p->battery_valid){snprintf(msg,sizeof(msg),"Controller %u  Battery %u%%",(unsigned)p->player+1u,(unsigned)p->battery);TitanNotifications_Push(TITAN_NOTIFICATION_INFO,name,msg);}else if(p->player<TITAN_UNIFIED_INPUT_MAX_PLAYERS){snprintf(msg,sizeof(msg),"Controller %u",(unsigned)p->player+1u);TitanNotifications_Push(TITAN_NOTIFICATION_INFO,name,msg);}else TitanNotifications_Push(TITAN_NOTIFICATION_INFO,name,"Controller connected");p->dirty=0;g_metrics.shown++;}
void TitanControllerUx_Initialize(void){memset(g_pending,0,sizeof(g_pending));memset(&g_metrics,0,sizeof(g_metrics));}
void TitanControllerUx_Update(int home_ready){TitanInputEvent e;unsigned i;while(TitanUnifiedInput_PollEvent(&e)){if(e.type==TITAN_INPUT_EVENT_CONNECTED||e.type==TITAN_INPUT_EVENT_RECONNECTED||e.type==TITAN_INPUT_EVENT_PLAYER_ASSIGNED||e.type==TITAN_INPUT_EVENT_BATTERY_CHANGED||e.type==TITAN_INPUT_EVENT_DISCONNECTED)absorb(&e);}for(i=0;i<UX_SLOTS;i++)if(g_pending[i].dirty){if(home_ready)emit(&g_pending[i]);else g_metrics.deferred++;}}
void TitanControllerUx_Shutdown(void){memset(g_pending,0,sizeof(g_pending));}
const TitanControllerUxMetrics *TitanControllerUx_GetMetrics(void){return &g_metrics;}

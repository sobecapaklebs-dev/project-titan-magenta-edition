#include "titan_vmc.h"
#include "titan_user_profile.h"
#include "titan_audio.h"
#include "titan_notifications.h"
#include "titan_shell_background.h"
#include "titan_time.h"
#include "titan_video.h"
#include "titan_font.h"
#include "titan_storage_scheduler.h"
#include "titan_game_runtime.h"
#include "titan_asset_image.h"

#include <stdint.h>
#include <string.h>

void TitanAudio_StartMusic(void) { }
void TitanAudio_StopMusic(void) { }
int TitanAudio_BeginExternalStream(void) { return 1; }
void TitanAudio_EndExternalStream(void) { }
unsigned int TitanAudio_GetExternalFreeFrames(void) { return 0u; }
unsigned int TitanAudio_SubmitExternalPcm(const short *samples, unsigned int frames)
{
    (void)samples;
    return frames;
}

int TitanNotifications_Push(
    TitanNotificationKind kind,
    const char *title,
    const char *message
)
{
    (void)kind;
    (void)title;
    (void)message;
    return 1;
}

void TitanShellBackground_Draw(
    unsigned int frame,
    float page_transition,
    float idle_intensity,
    float horizontal_motion,
    float vertical_transition,
    float system_view_transition
)
{
    (void)frame;
    (void)page_transition;
    (void)idle_intensity;
    (void)horizontal_motion;
    (void)vertical_transition;
    (void)system_view_transition;
}

uint64_t TitanTime_NowMilliseconds(void) { return 0u; }
uint64_t TitanTime_NowMicroseconds(void) { return 0u; }

float TitanFont_MeasureText(float scale, const char *text)
{
    size_t length = text != 0 ? strlen(text) : 0u;
    return (float)length * 8.0f * scale;
}

void TitanFont_DrawTextAlpha(
    float x, float y, float scale, const char *text,
    unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
)
{
    (void)x; (void)y; (void)scale; (void)text;
    (void)red; (void)green; (void)blue; (void)alpha;
}

void TitanFont_DrawTextShadow(
    float x, float y, float scale, const char *text,
    unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
)
{
    (void)x; (void)y; (void)scale; (void)text;
    (void)red; (void)green; (void)blue; (void)alpha;
}

void TitanVideo_DrawLineAlpha(
    float x0, float y0, float x1, float y1, float thickness,
    unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
)
{
    (void)x0; (void)y0; (void)x1; (void)y1; (void)thickness;
    (void)red; (void)green; (void)blue; (void)alpha;
}

void TitanVideo_DrawRectAlpha(
    float x, float y, float width, float height,
    unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
)
{
    (void)x; (void)y; (void)width; (void)height;
    (void)red; (void)green; (void)blue; (void)alpha;
}

void TitanVideo_DrawRoundedRectAlpha(
    float x, float y, float width, float height, float radius,
    unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
)
{
    (void)x; (void)y; (void)width; (void)height; (void)radius;
    (void)red; (void)green; (void)blue; (void)alpha;
}

TitanResult TitanVideo_DrawMediaImage16(
    const unsigned short *pixels,
    unsigned int width,
    unsigned int height,
    unsigned int stride,
    float x,
    float y,
    float draw_width,
    float draw_height,
    unsigned char alpha
)
{
    (void)pixels; (void)width; (void)height; (void)stride;
    (void)x; (void)y; (void)draw_width; (void)draw_height; (void)alpha;
    return TITAN_RESULT_OK;
}

void TitanStorageScheduler_SetTitanAppExclusiveMode(int enabled) { (void)enabled; }
int TitanStorageScheduler_IsTitanAppWorkerParked(void) { return 1; }

void TitanVideo_SetTransform(
    float scale,
    float origin_x,
    float origin_y,
    float offset_x,
    float offset_y
)
{
    (void)scale; (void)origin_x; (void)origin_y; (void)offset_x; (void)offset_y;
}

void TitanVideo_ResetTransform(void) { }

int TitanGameRuntime_Begin(
    const TitanGameSessionConfig *config,
    const TitanGameApiV1 **api
)
{
    (void)config;
    if (api != 0) *api = 0;
    return 0;
}
int TitanGameRuntime_SetStorageRoot(const char *root_path)
{ return root_path != 0 && root_path[0] != '\0'; }
void TitanGameRuntime_RequestExit(void) { }
int TitanGameRuntime_End(void) { return 1; }
int TitanGameRuntime_IsActive(void) { return 0; }
const TitanGameApiV1 *TitanGameRuntime_GetApi(void) { return 0; }

int TitanSystemError_Raise(const char *component, const char *message)
{
    (void)component;
    (void)message;
    return 1;
}

#include "titan_diagnostics.h"
#include "titan_text_input.h"

static int g_test_text_open;
static TitanTextInputRequest g_test_text_request;

void TitanDiagnostics_Event(TitanDiagLevel level, TitanDiagSubsystem subsystem,
    uint16_t event, int32_t result, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    (void)level; (void)subsystem; (void)event; (void)result;
    (void)a; (void)b; (void)c; (void)d;
}

void TitanTextInput_Initialize(void) { g_test_text_open = 0; }
int TitanTextInput_Open(const TitanTextInputRequest *request)
{
    if (request == 0 || g_test_text_open) return 0;
    g_test_text_request = *request;
    g_test_text_open = 1;
    return 1;
}
void TitanTextInput_Update(const TitanInputState *input) { (void)input; }
void TitanTextInput_Draw(unsigned int frame) { (void)frame; }
int TitanTextInput_IsOpen(void) { return g_test_text_open; }
TitanTextInputState TitanTextInput_GetState(void)
{ return g_test_text_open ? TITAN_TEXT_INPUT_ACTIVE : TITAN_TEXT_INPUT_CLOSED; }
const char *TitanTextInput_GetText(void) { return ""; }

#include "titan_app_sdk.h"
int TitanAppNetwork_BeginGet(const char *url, uint32_t maximum_bytes, uint32_t deadline_ms)
{ (void)url; (void)maximum_bytes; (void)deadline_ms; return 0; }
int TitanAppNetwork_GetStatus(TitanAppNetworkResult *result)
{ if (result) memset(result, 0, sizeof(*result)); return result != 0; }
int TitanAppNetwork_Read(uint32_t offset, void *buffer, uint32_t capacity)
{ (void)offset; (void)buffer; (void)capacity; return -1; }
void TitanAppNetwork_Cancel(void) { }
void TitanAppNetwork_AbandonSession(void) { }

int TitanAssetImage_DecodeMediaRgb565Fit(
    const unsigned char *source, unsigned int source_size, unsigned short *destination,
    unsigned int width, unsigned int height, unsigned int stride,
    const TitanAssetImageOptions *options)
{
    (void)source; (void)source_size; (void)destination; (void)width;
    (void)height; (void)stride; (void)options; return 0;
}

#include "titan_ftp_service.h"
static TitanFtpServiceSnapshot g_test_ftp;
int TitanFtpService_Start(const char *root, unsigned short port){(void)root;memset(&g_test_ftp,0,sizeof(g_test_ftp));g_test_ftp.state=TITAN_FTP_STATE_LISTENING;g_test_ftp.control_port=port?port:2121u;return 1;}
void TitanFtpService_Tick(void){}
void TitanFtpService_Stop(void){memset(&g_test_ftp,0,sizeof(g_test_ftp));}
void TitanFtpService_GetSnapshot(TitanFtpServiceSnapshot *s){if(s)*s=g_test_ftp;}


const TitanUserProfile *TitanUserProfiles_GetActive(void)
{
    static TitanUserProfile p = {1, 1u, 0u, "Host", "mass:/TITAN/users/user_0001/profile.vmc"};
    return &p;
}
int TitanVmc_ListRoot(const TitanUserProfile *p, unsigned int i, TitanVmcDirectoryEntry *e)
{
    (void)p; (void)i; if (e) memset(e,0,sizeof(*e)); return 0;
}

int TitanVmc_ListDirectory(const TitanUserProfile *p, const char *path, unsigned int i, TitanVmcDirectoryEntry *e)
{ (void)p; (void)path; (void)i; if (e) memset(e,0,sizeof(*e)); return 0; }

int TitanVmc_ReadFile(const TitanUserProfile *p,const char *path,unsigned int off,void *b,unsigned int c)
{ (void)p;(void)path;(void)off;if(b&&c) memset(b,0,c);return 0;}

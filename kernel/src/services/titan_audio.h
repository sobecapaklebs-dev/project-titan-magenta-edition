#ifndef PROJECT_TITAN_AUDIO_H
#define PROJECT_TITAN_AUDIO_H

#include <stddef.h>

typedef enum TitanAudioSfx
{
    TITAN_AUDIO_SFX_NOTIFICATION = 0,
    TITAN_AUDIO_SFX_PROFILE_ENTRY,
    TITAN_AUDIO_SFX_HOME_GRID_NAV,
    TITAN_AUDIO_SFX_TOP_MENU_NAV,
    TITAN_AUDIO_SFX_INTERFACE_MENU_NAV,
    TITAN_AUDIO_SFX_SHUTDOWN,
    TITAN_AUDIO_SFX_THEME_TOP_ENTER,
    TITAN_AUDIO_SFX_COUNT
} TitanAudioSfx;

/* Initialization is retryable. A failed attempt never latches the service in
 * a half-initialized state. Music is optional; valid SFX can still make the
 * service ready when magenta_melody.wav is absent. */
int TitanAudio_Initialize(void);
void TitanAudio_Shutdown(void);
void TitanAudio_Update(void);
/* Storage-thread-only pumps. On PS2 these are the only functions allowed to
 * perform music/SFX asset I/O after initialization. */
int TitanAudio_StorageWorkerNeedsRealtime(void);
int TitanAudio_StorageWorkerPumpRealtime(void);
int TitanAudio_StorageWorkerPumpDeferred(void);
void TitanAudio_SetDeferredAssetIoAllowed(int allowed);

int TitanAudio_IsReady(void);
const char *TitanAudio_GetStatusName(void);
unsigned int TitanAudio_GetLoadedSfxCount(void);

void TitanAudio_StartMusic(void);
void TitanAudio_StopMusic(void);
/* Rebinds music/SFX to the currently active Theme API v2 assets. */
void TitanAudio_ReloadThemeAssets(void);

/* Titan App media session: fixed 32 kHz, stereo, signed 16-bit PCM.
 * The caller may stream decoded media without taking ownership of AUDSRV. */
int TitanAudio_BeginExternalStream(void);
void TitanAudio_EndExternalStream(void);
unsigned int TitanAudio_SubmitExternalPcm(
    const short *interleaved_stereo,
    unsigned int frames
);
unsigned int TitanAudio_GetExternalBufferedFrames(void);
unsigned int TitanAudio_GetExternalFreeFrames(void);
int TitanAudio_IsExternalStreamActive(void);

int TitanAudio_PlaySfx(TitanAudioSfx effect);

void TitanAudio_PrepareHandoff(void);
void TitanAudio_CancelHandoff(void);
void TitanAudio_PlayShutdownAndWait(void);

#if defined(TITAN_AUDIO_TESTING)
size_t TitanAudio_TestGetPendingBytes(void);
size_t TitanAudio_TestGetMusicBufferedBytes(void);
unsigned int TitanAudio_TestGetMusicReadCalls(void);
#endif

#endif

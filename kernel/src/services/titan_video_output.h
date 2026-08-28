#ifndef PROJECT_TITAN_VIDEO_OUTPUT_H
#define PROJECT_TITAN_VIDEO_OUTPUT_H

#include "titan_result.h"

typedef enum TitanGameGsmMode
{
    TITAN_GAME_GSM_OFF = 0,
    TITAN_GAME_GSM_PROGRESSIVE,
    TITAN_GAME_GSM_PROGRESSIVE_COMPAT_1,
    TITAN_GAME_GSM_MODE_COUNT
} TitanGameGsmMode;

void TitanVideoOutput_Initialize(void);
int TitanVideoOutput_IsComponentConfigured(void);
const char *TitanVideoOutput_GetConfiguredOutputName(void);
const char *TitanVideoOutput_GetRegionName(void);
int TitanVideoOutput_IsShellProgressive(void);
int TitanVideoOutput_CanTestProgressive(void);
TitanResult TitanVideoOutput_SetShellProgressive(int enabled);
int TitanVideoOutput_LoadPersistentSetting(void);
int TitanVideoOutput_SavePersistentSetting(void);
TitanGameGsmMode TitanVideoOutput_GetGameGsmMode(void);
void TitanVideoOutput_SetGameGsmMode(TitanGameGsmMode mode);
void TitanVideoOutput_CycleGameGsmMode(int direction);
const char *TitanVideoOutput_GetGameGsmName(void);
const char *TitanVideoOutput_GetGameGsmArgument(void);

#endif

#ifndef PROJECT_TITAN_GAME_GSM_H
#define PROJECT_TITAN_GAME_GSM_H

typedef enum TitanPerGameGsmMode
{
    TITAN_PER_GAME_GSM_DEFAULT = 0,
    TITAN_PER_GAME_GSM_480P,
    TITAN_PER_GAME_GSM_720P,
    TITAN_PER_GAME_GSM_1080I,
    TITAN_PER_GAME_GSM_MODE_COUNT
} TitanPerGameGsmMode;

TitanPerGameGsmMode TitanGameGsm_GetMode(const char *startup);
int TitanGameGsm_SetMode(const char *startup, TitanPerGameGsmMode mode);
TitanPerGameGsmMode TitanGameGsm_CycleMode(
    const char *startup,
    int direction
);
const char *TitanGameGsm_GetModeName(TitanPerGameGsmMode mode);
const char *TitanGameGsm_GetNeutrinoArgument(TitanPerGameGsmMode mode);
int TitanGameGsm_IsValidStartup(const char *startup);
int TitanGameGsm_PrepareUlGoldenConfig(
    const char *startup,
    TitanPerGameGsmMode mode
);
void TitanGameGsm_InvalidateCache(void);

#endif

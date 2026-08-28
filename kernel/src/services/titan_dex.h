#ifndef PROJECT_TITAN_DEX_H
#define PROJECT_TITAN_DEX_H

void TitanDex_Initialize(void);
int TitanDex_IsEnabled(void);
int TitanDex_SetEnabled(int enabled);
const char *TitanDex_GetStateName(void);
int TitanDex_IsOverlayEnabled(void);
int TitanDex_SetOverlayEnabled(int enabled);
const char *TitanDex_GetOverlayStateName(void);

void TitanDex_BeginFrame(void);
void TitanDex_MarkEeComplete(void);
void TitanDex_EndFrame(void);

unsigned int TitanDex_GetEeLoadPercent(void);
unsigned int TitanDex_GetGsLoadPercent(void);
unsigned int TitanDex_GetRamUsedKilobytes(void);
const char *TitanDex_GetIpAddress(void);
void TitanDex_SetIpAddress(const char *address);

#endif

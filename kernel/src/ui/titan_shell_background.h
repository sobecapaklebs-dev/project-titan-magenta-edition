#ifndef PROJECT_TITAN_SHELL_BACKGROUND_H
#define PROJECT_TITAN_SHELL_BACKGROUND_H

#include "titan_theme_v4_runtime.h"

void TitanShellBackground_Initialize(void);
void TitanShellBackground_Shutdown(void);
void TitanShellBackground_RefreshThemeAssets(void);
/* BF30: dynamic scene assets are authorized by the shell lifecycle and loaded
 * only from the serialized storage worker, never from boot/render paths. */
void TitanShellBackground_RequestDynamicActivation(void);
int TitanShellBackground_HasStorageWork(void);
int TitanShellBackground_StorageWorkerPump(void);
int TitanShellBackground_IsDynamicReady(void);
int TitanShellBackground_IsVuActive(void);
void TitanShellBackground_GetThemeV4Telemetry(TitanThemeV4Telemetry *telemetry);

void TitanShellBackground_Draw(
    unsigned int frame,
    float page_transition,
    float idle_intensity,
    float horizontal_motion,
    float vertical_transition,
    float system_view_transition
);

#endif

#ifndef PROJECT_TITAN_SHELL_SCENE_H
#define PROJECT_TITAN_SHELL_SCENE_H

#include "titan_context.h"
#include "titan_input.h"

void TitanShellScene_Initialize(void);
void TitanShellScene_ApplyDexLayout(void);
int TitanShellScene_IsStartupActive(void);
void TitanShellScene_BeginGameReturn(unsigned int game_index);

void TitanShellScene_Update(
    const TitanInputState *input
);

void TitanShellScene_Render(
    const TitanContext *context,
    const TitanInputState *input
);

#endif

#ifndef PROJECT_TITAN_SYSTEM_ERROR_H
#define PROJECT_TITAN_SYSTEM_ERROR_H

#include "titan_input.h"

void TitanSystemError_Initialize(void);
int TitanSystemError_Raise(const char *component, const char *message);
int TitanSystemError_Update(const TitanInputState *input);
void TitanSystemError_Draw(unsigned int frame);
int TitanSystemError_IsOpen(void);
unsigned int TitanSystemError_GetDroppedCount(void);

#endif

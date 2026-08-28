#ifndef PROJECT_TITAN_PLATFORM_H
#define PROJECT_TITAN_PLATFORM_H

#include "titan_result.h"

TitanResult TitanPlatform_Initialize(void);

void TitanPlatform_ShowFatalError(
    const char *stage,
    TitanResult result
);

#endif

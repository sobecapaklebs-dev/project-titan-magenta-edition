#ifndef PROJECT_TITAN_APP_NETWORK_H
#define PROJECT_TITAN_APP_NETWORK_H

#include <stdint.h>
#include "../../include/titan_app_sdk.h"

void TitanAppNetwork_Initialize(void);
int TitanAppNetwork_BeginGet(const char *url, uint32_t maximum_bytes, uint32_t deadline_ms);
int TitanAppNetwork_GetStatus(TitanAppNetworkResult *result);
int TitanAppNetwork_Read(uint32_t offset, void *buffer, uint32_t capacity);
void TitanAppNetwork_Cancel(void);
void TitanAppNetwork_AbandonSession(void);

#endif

#ifndef PROJECT_TITAN_WIRELESS_ACCESSORIES_H
#define PROJECT_TITAN_WIRELESS_ACCESSORIES_H

#include <titan_pademu_telemetry.h>

void TitanWirelessAccessories_Initialize(void);
void TitanWirelessAccessories_BeginStartupReconnect(void);
void TitanWirelessAccessories_EndStartupReconnect(void);
int TitanWirelessAccessories_IsStartupReconnectActive(void);
void TitanWirelessAccessories_SetVisible(int visible);
void TitanWirelessAccessories_Update(void);
int TitanWirelessAccessories_BeginScan(void);
int TitanWirelessAccessories_Connect(unsigned int index);
int TitanWirelessAccessories_Disconnect(unsigned int index);
int TitanWirelessAccessories_Forget(unsigned int index);
const TitanPademuWirelessState *TitanWirelessAccessories_GetState(void);
unsigned int TitanWirelessAccessories_GetSeenCount(void);
unsigned int TitanWirelessAccessories_GetRegisteredCount(void);
const TitanPademuWirelessAccessory *TitanWirelessAccessories_GetSeen(unsigned int visible_index, unsigned int *source_index);
const TitanPademuWirelessAccessory *TitanWirelessAccessories_GetRegistered(unsigned int visible_index, unsigned int *source_index);

#endif

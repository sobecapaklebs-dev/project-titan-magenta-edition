#ifndef PROJECT_TITAN_CONTROLLER_ASSIGNMENT_H
#define PROJECT_TITAN_CONTROLLER_ASSIGNMENT_H

#include <stdint.h>
#include "titan_unified_input.h"

#define TITAN_CONTROLLER_ASSIGNMENT_VERSION 1u
#define TITAN_CONTROLLER_ASSIGNMENT_PLAYER_COUNT TITAN_UNIFIED_INPUT_MAX_PLAYERS

typedef struct TitanControllerAssignmentBinding
{
    uint32_t stable_key;
    TitanInputDeviceType type;
    TitanInputTransport transport;
    uint8_t valid;
} TitanControllerAssignmentBinding;

void TitanControllerAssignment_Initialize(void);
void TitanControllerAssignment_Update(void);

int TitanControllerAssignment_AssignDevice(unsigned int player, uint32_t device_id);
int TitanControllerAssignment_SetAutomatic(unsigned int player);
int TitanControllerAssignment_CyclePlayer(unsigned int player);
int TitanControllerAssignment_IsAutomatic(unsigned int player);
int TitanControllerAssignment_GetBinding(unsigned int player, TitanControllerAssignmentBinding *binding);
const TitanInputDeviceDescriptor *TitanControllerAssignment_GetPreferredConnected(unsigned int player);
const TitanInputDeviceDescriptor *TitanControllerAssignment_GetWelcomeCandidate(void);

int TitanControllerAssignment_SetMultitapEnabled(int enabled);
int TitanControllerAssignment_GetMultitapEnabled(void);
int TitanControllerAssignment_SetMultitapPort(unsigned int port);
unsigned int TitanControllerAssignment_GetMultitapPort(void);

/* -1 = physical/native input; 0 = PADEMU USB; 1 = PADEMU Bluetooth. */
int TitanControllerAssignment_GetGameplayPademuMode(void);

/* Persistent writes are owned exclusively by TitanStorageScheduler. */
int TitanControllerAssignment_HasStorageWork(void);
int TitanControllerAssignment_StorageWorkerPump(void);

#endif

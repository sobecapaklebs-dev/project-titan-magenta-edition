#ifndef TITAN_MODERN_GAMEPAD_PROTOCOL_H
#define TITAN_MODERN_GAMEPAD_PROTOCOL_H

#include "titan_unified_input.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TitanGamepadProtocolResult {
    TitanNormalizedInputState state;
    uint8_t battery_percent;
    uint8_t battery_valid;
} TitanGamepadProtocolResult;

int TitanGamepad_ParseDualSenseUsb(const uint8_t *data, size_t size, TitanGamepadProtocolResult *out);
int TitanGamepad_ParseDualSenseBluetooth(const uint8_t *data, size_t size, TitanGamepadProtocolResult *out);
int TitanGamepad_ParseXboxGipInput(const uint8_t *payload, size_t size, TitanGamepadProtocolResult *out);
int TitanGamepad_ParseXboxGipPacket(const uint8_t *packet, size_t size, TitanGamepadProtocolResult *out);
int TitanGamepad_ParseXboxBluetoothHid(const uint8_t *report, size_t size, TitanGamepadProtocolResult *out);

#ifdef __cplusplus
}
#endif
#endif

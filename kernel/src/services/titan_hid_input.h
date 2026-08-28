#ifndef PROJECT_TITAN_HID_INPUT_H
#define PROJECT_TITAN_HID_INPUT_H
#include <stdint.h>
typedef struct TitanHidInputStatus { int initialized; int keyboard_ready; int mouse_ready; int keyboard_module_id; int mouse_module_id; uint32_t keyboard_device_id; uint32_t mouse_device_id; uint32_t errors; } TitanHidInputStatus;
void TitanHidInput_Initialize(void);
void TitanHidInput_Update(void);
/* FileXio is globally serialized by PS2SDK. Keyboard reads therefore run only
 * on Titan's storage-owner worker and publish raw events into a RAM ring. */
int TitanHidInput_StorageWorkerPump(void);
void TitanHidInput_Shutdown(void);
const TitanHidInputStatus *TitanHidInput_GetStatus(void);
#endif

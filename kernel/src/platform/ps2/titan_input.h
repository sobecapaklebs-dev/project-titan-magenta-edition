#ifndef PROJECT_TITAN_INPUT_H
#define PROJECT_TITAN_INPUT_H

#include "titan_result.h"

#include <tamtypes.h>

typedef enum TitanInputButton
{
    TITAN_INPUT_BUTTON_SELECT   = 0x0001,
    TITAN_INPUT_BUTTON_L3       = 0x0002,
    TITAN_INPUT_BUTTON_R3       = 0x0004,
    TITAN_INPUT_BUTTON_START    = 0x0008,

    TITAN_INPUT_BUTTON_UP       = 0x0010,
    TITAN_INPUT_BUTTON_RIGHT    = 0x0020,
    TITAN_INPUT_BUTTON_DOWN     = 0x0040,
    TITAN_INPUT_BUTTON_LEFT     = 0x0080,

    TITAN_INPUT_BUTTON_L2       = 0x0100,
    TITAN_INPUT_BUTTON_R2       = 0x0200,
    TITAN_INPUT_BUTTON_L1       = 0x0400,
    TITAN_INPUT_BUTTON_R1       = 0x0800,

    TITAN_INPUT_BUTTON_TRIANGLE = 0x1000,
    TITAN_INPUT_BUTTON_CIRCLE   = 0x2000,
    TITAN_INPUT_BUTTON_CROSS    = 0x4000,
    TITAN_INPUT_BUTTON_SQUARE   = 0x8000
} TitanInputButton;

typedef enum TitanInputBootstrapStage
{
    TITAN_INPUT_BOOTSTRAP_IDLE = 0,
    TITAN_INPUT_BOOTSTRAP_RESETTING_IOP,
    TITAN_INPUT_BOOTSTRAP_SYNCING_IOP,
    TITAN_INPUT_BOOTSTRAP_INITIALIZING_LOADFILE,
    TITAN_INPUT_BOOTSTRAP_LOADING_SIO2MAN,
    TITAN_INPUT_BOOTSTRAP_LOADING_PADMAN,
    TITAN_INPUT_BOOTSTRAP_BINDING_PAD_RPC,
    TITAN_INPUT_BOOTSTRAP_OPENING_PORT,
    TITAN_INPUT_BOOTSTRAP_READY,
    TITAN_INPUT_BOOTSTRAP_FAILED
} TitanInputBootstrapStage;

typedef void (*TitanInputProgressCallback)(
    void *user_data
);

#define TITAN_INPUT_MAX_PADS 4u
#define TITAN_INPUT_PHYSICAL_PORTS 2u

typedef struct TitanInputPadState
{
    int connected;
    int pad_state;
    int port;
    int slot;
    int last_read_result;
    int main_mode_requested;
    u16 buttons;
    u16 pressed;
    u16 released;
    unsigned char left_stick_x;
    unsigned char left_stick_y;
    unsigned char right_stick_x;
    unsigned char right_stick_y;
} TitanInputPadState;

typedef struct TitanInputState
{
    int initialized;
    int iop_ready;
    int connected;

    int pad_state;
    int active_port;
    int last_read_result;

    TitanResult initialization_result;

    TitanInputBootstrapStage bootstrap_stage;
    int bootstrap_detail;

    int sio2man_module_id;
    int padman_module_id;
    int sio2man_uses_extended_module;

    int main_mode_requested;

    u16 buttons;
    u16 pressed;
    u16 released;

    unsigned char left_stick_x;
    unsigned char left_stick_y;
    unsigned char right_stick_x;
    unsigned char right_stick_y;
} TitanInputState;

TitanResult TitanInput_Initialize(
    TitanInputProgressCallback progress_callback,
    void *progress_user_data
);

/* Installer-only fast path: bind to PADMAN already resident in the current
 * IOP image without resetting the IOP. This preserves a USB mass stack that
 * was mounted by the launcher (for example wLaunchELF). */
TitanResult TitanInput_InitializePreserveIop(
    TitanInputProgressCallback progress_callback,
    void *progress_user_data
);

void TitanInput_Update(void);

void TitanInput_Shutdown(void);

const TitanInputState *TitanInput_GetState(void);
/* Game Runtime view: four stable logical pad slots. Slots 0/1 map to the
 * two native controller ports; slots 2/3 are reserved for future multitap and
 * report disconnected until a multitap backend is installed. */
const TitanInputPadState *TitanInput_GetPadState(unsigned int index);
unsigned int TitanInput_GetConnectedPadCount(void);
unsigned int TitanInput_GetPadCapacity(void);
int TitanInput_SetRumble(
    unsigned int index,
    unsigned char small_motor,
    unsigned char large_motor
);
void TitanInput_StopAllRumble(void);

int TitanInput_IsIopReady(void);

int TitanInput_UsesExtendedModules(void);

const char *TitanInput_GetBootstrapStageName(
    TitanInputBootstrapStage stage
);

int TitanInput_IsDown(
    TitanInputButton button
);

int TitanInput_WasPressed(
    TitanInputButton button
);

int TitanInput_WasReleased(
    TitanInputButton button
);

int TitanInput_GetPort(void);
int TitanInput_SetPort(int port);

#endif

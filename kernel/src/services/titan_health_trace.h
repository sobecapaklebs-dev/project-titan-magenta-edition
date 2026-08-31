#ifndef PROJECT_TITAN_HEALTH_TRACE_H
#define PROJECT_TITAN_HEALTH_TRACE_H

#include <stdint.h>

/* RC5.2 Health & Safety freeze flight recorder.
 * Numeric ids are stable so a frozen target can be decoded off-console. */
typedef enum TitanHealthTraceCode {
    TITAN_HT_FRAME_BEGIN                 = 0x0100,
    TITAN_HT_EARLY_INPUT_ENTER           = 0x0110,
    TITAN_HT_EARLY_INPUT_EXIT            = 0x0111,
    TITAN_HT_INPUT_ENTER                 = 0x0120,
    TITAN_HT_INPUT_EXIT                  = 0x0121,
    TITAN_HT_AUDIO_ENTER                 = 0x0130,
    TITAN_HT_AUDIO_EXIT                  = 0x0131,
    TITAN_HT_SERVICES_ENTER              = 0x0140,
    TITAN_HT_DOWNLOAD_ENTER              = 0x0141,
    TITAN_HT_DOWNLOAD_EXIT               = 0x0142,
    TITAN_HT_STORE_ENTER                 = 0x0143,
    TITAN_HT_STORE_EXIT                  = 0x0144,
    TITAN_HT_DISC_ENTER                  = 0x0145,
    TITAN_HT_DISC_EXIT                   = 0x0146,
    TITAN_HT_DIAG_ENTER                  = 0x0147,
    TITAN_HT_DIAG_EXIT                   = 0x0148,
    TITAN_HT_STORAGE_ENTER               = 0x0149,
    TITAN_HT_STORAGE_EXIT                = 0x014a,
    TITAN_HT_ASSET_ENTER                 = 0x014b,
    TITAN_HT_ASSET_EXIT                  = 0x014c,
    TITAN_HT_NETWORK_ENTER               = 0x014d,
    TITAN_HT_NETWORK_EXIT                = 0x014e,
    TITAN_HT_NET_UI_ENTER                = 0x014f,
    TITAN_HT_NET_UI_EXIT                 = 0x0150,
    TITAN_HT_BROWSER_ENTER               = 0x0151,
    TITAN_HT_BROWSER_EXIT                = 0x0152,
    TITAN_HT_SERVICES_EXIT               = 0x0153,
    TITAN_HT_UI_ENTER                    = 0x0160,
    TITAN_HT_SHELL_UPDATE_ENTER          = 0x0161,
    TITAN_HT_SHELL_UPDATE_EXIT           = 0x0162,
    TITAN_HT_UI_EXIT                     = 0x0163,
    TITAN_HT_RENDER_ENTER                = 0x0170,
    TITAN_HT_VIDEO_BEGIN_ENTER           = 0x0171,
    TITAN_HT_VIDEO_BEGIN_EXIT            = 0x0172,
    TITAN_HT_VIDEO_CLEAR                 = 0x0173,
    TITAN_HT_SHELL_RENDER_ENTER          = 0x0174,
    TITAN_HT_SHELL_RENDER_EXIT           = 0x0175,
    TITAN_HT_OVERLAYS_ENTER              = 0x0176,
    TITAN_HT_OVERLAYS_EXIT               = 0x0177,
    TITAN_HT_VIDEO_END_ENTER             = 0x0178,
    TITAN_HT_VIDEO_END_EXIT              = 0x0179,
    TITAN_HT_RENDER_EXIT                 = 0x017a,
    TITAN_HT_FRAME_END                   = 0x0180,

    TITAN_HT_STARTUP_HEALTH_TRANSITION   = 0x1000,
    TITAN_HT_STARTUP_AUDIO_BEFORE        = 0x1001,
    TITAN_HT_STARTUP_AUDIO_AFTER         = 0x1002,
    TITAN_HT_STARTUP_WIRELESS_BEFORE     = 0x1003,
    TITAN_HT_STARTUP_WIRELESS_AFTER      = 0x1004,
    TITAN_HT_STARTUP_HEALTH_FRAME        = 0x1010,
    TITAN_HT_STARTUP_WELCOME_TRANSITION  = 0x1011,

    TITAN_HT_WIRELESS_BEGIN              = 0x1100,
    TITAN_HT_WIRELESS_BT_REQUEST_BEFORE  = 0x1101,
    TITAN_HT_WIRELESS_BT_REQUEST_AFTER   = 0x1102,
    TITAN_HT_WIRELESS_BOOTSTRAP_BEFORE   = 0x1103,
    TITAN_HT_WIRELESS_BOOTSTRAP_AFTER    = 0x1104,
    TITAN_HT_WIRELESS_BOND_BEFORE        = 0x1105,
    TITAN_HT_WIRELESS_BOND_AFTER         = 0x1106,
    TITAN_HT_WIRELESS_END                = 0x1107,

    TITAN_HT_BT_REQUEST_ENTER            = 0x1200,
    TITAN_HT_BT_REQUEST_EARLY_WAIT       = 0x1201,
    TITAN_HT_BT_WORKER_CREATE_BEFORE     = 0x1202,
    TITAN_HT_BT_WORKER_CREATE_AFTER      = 0x1203,
    TITAN_HT_BT_WORKER_ENTER             = 0x1210,
    TITAN_HT_BT_SMSUTILS_EXEC_BEFORE     = 0x1211,
    TITAN_HT_BT_SMSUTILS_EXEC_AFTER      = 0x1212,
    TITAN_HT_BT_PADEMU_EXEC_BEFORE       = 0x1213,
    TITAN_HT_BT_PADEMU_EXEC_AFTER        = 0x1214,
    TITAN_HT_BT_WORKER_EXIT              = 0x1215,

    TITAN_HT_BT_BIND_BEFORE              = 0x1300,
    TITAN_HT_BT_BIND_AFTER               = 0x1301,
    TITAN_HT_WIRELESS_RPC_BEFORE         = 0x1310,
    TITAN_HT_WIRELESS_RPC_AFTER          = 0x1311,
    TITAN_HT_BOND_RPC_BEFORE             = 0x1320,
    TITAN_HT_BOND_RPC_AFTER              = 0x1321,

    TITAN_HT_NETWORK_BOOT_WORKER_CREATE_BEFORE = 0x1400,
    TITAN_HT_NETWORK_BOOT_WORKER_CREATE_AFTER  = 0x1401,
    TITAN_HT_NETWORK_BOOT_WORKER_ENTER         = 0x1402,
    TITAN_HT_NETWORK_MODULE_SEARCH_BEFORE      = 0x1410,
    TITAN_HT_NETWORK_MODULE_SEARCH_AFTER       = 0x1411,
    TITAN_HT_NETWORK_MODULE_EXEC_BEFORE        = 0x1420,
    TITAN_HT_NETWORK_MODULE_EXEC_AFTER         = 0x1421,
    TITAN_HT_NETWORK_NETMAN_INIT_BEFORE        = 0x1428,
    TITAN_HT_NETWORK_NETMAN_INIT_AFTER         = 0x1429,
    TITAN_HT_NETWORK_LINK_MODE_BEFORE          = 0x142a,
    TITAN_HT_NETWORK_LINK_MODE_AFTER           = 0x142b,
    TITAN_HT_NETWORK_PS2IP_INIT_BEFORE         = 0x1430,
    TITAN_HT_NETWORK_PS2IP_INIT_AFTER          = 0x1431,
    TITAN_HT_NETWORK_SELECT_IF_BEFORE          = 0x1440,
    TITAN_HT_NETWORK_SELECT_IF_AFTER           = 0x1441,
    TITAN_HT_NETWORK_BOOT_WORKER_EXIT          = 0x1450
} TitanHealthTraceCode;

void TitanHealthTrace_Initialize(void);
void TitanHealthTrace_SetEnabled(int enabled);
int TitanHealthTrace_IsEnabled(void);
void TitanHealthTrace_Mark(uint16_t code, uint32_t frame, uint32_t a, uint32_t b, uint32_t c);
void TitanHealthTrace_Critical(uint16_t code, uint32_t frame, uint32_t a, uint32_t b, uint32_t c);
void TitanHealthTrace_RequestFlush(void);
int TitanHealthTrace_HasStorageWork(void);
int TitanHealthTrace_StorageWorkerPump(void);

#endif

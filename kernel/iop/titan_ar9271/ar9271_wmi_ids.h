#ifndef TITAN_AR9271_WMI_IDS_H
#define TITAN_AR9271_WMI_IDS_H

/*
 * AR9271 ath9k_htc firmware WMI command IDs shared by the scanner and radio
 * layers.  Keep these in one header: V10.10.0 had the register command IDs
 * private to ar9271_radio.c while ar9271_scanner.c also used them, which only
 * surfaced on the real PS2SDK cross-build.
 */
#define WMI_GET_FW_VERSION_CMDID       0x0003
#define WMI_DISABLE_INTR_CMDID         0x0004
#define WMI_ENABLE_INTR_CMDID          0x0005
#define WMI_ATH_INIT_CMDID             0x0006
#define WMI_DRAIN_TXQ_ALL_CMDID        0x000b
#define WMI_START_RECV_CMDID           0x000c
#define WMI_STOP_RECV_CMDID            0x000d
#define WMI_FLUSH_RECV_CMDID           0x000e
#define WMI_SET_MODE_CMDID             0x000f
#define WMI_NODE_CREATE_CMDID          0x0010
#define WMI_NODE_REMOVE_CMDID          0x0011
#define WMI_VAP_REMOVE_CMDID           0x0012
#define WMI_VAP_CREATE_CMDID           0x0013
#define WMI_REG_READ_CMDID             0x0014
#define WMI_REG_WRITE_CMDID            0x0015
#define WMI_RC_RATE_UPDATE_CMDID       0x0017
#define WMI_TARGET_IC_UPDATE           0x0018
#define WMI_RX_STATS_CMDID             0x001e
#define WMI_REG_RMW_CMDID              0x0020

#endif

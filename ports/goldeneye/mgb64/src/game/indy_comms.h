#ifndef _INDY_COMMS_H_
#define _INDY_COMMS_H_
#include <ultra64.h>

s32 indycommInit(void);
void indycommHostinit(void);
void indycommHostLoadFile(const char *filename, u8 *targetloc);
void indycommHostSendDump(const char *filename, u8 *data, u32 size);
void indycommHostRamRomLoad(const char *filename, u8 *target, s32 size);
void indycommHostSaveFile(const char *filename, s32 size, u8 * data);
u8 * indycommHostCheckFileExists(const char *name, s32 *size);
u8 *indycommHostSendCmd(const char *cmdstr);
#endif

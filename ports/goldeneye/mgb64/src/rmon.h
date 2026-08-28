#ifndef _RMON_H_
#define _RMON_H_

/*
 * Remote-monitor compatibility entry points used by the game's debug and host
 * I/O paths. Native builds provide a small project-owned shim in rmon.c.
 */

void rmonMain(void);
s32 rmonGetToken(void);
s32 rmonStatus(void);
void osWriteHost(void * arg0, u32 arg1);
void osReadHost(void * arg0, u32 arg1);
void osSyncPrintf(const char *fmt, ...);

#endif

#ifdef NATIVE_PORT
/* The native port uses src/platform/platform_os.h for this surface. */
#else
#ifndef _OS_INTERNAL_H_
#define _OS_INTERNAL_H_

/*
 * Clean-room declarations for the internal libultra OS helpers referenced by
 * matching-target compatibility sources. This header intentionally exposes only
 * types and function prototypes.
 */

#ifdef _LANGUAGE_C_PLUS_PLUS
extern "C" {
#endif

#include <PR/os.h>

#if defined(_LANGUAGE_C) || defined(_LANGUAGE_C_PLUS_PLUS)

u32 __osGetCause(void);
void __osSetCause(u32 value);
u32 __osGetCompare(void);
void __osSetCompare(u32 value);
u32 __osGetConfig(void);
void __osSetConfig(u32 value);
void __osSetCount(u32 value);
u32 __osGetSR(void);
void __osSetSR(u32 value);
u32 __osDisableInt(void);
void __osRestoreInt(u32 mask);

u32 __osSetFpcCsr(u32 value);
u32 __osGetFpcCsr(void);

void __osSetHWIntrRoutine(OSHWIntr intr, s32 (*handler)(void));
void __osSetGlobalIntMask(OSHWIntr intr);
void __osResetGlobalIntMask(OSHWIntr intr);
s32 __osLeoInterrupt(void);

u32 __osGetTLBASID(void);
u32 __osGetTLBPageMask(s32 index);
u32 __osGetTLBHi(s32 index);
u32 __osGetTLBLo0(s32 index);
u32 __osGetTLBLo1(s32 index);

u32 __osSiGetStatus(void);
s32 __osSiRawWriteIo(u32 devAddr, u32 data);
s32 __osSiRawReadIo(u32 devAddr, u32 *data);
s32 __osSiRawStartDma(s32 direction, void *dramAddr);

u32 __osSpGetStatus(void);
void __osSpSetStatus(u32 status);
s32 __osSpSetPc(u32 pc);
s32 __osSpRawWriteIo(u32 devAddr, u32 data);
s32 __osSpRawReadIo(u32 devAddr, u32 *data);
s32 __osSpRawStartDma(s32 direction, u32 devAddr, void *dramAddr, u32 size);

void __osError(s16 code, s16 argCount, ...);
OSThread *__osGetCurrFaultedThread(void);
OSThread *__osGetNextFaultedThread(OSThread *thread);

void __osGIOInit(s32 flag);
void __osGIOInterrupt(s32 flag);
void __osGIORawInterrupt(s32 flag);

OSThread *__osGetActiveQueue(void);

void __osSyncPutChars(int channel, int length, const char *buffer);
int __osSyncGetChars(char *buffer);
void __osAsyncPutChars(int channel, int length, const char *buffer);
int __osAsyncGetChars(char *buffer);
int __osAtomicInc(unsigned int *value);
int __osAtomicDec(unsigned int *value);

u32 __osRdbSend(u8 *buffer, u32 size, u32 type);

#endif

#ifdef _LANGUAGE_C_PLUS_PLUS
}
#endif

#endif
#endif

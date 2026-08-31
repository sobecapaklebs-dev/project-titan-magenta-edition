#ifndef PROJECT_TITAN_GOLDENEYE_RUNTIME_H
#define PROJECT_TITAN_GOLDENEYE_RUNTIME_H

#include "titan_ge_platform.h"
#include "titan_ge_rom.h"

int TitanGeRuntime_Attach(const TitanGameApiV1 *api);
int TitanGeRuntime_SetAppRoot(const char *app_root);
int TitanGeRuntime_MakePath(const char *relative_path,char *output,
                            unsigned int capacity);
int TitanGeRuntime_OpenRom(const char *path);
void TitanGeRuntime_Fail(void);
int TitanGeRuntime_HasFailed(void);
const char *TitanGeRuntime_GetFailureStage(void);
void TitanGeRuntime_Detach(void);
TitanGePlatform *TitanGeRuntime_GetPlatform(void);
const TitanGameApiV1 *TitanGeRuntime_GetApi(void);
int TitanGeRuntime_RomRead(void *context, unsigned int offset,
                           void *destination, unsigned int bytes);
int TitanGeRuntime_AudioBegin(void);
unsigned int TitanGeRuntime_AudioSubmit(const short *samples,
                                        unsigned int frames);
unsigned int TitanGeRuntime_AudioFreeFrames(void);
void TitanGeRuntime_AudioEnd(void);

/* Bounded RAM recorder.  Flush only at lifecycle/fatal boundaries: storage on
 * the PS2 (especially USB 1.1) must never be opened from the render hot path. */
void TitanGeRuntime_TraceReset(void);
void TitanGeRuntime_Trace(const char *stage);
void TitanGeRuntime_TraceHeap(const char *stage, unsigned int request_bytes);
void TitanGeRuntime_TraceFlush(void);

#endif

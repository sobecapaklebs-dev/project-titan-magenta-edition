#ifndef _BOSS_H_
#define _BOSS_H_
#include <ultra64.h>
#include <bondgame.h>

struct memallocstring
{
  s32 id;
  void *string;
};

LEVELID bossGetStageNum(void);
void bossSetLoadedStage(LEVELID stage);
void bossInit(void);
void bossEntry(void);
void bossEnableShowMemUseFlag(void);
void bossMemBarsFlagToggle(void);
void bossRunTitleStage(void);
void bossReturnTitleStage(void);

#ifdef NATIVE_PORT
#include <stddef.h>
/* Native game memory arena accessors — used by the sim-state-hash invariance
 * gate (src/platform/sim_state_hash.c) to hash the primary mutable-state region. */
const void *bossGetPcPoolBase(void);
size_t      bossGetPcPoolSize(void);
#endif

#endif

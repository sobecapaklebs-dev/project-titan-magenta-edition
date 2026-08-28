#ifndef PROJECT_TITAN_GAME_RUNTIME_H
#define PROJECT_TITAN_GAME_RUNTIME_H

#include "../../include/titan_game_sdk.h"

int TitanGameRuntime_Begin(
    const TitanGameSessionConfig *config,
    const TitanGameApiV1 **api
);
/* Internal shell hand-off.  The public game API only ever sees paths rooted
 * below this directory; writes are additionally restricted to data/. */
int TitanGameRuntime_SetStorageRoot(const char *root_path);
void TitanGameRuntime_RequestExit(void);
int TitanGameRuntime_End(void);
int TitanGameRuntime_IsActive(void);
const TitanGameApiV1 *TitanGameRuntime_GetApi(void);

#endif

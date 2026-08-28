#ifndef PROJECT_TITAN_LIBRARY_SCAN_H
#define PROJECT_TITAN_LIBRARY_SCAN_H

typedef enum TitanLibraryScanPhase
{
    TITAN_LIBRARY_SCAN_IDLE = 0,
    TITAN_LIBRARY_SCAN_GAMES,
    TITAN_LIBRARY_SCAN_GAME_METADATA,
    TITAN_LIBRARY_SCAN_APPLICATIONS,
    TITAN_LIBRARY_SCAN_FINALIZING,
    TITAN_LIBRARY_SCAN_COMPLETE,
    TITAN_LIBRARY_SCAN_ERROR
} TitanLibraryScanPhase;

typedef struct TitanLibraryScanStatus
{
    TitanLibraryScanPhase phase;
    unsigned int active;
    unsigned int progress_percent;
    unsigned int processed;
    unsigned int total;
    unsigned int games_found;
    unsigned int applications_found;
    unsigned int cache_loaded;
    unsigned int metadata_reused;
    unsigned int scan_steps;
    char message[112];
} TitanLibraryScanStatus;

void TitanLibraryScan_Initialize(void);
void TitanLibraryScan_SetDeferredUi(int enabled);
int TitanLibraryScan_CommitPendingCatalog(void);
int TitanLibraryScan_HasPendingCatalog(void);
void TitanLibraryScan_Begin(int force_rebuild);
void TitanLibraryScan_Update(unsigned int entry_budget);
int TitanLibraryScan_IsActive(void);
const TitanLibraryScanStatus *TitanLibraryScan_GetStatus(void);

#endif

#ifndef PROJECT_TITAN_USER_PROFILE_H
#define PROJECT_TITAN_USER_PROFILE_H

#include "titan_avatar_ids.h"

#define TITAN_USER_PROFILE_CAPACITY 4
#define TITAN_USER_PROFILE_NAME_CAPACITY 64
#define TITAN_USER_PROFILE_VMC_PATH_CAPACITY 96

typedef enum TitanUserProfileNameResult
{
    TITAN_USER_PROFILE_NAME_OK = 0,
    TITAN_USER_PROFILE_NAME_EMPTY,
    TITAN_USER_PROFILE_NAME_TOO_LONG,
    TITAN_USER_PROFILE_NAME_INVALID,
    TITAN_USER_PROFILE_NAME_DUPLICATE,
    TITAN_USER_PROFILE_NAME_CAPACITY_REACHED
} TitanUserProfileNameResult;

typedef struct TitanUserProfile
{
    int occupied;
    unsigned int identifier;
    unsigned int avatar_id;

    char name[
        TITAN_USER_PROFILE_NAME_CAPACITY
    ];

    char vmc_path[
        TITAN_USER_PROFILE_VMC_PATH_CAPACITY
    ];
} TitanUserProfile;

typedef struct TitanUserProfileState
{
    unsigned int profile_count;
    unsigned int active_profile_index;
    unsigned int next_profile_identifier;

    TitanUserProfile profiles[
        TITAN_USER_PROFILE_CAPACITY
    ];
} TitanUserProfileState;

void TitanUserProfiles_Initialize(void);

unsigned int TitanUserProfiles_GetCount(void);
unsigned int TitanUserProfiles_GetActiveIndex(void);
unsigned int TitanUserProfiles_GetRevision(void);

const TitanUserProfile *TitanUserProfiles_Get(
    unsigned int index
);

const TitanUserProfile *TitanUserProfiles_GetActive(void);

int TitanUserProfiles_Add(void);
int TitanUserProfiles_AddNamed(
    const char *name
);

int TitanUserProfiles_AddNamedWithAvatar(
    const char *name,
    unsigned int avatar_id
);

TitanUserProfileNameResult TitanUserProfiles_CheckName(
    const char *name
);

TitanUserProfileNameResult TitanUserProfiles_CheckRenameActive(
    const char *name
);
int TitanUserProfiles_RenameActive(const char *name);

int TitanUserProfiles_RemoveActive(void);

int TitanUserProfiles_Select(
    unsigned int index
);

int TitanUserProfiles_SelectPrevious(void);
int TitanUserProfiles_SelectNext(void);

int TitanUserProfiles_ExportState(
    TitanUserProfileState *state
);

int TitanUserProfiles_ImportState(
    const TitanUserProfileState *state
);

int TitanUserProfiles_ValidateState(
    const TitanUserProfileState *state
);

int TitanUserProfiles_BuildVmcPath(
    unsigned int identifier,
    char *destination,
    unsigned int capacity
);

#endif

#ifndef PROJECT_TITAN_PROFILE_SNAPSHOT_H
#define PROJECT_TITAN_PROFILE_SNAPSHOT_H

#include "titan_user_profile.h"

#define TITAN_PROFILE_SNAPSHOT_SIZE 768u
#define TITAN_PROFILE_SNAPSHOT_FORMAT_VERSION 1u

typedef enum TitanProfileSnapshotResult
{
    TITAN_PROFILE_SNAPSHOT_OK = 0,
    TITAN_PROFILE_SNAPSHOT_INVALID,
    TITAN_PROFILE_SNAPSHOT_FUTURE_VERSION
} TitanProfileSnapshotResult;

TitanProfileSnapshotResult TitanProfileSnapshot_Encode(
    const TitanUserProfileState *state,
    unsigned int generation,
    unsigned char *buffer,
    unsigned int buffer_size
);

TitanProfileSnapshotResult TitanProfileSnapshot_Decode(
    const unsigned char *buffer,
    unsigned int buffer_size,
    TitanUserProfileState *state,
    unsigned int *generation
);

int TitanProfileSnapshot_IsGenerationNewer(
    unsigned int candidate,
    unsigned int reference
);

int TitanProfileSnapshot_ChooseNewest(
    TitanProfileSnapshotResult result_a,
    unsigned int generation_a,
    TitanProfileSnapshotResult result_b,
    unsigned int generation_b
);

int TitanProfileSnapshot_ChooseWriteTarget(
    TitanProfileSnapshotResult result_a,
    unsigned int generation_a,
    TitanProfileSnapshotResult result_b,
    unsigned int generation_b
);

#endif

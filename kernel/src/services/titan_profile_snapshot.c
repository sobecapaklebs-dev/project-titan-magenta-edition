#include "titan_profile_snapshot.h"

#include <stdint.h>
#include <string.h>

#define TITAN_PROFILE_SNAPSHOT_MAGIC_SIZE 8u
#define TITAN_PROFILE_SNAPSHOT_HEADER_SIZE 32u
#define TITAN_PROFILE_SNAPSHOT_PROFILE_OFFSET 32u
#define TITAN_PROFILE_SNAPSHOT_PROFILE_STRIDE 176u
#define TITAN_PROFILE_SNAPSHOT_CRC_OFFSET 16u
#define TITAN_PROFILE_SNAPSHOT_NAME_OFFSET 12u
#define TITAN_PROFILE_SNAPSHOT_VMC_PATH_OFFSET 76u

static const unsigned char g_titan_profile_snapshot_magic[
    TITAN_PROFILE_SNAPSHOT_MAGIC_SIZE
] = {
    'T', 'I', 'T', 'A', 'N', 'D', 'A', 'T'
};

static void TitanProfileSnapshot_Write16(
    unsigned char *buffer,
    unsigned int offset,
    unsigned int value
)
{
    buffer[offset + 0u] = (unsigned char)(value & 0xFFu);
    buffer[offset + 1u] = (unsigned char)((value >> 8) & 0xFFu);
}

static void TitanProfileSnapshot_Write32(
    unsigned char *buffer,
    unsigned int offset,
    unsigned int value
)
{
    buffer[offset + 0u] = (unsigned char)(value & 0xFFu);
    buffer[offset + 1u] = (unsigned char)((value >> 8) & 0xFFu);
    buffer[offset + 2u] = (unsigned char)((value >> 16) & 0xFFu);
    buffer[offset + 3u] = (unsigned char)((value >> 24) & 0xFFu);
}

static unsigned int TitanProfileSnapshot_Read16(
    const unsigned char *buffer,
    unsigned int offset
)
{
    return
        (unsigned int)buffer[offset + 0u] |
        ((unsigned int)buffer[offset + 1u] << 8);
}

static unsigned int TitanProfileSnapshot_Read32(
    const unsigned char *buffer,
    unsigned int offset
)
{
    return
        (unsigned int)buffer[offset + 0u] |
        ((unsigned int)buffer[offset + 1u] << 8) |
        ((unsigned int)buffer[offset + 2u] << 16) |
        ((unsigned int)buffer[offset + 3u] << 24);
}

static unsigned int TitanProfileSnapshot_CalculateCrc32(
    const unsigned char *buffer
)
{
    unsigned int crc;
    unsigned int index;

    crc = 0xFFFFFFFFu;

    for (index = 0u; index < TITAN_PROFILE_SNAPSHOT_SIZE; index++)
    {
        unsigned int value;
        unsigned int bit;

        value =
            index >= TITAN_PROFILE_SNAPSHOT_CRC_OFFSET &&
            index < TITAN_PROFILE_SNAPSHOT_CRC_OFFSET + 4u
                ? 0u
                : (unsigned int)buffer[index];

        crc ^= value;

        for (bit = 0u; bit < 8u; bit++)
        {
            unsigned int mask;

            mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }

    return ~crc;
}

TitanProfileSnapshotResult TitanProfileSnapshot_Encode(
    const TitanUserProfileState *state,
    unsigned int generation,
    unsigned char *buffer,
    unsigned int buffer_size
)
{
    unsigned int index;
    unsigned int crc;

    if (
        state == 0 ||
        buffer == 0 ||
        buffer_size != TITAN_PROFILE_SNAPSHOT_SIZE ||
        state->profile_count == 0u ||
        state->profile_count > TITAN_USER_PROFILE_CAPACITY ||
        state->active_profile_index >= state->profile_count ||
        !TitanUserProfiles_ValidateState(state)
    )
    {
        return TITAN_PROFILE_SNAPSHOT_INVALID;
    }

    memset(buffer, 0, buffer_size);
    memcpy(
        buffer,
        g_titan_profile_snapshot_magic,
        sizeof(g_titan_profile_snapshot_magic)
    );

    TitanProfileSnapshot_Write16(
        buffer,
        8u,
        TITAN_PROFILE_SNAPSHOT_FORMAT_VERSION
    );
    TitanProfileSnapshot_Write16(
        buffer,
        10u,
        TITAN_PROFILE_SNAPSHOT_HEADER_SIZE
    );
    TitanProfileSnapshot_Write32(buffer, 12u, generation);
    buffer[20u] = (unsigned char)state->profile_count;
    buffer[21u] = (unsigned char)state->active_profile_index;
    TitanProfileSnapshot_Write32(
        buffer,
        24u,
        state->next_profile_identifier
    );

    for (index = 0u; index < state->profile_count; index++)
    {
        const TitanUserProfile *profile;
        unsigned int offset;
        unsigned int name_length;
        unsigned int vmc_path_length;

        profile = &state->profiles[index];
        offset =
            TITAN_PROFILE_SNAPSHOT_PROFILE_OFFSET +
            (index * TITAN_PROFILE_SNAPSHOT_PROFILE_STRIDE);
        name_length = (unsigned int)strlen(profile->name);
        vmc_path_length = (unsigned int)strlen(profile->vmc_path);

        if (
            !profile->occupied ||
            profile->identifier == 0u ||
            profile->avatar_id >= TITAN_AVATAR_COUNT ||
            name_length == 0u ||
            name_length >= TITAN_USER_PROFILE_NAME_CAPACITY ||
            vmc_path_length == 0u ||
            vmc_path_length >= TITAN_USER_PROFILE_VMC_PATH_CAPACITY
        )
        {
            memset(buffer, 0, buffer_size);
            return TITAN_PROFILE_SNAPSHOT_INVALID;
        }

        buffer[offset + 0u] = 1u;
        buffer[offset + 1u] = (unsigned char)name_length;
        buffer[offset + 2u] = (unsigned char)vmc_path_length;
        TitanProfileSnapshot_Write16(
            buffer,
            offset + 4u,
            profile->avatar_id
        );
        TitanProfileSnapshot_Write32(
            buffer,
            offset + 8u,
            profile->identifier
        );
        memcpy(
            &buffer[offset + TITAN_PROFILE_SNAPSHOT_NAME_OFFSET],
            profile->name,
            name_length
        );
        memcpy(
            &buffer[offset + TITAN_PROFILE_SNAPSHOT_VMC_PATH_OFFSET],
            profile->vmc_path,
            vmc_path_length
        );
    }

    crc = TitanProfileSnapshot_CalculateCrc32(buffer);
    TitanProfileSnapshot_Write32(
        buffer,
        TITAN_PROFILE_SNAPSHOT_CRC_OFFSET,
        crc
    );

    return TITAN_PROFILE_SNAPSHOT_OK;
}

TitanProfileSnapshotResult TitanProfileSnapshot_Decode(
    const unsigned char *buffer,
    unsigned int buffer_size,
    TitanUserProfileState *state,
    unsigned int *generation
)
{
    TitanUserProfileState decoded;
    unsigned int stored_crc;
    unsigned int actual_crc;
    unsigned int version;
    unsigned int index;

    if (
        buffer == 0 ||
        state == 0 ||
        generation == 0 ||
        buffer_size != TITAN_PROFILE_SNAPSHOT_SIZE ||
        memcmp(
            buffer,
            g_titan_profile_snapshot_magic,
            sizeof(g_titan_profile_snapshot_magic)
        ) != 0 ||
        TitanProfileSnapshot_Read16(buffer, 10u) !=
            TITAN_PROFILE_SNAPSHOT_HEADER_SIZE
    )
    {
        return TITAN_PROFILE_SNAPSHOT_INVALID;
    }

    stored_crc = TitanProfileSnapshot_Read32(
        buffer,
        TITAN_PROFILE_SNAPSHOT_CRC_OFFSET
    );
    actual_crc = TitanProfileSnapshot_CalculateCrc32(buffer);

    if (stored_crc != actual_crc)
    {
        return TITAN_PROFILE_SNAPSHOT_INVALID;
    }

    version = TitanProfileSnapshot_Read16(buffer, 8u);

    if (version > TITAN_PROFILE_SNAPSHOT_FORMAT_VERSION)
    {
        return TITAN_PROFILE_SNAPSHOT_FUTURE_VERSION;
    }

    if (version != TITAN_PROFILE_SNAPSHOT_FORMAT_VERSION)
    {
        return TITAN_PROFILE_SNAPSHOT_INVALID;
    }

    memset(&decoded, 0, sizeof(decoded));
    decoded.profile_count = (unsigned int)buffer[20u];
    decoded.active_profile_index = (unsigned int)buffer[21u];
    decoded.next_profile_identifier = TitanProfileSnapshot_Read32(
        buffer,
        24u
    );

    if (
        decoded.profile_count == 0u ||
        decoded.profile_count > TITAN_USER_PROFILE_CAPACITY ||
        decoded.active_profile_index >= decoded.profile_count ||
        decoded.next_profile_identifier == 0u
    )
    {
        return TITAN_PROFILE_SNAPSHOT_INVALID;
    }

    for (index = 0u; index < decoded.profile_count; index++)
    {
        TitanUserProfile *profile;
        unsigned int offset;
        unsigned int name_length;
        unsigned int vmc_path_length;
        unsigned int name_index;
        unsigned int vmc_path_index;

        profile = &decoded.profiles[index];
        offset =
            TITAN_PROFILE_SNAPSHOT_PROFILE_OFFSET +
            (index * TITAN_PROFILE_SNAPSHOT_PROFILE_STRIDE);
        name_length = (unsigned int)buffer[offset + 1u];
        vmc_path_length = (unsigned int)buffer[offset + 2u];

        if (
            buffer[offset + 0u] != 1u ||
            name_length == 0u ||
            name_length >= TITAN_USER_PROFILE_NAME_CAPACITY ||
            vmc_path_length == 0u ||
            vmc_path_length >= TITAN_USER_PROFILE_VMC_PATH_CAPACITY
        )
        {
            return TITAN_PROFILE_SNAPSHOT_INVALID;
        }

        for (name_index = 0u; name_index < name_length; name_index++)
        {
            if (
                buffer[
                    offset +
                    TITAN_PROFILE_SNAPSHOT_NAME_OFFSET +
                    name_index
                ] == 0u
            )
            {
                return TITAN_PROFILE_SNAPSHOT_INVALID;
            }
        }

        for (
            vmc_path_index = 0u;
            vmc_path_index < vmc_path_length;
            vmc_path_index++
        )
        {
            if (
                buffer[
                    offset +
                    TITAN_PROFILE_SNAPSHOT_VMC_PATH_OFFSET +
                    vmc_path_index
                ] == 0u
            )
            {
                return TITAN_PROFILE_SNAPSHOT_INVALID;
            }
        }

        profile->occupied = 1;
        profile->avatar_id = TitanProfileSnapshot_Read16(
            buffer,
            offset + 4u
        );
        profile->identifier = TitanProfileSnapshot_Read32(
            buffer,
            offset + 8u
        );
        memcpy(
            profile->name,
            &buffer[offset + TITAN_PROFILE_SNAPSHOT_NAME_OFFSET],
            name_length
        );
        profile->name[name_length] = '\0';
        memcpy(
            profile->vmc_path,
            &buffer[offset + TITAN_PROFILE_SNAPSHOT_VMC_PATH_OFFSET],
            vmc_path_length
        );
        profile->vmc_path[vmc_path_length] = '\0';
    }

    *state = decoded;
    *generation = TitanProfileSnapshot_Read32(buffer, 12u);

    return TITAN_PROFILE_SNAPSHOT_OK;
}

int TitanProfileSnapshot_IsGenerationNewer(
    unsigned int candidate,
    unsigned int reference
)
{
    return (int32_t)(candidate - reference) > 0;
}

int TitanProfileSnapshot_ChooseNewest(
    TitanProfileSnapshotResult result_a,
    unsigned int generation_a,
    TitanProfileSnapshotResult result_b,
    unsigned int generation_b
)
{
    if (result_a == TITAN_PROFILE_SNAPSHOT_OK)
    {
        if (
            result_b != TITAN_PROFILE_SNAPSHOT_OK ||
            !TitanProfileSnapshot_IsGenerationNewer(
                generation_b,
                generation_a
            )
        )
        {
            return 0;
        }
    }

    if (result_b == TITAN_PROFILE_SNAPSHOT_OK)
    {
        return 1;
    }

    return -1;
}

int TitanProfileSnapshot_ChooseWriteTarget(
    TitanProfileSnapshotResult result_a,
    unsigned int generation_a,
    TitanProfileSnapshotResult result_b,
    unsigned int generation_b
)
{
    if (result_a != TITAN_PROFILE_SNAPSHOT_OK)
    {
        return 0;
    }

    if (result_b != TITAN_PROFILE_SNAPSHOT_OK)
    {
        return 1;
    }

    return TitanProfileSnapshot_IsGenerationNewer(
        generation_a,
        generation_b
    )
        ? 1
        : 0;
}

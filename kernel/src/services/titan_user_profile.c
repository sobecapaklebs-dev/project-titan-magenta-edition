#include "titan_user_profile.h"

#include "titan_storage_paths.h"

#include <string.h>

static TitanUserProfile g_profiles[
    TITAN_USER_PROFILE_CAPACITY
];

static unsigned int g_profile_count;
static unsigned int g_active_profile_index;
static unsigned int g_next_profile_identifier;
static unsigned int g_profile_revision;
static int g_profiles_initialized;

static int TitanUserProfiles_IsContinuationByte(
    unsigned char value
)
{
    return (value & 0xC0u) == 0x80u;
}

static int TitanUserProfiles_HasTerminator(
    const char *text,
    unsigned int capacity
)
{
    unsigned int index;

    if (text == 0 || capacity == 0u)
    {
        return 0;
    }

    for (index = 0u; index < capacity; index++)
    {
        if (text[index] == '\0')
        {
            return 1;
        }
    }

    return 0;
}

static int TitanUserProfiles_IsValidUtf8(
    const char *text,
    unsigned int capacity
)
{
    const unsigned char *cursor;
    const unsigned char *limit;

    if (text == 0 || capacity == 0u)
    {
        return 0;
    }

    cursor = (const unsigned char *)text;
    limit = cursor + capacity;

    while (cursor < limit && *cursor != 0u)
    {
        unsigned int codepoint;

        if (*cursor < 0x80u)
        {
            cursor++;
            continue;
        }

        if (
            *cursor >= 0xC2u &&
            *cursor <= 0xDFu &&
            cursor + 1 < limit &&
            TitanUserProfiles_IsContinuationByte(cursor[1])
        )
        {
            cursor += 2;
            continue;
        }

        if (
            *cursor >= 0xE0u &&
            *cursor <= 0xEFu &&
            cursor + 2 < limit &&
            TitanUserProfiles_IsContinuationByte(cursor[1]) &&
            TitanUserProfiles_IsContinuationByte(cursor[2])
        )
        {
            codepoint =
                ((unsigned int)(cursor[0] & 0x0Fu) << 12) |
                ((unsigned int)(cursor[1] & 0x3Fu) << 6) |
                (unsigned int)(cursor[2] & 0x3Fu);

            if (
                codepoint < 0x800u ||
                (codepoint >= 0xD800u && codepoint <= 0xDFFFu)
            )
            {
                return 0;
            }

            cursor += 3;
            continue;
        }

        if (
            *cursor >= 0xF0u &&
            *cursor <= 0xF4u &&
            cursor + 3 < limit &&
            TitanUserProfiles_IsContinuationByte(cursor[1]) &&
            TitanUserProfiles_IsContinuationByte(cursor[2]) &&
            TitanUserProfiles_IsContinuationByte(cursor[3])
        )
        {
            codepoint =
                ((unsigned int)(cursor[0] & 0x07u) << 18) |
                ((unsigned int)(cursor[1] & 0x3Fu) << 12) |
                ((unsigned int)(cursor[2] & 0x3Fu) << 6) |
                (unsigned int)(cursor[3] & 0x3Fu);

            if (
                codepoint < 0x10000u ||
                codepoint > 0x10FFFFu
            )
            {
                return 0;
            }

            cursor += 4;
            continue;
        }

        return 0;
    }

    return cursor < limit && *cursor == 0u;
}

static int TitanUserProfiles_NormalizeName(
    char *destination,
    unsigned int capacity,
    const char *source
)
{
    const char *start;
    const char *end;
    const char *cursor;
    unsigned int length;

    if (
        destination == 0 ||
        capacity == 0u ||
        source == 0
    )
    {
        return 0;
    }

    destination[0] = '\0';
    start = source;

    while (*start == ' ' || *start == '\t')
    {
        start++;
    }

    end = start;
    cursor = start;

    while (*cursor != '\0')
    {
        unsigned char value;

        value = (unsigned char)*cursor;

        if (value < 0x20u && value != (unsigned char)'\t')
        {
            return 0;
        }

        cursor++;

        if (value != (unsigned char)' ' && value != (unsigned char)'\t')
        {
            end = cursor;
        }
    }

    length = (unsigned int)(end - start);

    if (length == 0u || length >= capacity)
    {
        return 0;
    }

    memcpy(destination, start, length);
    destination[length] = '\0';

    return 1;
}

static void TitanUserProfiles_CopyString(
    char *destination,
    unsigned int capacity,
    const char *source
)
{
    unsigned int index;

    if (destination == 0 || capacity == 0u)
    {
        return;
    }

    destination[0] = '\0';

    if (source == 0)
    {
        return;
    }

    index = 0u;

    while (
        index + 1u < capacity &&
        source[index] != '\0'
    )
    {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

static unsigned int TitanUserProfiles_AppendString(
    char *destination,
    unsigned int capacity,
    unsigned int offset,
    const char *source
)
{
    unsigned int source_index;

    if (
        destination == 0 ||
        source == 0 ||
        capacity == 0u ||
        offset >= capacity
    )
    {
        return offset;
    }

    source_index = 0u;

    while (
        offset + 1u < capacity &&
        source[source_index] != '\0'
    )
    {
        destination[offset] = source[source_index];
        offset++;
        source_index++;
    }

    destination[offset] = '\0';

    return offset;
}

static unsigned int TitanUserProfiles_AppendUnsigned(
    char *destination,
    unsigned int capacity,
    unsigned int offset,
    unsigned int value,
    unsigned int minimum_digits
)
{
    char reversed[16];
    unsigned int count;
    unsigned int index;

    count = 0u;

    do
    {
        reversed[count] =
            (char)('0' + (value % 10u));

        value /= 10u;
        count++;
    }
    while (
        value > 0u &&
        count < sizeof(reversed)
    );

    while (
        count < minimum_digits &&
        count < sizeof(reversed)
    )
    {
        reversed[count] = '0';
        count++;
    }

    index = count;

    while (
        index > 0u &&
        offset + 1u < capacity
    )
    {
        index--;
        destination[offset] = reversed[index];
        offset++;
    }

    if (offset < capacity)
    {
        destination[offset] = '\0';
    }

    return offset;
}

static void TitanUserProfiles_BuildProfile(
    TitanUserProfile *profile,
    unsigned int identifier,
    const char *name,
    unsigned int avatar_id
)
{
    unsigned int offset;

    if (profile == 0)
    {
        return;
    }

    profile->occupied = 1;
    profile->identifier = identifier;
    profile->avatar_id =
        avatar_id < TITAN_AVATAR_COUNT
            ? avatar_id
            : 0u;

    if (name != 0 && name[0] != '\0')
    {
        TitanUserProfiles_CopyString(
            profile->name,
            sizeof(profile->name),
            name
        );
    }
    else
    {
        TitanUserProfiles_CopyString(
            profile->name,
            sizeof(profile->name),
            "Player "
        );

        offset = 7u;

        TitanUserProfiles_AppendUnsigned(
            profile->name,
            sizeof(profile->name),
            offset,
            identifier,
            1u
        );
    }

    TitanUserProfiles_BuildVmcPath(
        identifier,
        profile->vmc_path,
        sizeof(profile->vmc_path)
    );
}

int TitanUserProfiles_BuildVmcPath(
    unsigned int identifier,
    char *destination,
    unsigned int capacity
)
{
    unsigned int offset;

    if (
        identifier == 0u ||
        destination == 0 ||
        capacity == 0u
    )
    {
        return 0;
    }

    TitanUserProfiles_CopyString(
        destination,
        capacity,
        TITAN_PROFILE_USERS_PATH "/user_"
    );
    offset = (unsigned int)strlen(destination);
    offset = TitanUserProfiles_AppendUnsigned(
        destination,
        capacity,
        offset,
        identifier,
        4u
    );
    offset = TitanUserProfiles_AppendString(
        destination,
        capacity,
        offset,
        "/profile.vmc"
    );

    return
        offset + 1u < capacity &&
        destination[offset] == '\0';
}

void TitanUserProfiles_Initialize(void)
{
    unsigned int index;

    if (g_profiles_initialized)
    {
        return;
    }

    for (
        index = 0u;
        index < TITAN_USER_PROFILE_CAPACITY;
        index++
    )
    {
        g_profiles[index].occupied = 0;
        g_profiles[index].identifier = 0u;
        g_profiles[index].avatar_id = 0u;
        g_profiles[index].name[0] = '\0';
        g_profiles[index].vmc_path[0] = '\0';
    }

    g_profile_count = 1u;
    g_active_profile_index = 0u;
    g_next_profile_identifier = 2u;
    g_profile_revision = 1u;

    TitanUserProfiles_BuildProfile(
        &g_profiles[0],
        1u,
        0,
        0u
    );

    g_profiles_initialized = 1;
}

unsigned int TitanUserProfiles_GetCount(void)
{
    TitanUserProfiles_Initialize();

    return g_profile_count;
}

unsigned int TitanUserProfiles_GetActiveIndex(void)
{
    TitanUserProfiles_Initialize();

    return g_active_profile_index;
}

unsigned int TitanUserProfiles_GetRevision(void)
{
    TitanUserProfiles_Initialize();

    return g_profile_revision;
}

const TitanUserProfile *TitanUserProfiles_Get(
    unsigned int index
)
{
    TitanUserProfiles_Initialize();

    if (
        index >= g_profile_count ||
        !g_profiles[index].occupied
    )
    {
        return 0;
    }

    return &g_profiles[index];
}

const TitanUserProfile *TitanUserProfiles_GetActive(void)
{
    return TitanUserProfiles_Get(
        TitanUserProfiles_GetActiveIndex()
    );
}

int TitanUserProfiles_Add(void)
{
    TitanUserProfiles_Initialize();

    if (g_profile_count >= TITAN_USER_PROFILE_CAPACITY)
    {
        return 0;
    }

    TitanUserProfiles_BuildProfile(
        &g_profiles[g_profile_count],
        g_next_profile_identifier,
        0,
        (g_next_profile_identifier - 1u) % TITAN_AVATAR_COUNT
    );

    g_active_profile_index = g_profile_count;
    g_profile_count++;
    g_next_profile_identifier++;
    g_profile_revision++;

    return 1;
}

TitanUserProfileNameResult TitanUserProfiles_CheckName(
    const char *name
)
{
    char normalized[TITAN_USER_PROFILE_NAME_CAPACITY];
    unsigned int index;
    unsigned int length;

    TitanUserProfiles_Initialize();

    if (g_profile_count >= TITAN_USER_PROFILE_CAPACITY)
    {
        return TITAN_USER_PROFILE_NAME_CAPACITY_REACHED;
    }

    if (name == 0)
    {
        return TITAN_USER_PROFILE_NAME_EMPTY;
    }

    length = (unsigned int)strlen(name);

    if (length >= sizeof(normalized))
    {
        return TITAN_USER_PROFILE_NAME_TOO_LONG;
    }

    if (!TitanUserProfiles_NormalizeName(normalized, sizeof(normalized), name))
    {
        const char *cursor;

        cursor = name;

        while (*cursor == ' ' || *cursor == '\t')
        {
            cursor++;
        }

        return *cursor == '\0'
            ? TITAN_USER_PROFILE_NAME_EMPTY
            : TITAN_USER_PROFILE_NAME_INVALID;
    }

    for (index = 0u; index < g_profile_count; index++)
    {
        if (
            g_profiles[index].occupied &&
            strcmp(g_profiles[index].name, normalized) == 0
        )
        {
            return TITAN_USER_PROFILE_NAME_DUPLICATE;
        }
    }

    return TITAN_USER_PROFILE_NAME_OK;
}

TitanUserProfileNameResult TitanUserProfiles_CheckRenameActive(
    const char *name
)
{
    char normalized[TITAN_USER_PROFILE_NAME_CAPACITY];
    unsigned int index;
    unsigned int length;

    TitanUserProfiles_Initialize();

    if (name == 0)
    {
        return TITAN_USER_PROFILE_NAME_EMPTY;
    }

    length = (unsigned int)strlen(name);
    if (length >= sizeof(normalized))
    {
        return TITAN_USER_PROFILE_NAME_TOO_LONG;
    }

    if (!TitanUserProfiles_NormalizeName(normalized, sizeof(normalized), name))
    {
        const char *cursor;

        cursor = name;
        while (*cursor == ' ' || *cursor == '\t')
        {
            cursor++;
        }
        return *cursor == '\0'
            ? TITAN_USER_PROFILE_NAME_EMPTY
            : TITAN_USER_PROFILE_NAME_INVALID;
    }

    for (index = 0u; index < g_profile_count; index++)
    {
        if (
            index != g_active_profile_index &&
            g_profiles[index].occupied &&
            strcmp(g_profiles[index].name, normalized) == 0
        )
        {
            return TITAN_USER_PROFILE_NAME_DUPLICATE;
        }
    }

    return TITAN_USER_PROFILE_NAME_OK;
}

int TitanUserProfiles_RenameActive(const char *name)
{
    char normalized[TITAN_USER_PROFILE_NAME_CAPACITY];

    TitanUserProfiles_Initialize();
    if (
        TitanUserProfiles_CheckRenameActive(name) != TITAN_USER_PROFILE_NAME_OK ||
        !TitanUserProfiles_NormalizeName(normalized, sizeof(normalized), name)
    )
    {
        return 0;
    }

    if (strcmp(g_profiles[g_active_profile_index].name, normalized) == 0)
    {
        return 1;
    }

    TitanUserProfiles_CopyString(
        g_profiles[g_active_profile_index].name,
        sizeof(g_profiles[g_active_profile_index].name),
        normalized
    );
    g_profile_revision++;
    return 1;
}

int TitanUserProfiles_AddNamed(
    const char *name
)
{
    TitanUserProfiles_Initialize();

    return TitanUserProfiles_AddNamedWithAvatar(
        name,
        (g_next_profile_identifier - 1u) % TITAN_AVATAR_COUNT
    );
}

int TitanUserProfiles_AddNamedWithAvatar(
    const char *name,
    unsigned int avatar_id
)
{
    char normalized[TITAN_USER_PROFILE_NAME_CAPACITY];

    TitanUserProfiles_Initialize();

    if (TitanUserProfiles_CheckName(name) != TITAN_USER_PROFILE_NAME_OK)
    {
        return 0;
    }

    if (!TitanUserProfiles_NormalizeName(normalized, sizeof(normalized), name))
    {
        return 0;
    }

    if (avatar_id >= TITAN_AVATAR_COUNT)
    {
        return 0;
    }

    TitanUserProfiles_BuildProfile(
        &g_profiles[g_profile_count],
        g_next_profile_identifier,
        normalized,
        avatar_id
    );

    g_active_profile_index = g_profile_count;
    g_profile_count++;
    g_next_profile_identifier++;
    g_profile_revision++;

    return 1;
}

int TitanUserProfiles_RemoveActive(void)
{
    unsigned int index;

    TitanUserProfiles_Initialize();

    if (g_profile_count <= 1u)
    {
        return 0;
    }

    for (
        index = g_active_profile_index;
        index + 1u < g_profile_count;
        index++
    )
    {
        g_profiles[index] = g_profiles[index + 1u];
    }

    g_profile_count--;
    g_profiles[g_profile_count].occupied = 0;
    g_profiles[g_profile_count].identifier = 0u;
    g_profiles[g_profile_count].avatar_id = 0u;
    g_profiles[g_profile_count].name[0] = '\0';
    g_profiles[g_profile_count].vmc_path[0] = '\0';

    if (g_active_profile_index >= g_profile_count)
    {
        g_active_profile_index = g_profile_count - 1u;
    }

    g_profile_revision++;

    return 1;
}

int TitanUserProfiles_Select(
    unsigned int index
)
{
    TitanUserProfiles_Initialize();

    if (
        index >= g_profile_count ||
        !g_profiles[index].occupied
    )
    {
        return 0;
    }

    if (g_active_profile_index != index)
    {
        g_active_profile_index = index;
        g_profile_revision++;
    }

    return 1;
}

int TitanUserProfiles_SelectPrevious(void)
{
    unsigned int index;

    TitanUserProfiles_Initialize();

    if (g_profile_count == 0u)
    {
        return 0;
    }

    index = g_active_profile_index;

    if (index == 0u)
    {
        index = g_profile_count - 1u;
    }
    else
    {
        index--;
    }

    return TitanUserProfiles_Select(index);
}

int TitanUserProfiles_SelectNext(void)
{
    unsigned int index;

    TitanUserProfiles_Initialize();

    if (g_profile_count == 0u)
    {
        return 0;
    }

    index = g_active_profile_index + 1u;

    if (index >= g_profile_count)
    {
        index = 0u;
    }

    return TitanUserProfiles_Select(index);
}

int TitanUserProfiles_ExportState(
    TitanUserProfileState *state
)
{
    unsigned int index;

    TitanUserProfiles_Initialize();

    if (state == 0)
    {
        return 0;
    }

    memset(state, 0, sizeof(*state));
    state->profile_count = g_profile_count;
    state->active_profile_index = g_active_profile_index;
    state->next_profile_identifier = g_next_profile_identifier;

    for (index = 0u; index < g_profile_count; index++)
    {
        state->profiles[index] = g_profiles[index];
    }

    return 1;
}

int TitanUserProfiles_ValidateState(
    const TitanUserProfileState *state
)
{
    unsigned int index;
    unsigned int other_index;
    unsigned int highest_identifier;

    if (
        state == 0 ||
        state->profile_count == 0u ||
        state->profile_count > TITAN_USER_PROFILE_CAPACITY ||
        state->active_profile_index >= state->profile_count
    )
    {
        return 0;
    }

    highest_identifier = 0u;

    for (index = 0u; index < state->profile_count; index++)
    {
        const TitanUserProfile *profile;
        char normalized[TITAN_USER_PROFILE_NAME_CAPACITY];
        char expected_vmc_path[TITAN_USER_PROFILE_VMC_PATH_CAPACITY];

        profile = &state->profiles[index];

        if (
            !profile->occupied ||
            profile->identifier == 0u ||
            profile->avatar_id >= TITAN_AVATAR_COUNT ||
            !TitanUserProfiles_HasTerminator(
                profile->name,
                sizeof(profile->name)
            ) ||
            !TitanUserProfiles_IsValidUtf8(
                profile->name,
                sizeof(profile->name)
            ) ||
            !TitanUserProfiles_NormalizeName(
                normalized,
                sizeof(normalized),
                profile->name
            ) ||
            strcmp(normalized, profile->name) != 0 ||
            !TitanUserProfiles_HasTerminator(
                profile->vmc_path,
                sizeof(profile->vmc_path)
            ) ||
            !TitanUserProfiles_BuildVmcPath(
                profile->identifier,
                expected_vmc_path,
                sizeof(expected_vmc_path)
            ) ||
            strcmp(expected_vmc_path, profile->vmc_path) != 0
        )
        {
            return 0;
        }

        for (other_index = 0u; other_index < index; other_index++)
        {
            if (
                state->profiles[other_index].identifier == profile->identifier ||
                strcmp(state->profiles[other_index].name, profile->name) == 0
            )
            {
                return 0;
            }
        }

        if (profile->identifier > highest_identifier)
        {
            highest_identifier = profile->identifier;
        }
    }

    if (
        state->next_profile_identifier == 0u ||
        state->next_profile_identifier <= highest_identifier
    )
    {
        return 0;
    }

    return 1;
}

int TitanUserProfiles_ImportState(
    const TitanUserProfileState *state
)
{
    unsigned int index;

    TitanUserProfiles_Initialize();

    if (!TitanUserProfiles_ValidateState(state))
    {
        return 0;
    }

    memset(g_profiles, 0, sizeof(g_profiles));

    for (index = 0u; index < state->profile_count; index++)
    {
        TitanUserProfiles_BuildProfile(
            &g_profiles[index],
            state->profiles[index].identifier,
            state->profiles[index].name,
            state->profiles[index].avatar_id
        );
    }

    g_profile_count = state->profile_count;
    g_active_profile_index = state->active_profile_index;
    g_next_profile_identifier = state->next_profile_identifier;
    g_profile_revision++;

    return 1;
}

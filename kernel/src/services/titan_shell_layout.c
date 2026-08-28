#include "titan_shell_layout.h"

#include "titan_storage_io.h"
#include "titan_storage_paths.h"

#include <stdint.h>
#include <string.h>

#define TITAN_SHELL_LAYOUT_PATH TITAN_STORAGE_DEVICE_ROOT "/SHELLLAY.DAT"
#define TITAN_SHELL_LAYOUT_TEMP_PATH TITAN_STORAGE_DEVICE_ROOT "/SHELLLAY.TMP"
#define TITAN_SHELL_LAYOUT_BACKUP_PATH TITAN_STORAGE_DEVICE_ROOT "/SHELLLAY.BAK"
#define TITAN_SHELL_LAYOUT_EXPORT_PATH TITAN_STORAGE_DEVICE_ROOT "/SHELLLAY.EXP"
#define TITAN_SHELL_LAYOUT_EXPORT_TEMP_PATH TITAN_STORAGE_DEVICE_ROOT "/SHELLEXP.TMP"
#define TITAN_SHELL_LAYOUT_EXPORT_BACKUP_PATH TITAN_STORAGE_DEVICE_ROOT "/SHELLEXP.BAK"

#define TITAN_SHELL_LAYOUT_VERSION 1u
#define TITAN_SHELL_LAYOUT_HEADER_SIZE 16u
#define TITAN_SHELL_LAYOUT_PAYLOAD_SIZE 16u
#define TITAN_SHELL_LAYOUT_FILE_SIZE \
    (TITAN_SHELL_LAYOUT_HEADER_SIZE + TITAN_SHELL_LAYOUT_PAYLOAD_SIZE)

typedef struct TitanShellLayoutState
{
    unsigned char home_order[TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT];
    unsigned char system_order[TITAN_SHELL_LAYOUT_SYSTEM_ITEM_COUNT];
    unsigned int home_hidden_mask;
} TitanShellLayoutState;


#define TITAN_SHELL_LAYOUT_LIBRARY_CATALOG_INDEX 3u

static void TitanShellLayout_NormalizeLibraryLast(TitanShellLayoutState *state)
{
    unsigned int slot;
    unsigned int library_slot = TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT;
    if (state == 0) return;
    for (slot = 0u; slot < TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT; slot++)
    {
        if ((unsigned int)state->home_order[slot] == TITAN_SHELL_LAYOUT_LIBRARY_CATALOG_INDEX)
        {
            library_slot = slot;
            break;
        }
    }
    if (library_slot < TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT - 1u)
    {
        for (slot = library_slot; slot + 1u < TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT; slot++)
            state->home_order[slot] = state->home_order[slot + 1u];
        state->home_order[TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT - 1u] =
            (unsigned char)TITAN_SHELL_LAYOUT_LIBRARY_CATALOG_INDEX;
    }
    state->home_hidden_mask &= ~(1u << TITAN_SHELL_LAYOUT_LIBRARY_CATALOG_INDEX);
}

static const unsigned char g_shell_layout_magic[4] = {'T', 'S', 'L', '1'};
static TitanShellLayoutState g_layout;
static TitanShellLayoutStatus g_status = TITAN_SHELL_LAYOUT_NOT_INITIALIZED;

static void TitanShellLayout_WriteU32Le(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)(value & 0xffu);
    destination[1] = (unsigned char)((value >> 8) & 0xffu);
    destination[2] = (unsigned char)((value >> 16) & 0xffu);
    destination[3] = (unsigned char)((value >> 24) & 0xffu);
}

static uint32_t TitanShellLayout_ReadU32Le(const unsigned char *source)
{
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static uint32_t TitanShellLayout_Checksum(const unsigned char *data, unsigned int size)
{
    uint32_t checksum;
    unsigned int index;

    checksum = 2166136261u;
    for (index = 0u; index < size; index++)
    {
        checksum ^= data[index];
        checksum *= 16777619u;
    }
    return checksum;
}

static void TitanShellLayout_SetDefaultsInMemory(void)
{
    unsigned int index;

    memset(&g_layout, 0, sizeof(g_layout));
    for (index = 0u; index < TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT; index++)
    {
        g_layout.home_order[index] = (unsigned char)index;
    }
    for (index = 0u; index < TITAN_SHELL_LAYOUT_SYSTEM_ITEM_COUNT; index++)
    {
        g_layout.system_order[index] = (unsigned char)index;
    }
    TitanShellLayout_NormalizeLibraryLast(&g_layout);
}

static int TitanShellLayout_ValidatePermutation(
    const unsigned char *values,
    unsigned int count
)
{
    unsigned int left;
    unsigned int right;

    if (values == 0 || count == 0u)
    {
        return 0;
    }

    for (left = 0u; left < count; left++)
    {
        if ((unsigned int)values[left] >= count)
        {
            return 0;
        }

        for (right = left + 1u; right < count; right++)
        {
            if (values[left] == values[right])
            {
                return 0;
            }
        }
    }
    return 1;
}

static int TitanShellLayout_ValidateState(const TitanShellLayoutState *state)
{
    unsigned int allowed_mask;

    if (state == 0)
    {
        return 0;
    }

    allowed_mask = (1u << TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT) - 1u;
    return
        TitanShellLayout_ValidatePermutation(
            state->home_order,
            TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT
        ) &&
        TitanShellLayout_ValidatePermutation(
            state->system_order,
            TITAN_SHELL_LAYOUT_SYSTEM_ITEM_COUNT
        ) &&
        (state->home_hidden_mask & ~allowed_mask) == 0u;
}

static void TitanShellLayout_BuildPayload(unsigned char payload[TITAN_SHELL_LAYOUT_PAYLOAD_SIZE])
{
    memset(payload, 0, TITAN_SHELL_LAYOUT_PAYLOAD_SIZE);
    memcpy(payload, g_layout.home_order, TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT);
    memcpy(
        payload + 4u,
        g_layout.system_order,
        TITAN_SHELL_LAYOUT_SYSTEM_ITEM_COUNT
    );
    TitanShellLayout_WriteU32Le(payload + 12u, g_layout.home_hidden_mask);
}

static int TitanShellLayout_ParsePayload(
    const unsigned char payload[TITAN_SHELL_LAYOUT_PAYLOAD_SIZE],
    TitanShellLayoutState *state
)
{
    if (payload == 0 || state == 0)
    {
        return 0;
    }

    memset(state, 0, sizeof(*state));
    memcpy(state->home_order, payload, TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT);
    memcpy(
        state->system_order,
        payload + 4u,
        TITAN_SHELL_LAYOUT_SYSTEM_ITEM_COUNT
    );
    state->home_hidden_mask = TitanShellLayout_ReadU32Le(payload + 12u);
    TitanShellLayout_NormalizeLibraryLast(state);
    return TitanShellLayout_ValidateState(state);
}

static int TitanShellLayout_ReadFile(
    const char *path,
    TitanShellLayoutState *state,
    TitanShellLayoutStatus *status
)
{
    unsigned char file_data[TITAN_SHELL_LAYOUT_FILE_SIZE];
    TitanStorageIoSize file_size;
    uint32_t version;
    uint32_t expected_checksum;
    uint32_t payload_size;
    int descriptor;

    if (path == 0 || state == 0 || status == 0)
    {
        return -1;
    }

    if (!TitanStorageIo_PathInfo64(path, 0, &file_size))
    {
        *status = TITAN_SHELL_LAYOUT_READY_DEFAULTS;
        return 0;
    }

    if (file_size != TITAN_SHELL_LAYOUT_FILE_SIZE)
    {
        *status = TITAN_SHELL_LAYOUT_CORRUPT;
        return -1;
    }

    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
    {
        *status = TITAN_SHELL_LAYOUT_IO_ERROR;
        return -1;
    }

    if (
        !TitanStorageIo_ReadExact(descriptor, file_data, sizeof(file_data)) ||
        TitanStorageIo_Close(descriptor) < 0
    )
    {
        *status = TITAN_SHELL_LAYOUT_IO_ERROR;
        return -1;
    }

    if (memcmp(file_data, g_shell_layout_magic, sizeof(g_shell_layout_magic)) != 0)
    {
        *status = TITAN_SHELL_LAYOUT_CORRUPT;
        return -1;
    }

    version = TitanShellLayout_ReadU32Le(file_data + 4u);
    expected_checksum = TitanShellLayout_ReadU32Le(file_data + 8u);
    payload_size = TitanShellLayout_ReadU32Le(file_data + 12u);

    if (version > TITAN_SHELL_LAYOUT_VERSION)
    {
        *status = TITAN_SHELL_LAYOUT_FUTURE_VERSION;
        return -1;
    }

    if (
        version != TITAN_SHELL_LAYOUT_VERSION ||
        payload_size != TITAN_SHELL_LAYOUT_PAYLOAD_SIZE ||
        TitanShellLayout_Checksum(
            file_data + TITAN_SHELL_LAYOUT_HEADER_SIZE,
            TITAN_SHELL_LAYOUT_PAYLOAD_SIZE
        ) != expected_checksum ||
        !TitanShellLayout_ParsePayload(
            file_data + TITAN_SHELL_LAYOUT_HEADER_SIZE,
            state
        )
    )
    {
        *status = TITAN_SHELL_LAYOUT_CORRUPT;
        return -1;
    }

    *status = TITAN_SHELL_LAYOUT_READY;
    return 1;
}

static int TitanShellLayout_WriteFile(
    const char *temporary_path,
    const char *target_path,
    const char *backup_path
)
{
    unsigned char file_data[TITAN_SHELL_LAYOUT_FILE_SIZE];
    unsigned char *payload;
    uint32_t checksum;
    int descriptor;

    memset(file_data, 0, sizeof(file_data));
    memcpy(file_data, g_shell_layout_magic, sizeof(g_shell_layout_magic));
    TitanShellLayout_WriteU32Le(file_data + 4u, TITAN_SHELL_LAYOUT_VERSION);
    payload = file_data + TITAN_SHELL_LAYOUT_HEADER_SIZE;
    TitanShellLayout_BuildPayload(payload);
    checksum = TitanShellLayout_Checksum(payload, TITAN_SHELL_LAYOUT_PAYLOAD_SIZE);
    TitanShellLayout_WriteU32Le(file_data + 8u, checksum);
    TitanShellLayout_WriteU32Le(file_data + 12u, TITAN_SHELL_LAYOUT_PAYLOAD_SIZE);

    descriptor = TitanStorageIo_Open(
        temporary_path,
        TITAN_STORAGE_IO_WRITE |
        TITAN_STORAGE_IO_CREATE |
        TITAN_STORAGE_IO_TRUNCATE
    );
    if (descriptor < 0)
    {
        return 0;
    }

    if (
        !TitanStorageIo_WriteExact(descriptor, file_data, sizeof(file_data)) ||
        TitanStorageIo_Flush(descriptor) < 0 ||
        TitanStorageIo_Close(descriptor) < 0
    )
    {
        TitanStorageIo_Remove(temporary_path);
        return 0;
    }

    if (!TitanStorageIo_AtomicReplace(temporary_path, target_path, backup_path))
    {
        TitanStorageIo_Remove(temporary_path);
        return 0;
    }
    return 1;
}

void TitanShellLayout_Initialize(void)
{
    TitanShellLayoutState loaded;
    TitanShellLayoutStatus status;
    int result;

    TitanShellLayout_SetDefaultsInMemory();
    status = TITAN_SHELL_LAYOUT_READY_DEFAULTS;
    result = TitanShellLayout_ReadFile(TITAN_SHELL_LAYOUT_PATH, &loaded, &status);
    if (result > 0)
    {
        g_layout = loaded;
        g_status = TITAN_SHELL_LAYOUT_READY;
        return;
    }

    if (
        result < 0 &&
        status != TITAN_SHELL_LAYOUT_FUTURE_VERSION &&
        TitanShellLayout_ReadFile(
            TITAN_SHELL_LAYOUT_BACKUP_PATH,
            &loaded,
            &status
        ) > 0
    )
    {
        g_layout = loaded;
        g_status = TITAN_SHELL_LAYOUT_READY;
        (void)TitanShellLayout_Save();
        return;
    }

    if (status == TITAN_SHELL_LAYOUT_FUTURE_VERSION)
    {
        g_status = status;
        return;
    }

    g_status = result < 0 ? status : TITAN_SHELL_LAYOUT_READY_DEFAULTS;
}

TitanShellLayoutStatus TitanShellLayout_GetStatus(void)
{
    return g_status;
}

unsigned int TitanShellLayout_GetHomeCatalogIndex(unsigned int slot)
{
    if (slot >= TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT)
    {
        return TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT;
    }
    return g_layout.home_order[slot];
}

int TitanShellLayout_GetHomeSlot(unsigned int catalog_index)
{
    unsigned int slot;

    for (slot = 0u; slot < TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT; slot++)
    {
        if ((unsigned int)g_layout.home_order[slot] == catalog_index)
        {
            return (int)slot;
        }
    }
    return -1;
}

int TitanShellLayout_IsHomeHidden(unsigned int catalog_index)
{
    if (catalog_index >= TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT)
    {
        return 0;
    }
    return (g_layout.home_hidden_mask & (1u << catalog_index)) != 0u;
}

int TitanShellLayout_SetHomeHidden(unsigned int catalog_index, int hidden)
{
    if (catalog_index == TITAN_SHELL_LAYOUT_LIBRARY_CATALOG_INDEX) return hidden ? 0 : 1;
    unsigned int bit;

    if (catalog_index >= TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT)
    {
        return 0;
    }

    bit = 1u << catalog_index;
    if (hidden)
    {
        g_layout.home_hidden_mask |= bit;
    }
    else
    {
        g_layout.home_hidden_mask &= ~bit;
    }
    return 1;
}

static int TitanShellLayout_MoveInOrder(
    unsigned char *order,
    unsigned int count,
    unsigned int catalog_index,
    int delta
)
{
    int slot;
    int target;
    unsigned char temporary;
    unsigned int index;

    if (order == 0 || delta == 0)
    {
        return 0;
    }

    slot = -1;
    for (index = 0u; index < count; index++)
    {
        if ((unsigned int)order[index] == catalog_index)
        {
            slot = (int)index;
            break;
        }
    }
    if (slot < 0)
    {
        return 0;
    }

    target = slot + (delta < 0 ? -1 : 1);
    if (target < 0 || target >= (int)count)
    {
        return 0;
    }

    temporary = order[slot];
    order[slot] = order[target];
    order[target] = temporary;
    return 1;
}

int TitanShellLayout_MoveHome(unsigned int catalog_index, int delta)
{
    int slot;
    int target;
    if (catalog_index == TITAN_SHELL_LAYOUT_LIBRARY_CATALOG_INDEX) return 0;
    slot = TitanShellLayout_GetHomeSlot(catalog_index);
    target = slot + (delta < 0 ? -1 : 1);
    if (slot < 0 || target < 0 ||
        target >= (int)TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT - 1)
        return 0;
    return TitanShellLayout_MoveInOrder(
        g_layout.home_order,
        TITAN_SHELL_LAYOUT_HOME_ITEM_COUNT,
        catalog_index,
        delta
    );
}

unsigned int TitanShellLayout_GetSystemCatalogIndex(unsigned int slot)
{
    if (slot >= TITAN_SHELL_LAYOUT_SYSTEM_ITEM_COUNT)
    {
        return TITAN_SHELL_LAYOUT_SYSTEM_ITEM_COUNT;
    }
    return g_layout.system_order[slot];
}

int TitanShellLayout_GetSystemSlot(unsigned int catalog_index)
{
    unsigned int slot;

    for (slot = 0u; slot < TITAN_SHELL_LAYOUT_SYSTEM_ITEM_COUNT; slot++)
    {
        if ((unsigned int)g_layout.system_order[slot] == catalog_index)
        {
            return (int)slot;
        }
    }
    return -1;
}

int TitanShellLayout_MoveSystem(unsigned int catalog_index, int delta)
{
    return TitanShellLayout_MoveInOrder(
        g_layout.system_order,
        TITAN_SHELL_LAYOUT_SYSTEM_ITEM_COUNT,
        catalog_index,
        delta
    );
}

int TitanShellLayout_Save(void)
{
    unsigned char file_data[TITAN_SHELL_LAYOUT_FILE_SIZE];
    unsigned char *payload;
    uint32_t checksum;
    int descriptor;

    if (!TitanShellLayout_ValidateState(&g_layout))
    {
        g_status = TITAN_SHELL_LAYOUT_CORRUPT;
        return 0;
    }

    memset(file_data, 0, sizeof(file_data));
    memcpy(file_data, g_shell_layout_magic, sizeof(g_shell_layout_magic));
    TitanShellLayout_WriteU32Le(file_data + 4u, TITAN_SHELL_LAYOUT_VERSION);
    payload = file_data + TITAN_SHELL_LAYOUT_HEADER_SIZE;
    TitanShellLayout_BuildPayload(payload);
    checksum = TitanShellLayout_Checksum(payload, TITAN_SHELL_LAYOUT_PAYLOAD_SIZE);
    TitanShellLayout_WriteU32Le(file_data + 8u, checksum);
    TitanShellLayout_WriteU32Le(file_data + 12u, TITAN_SHELL_LAYOUT_PAYLOAD_SIZE);

    descriptor = TitanStorageIo_Open(
        TITAN_SHELL_LAYOUT_TEMP_PATH,
        TITAN_STORAGE_IO_WRITE |
        TITAN_STORAGE_IO_CREATE |
        TITAN_STORAGE_IO_TRUNCATE
    );
    if (descriptor < 0)
    {
        g_status = TITAN_SHELL_LAYOUT_IO_ERROR;
        return 0;
    }

    if (
        !TitanStorageIo_WriteExact(descriptor, file_data, sizeof(file_data)) ||
        TitanStorageIo_Flush(descriptor) < 0 ||
        TitanStorageIo_Close(descriptor) < 0
    )
    {
        TitanStorageIo_Remove(TITAN_SHELL_LAYOUT_TEMP_PATH);
        g_status = TITAN_SHELL_LAYOUT_IO_ERROR;
        return 0;
    }

    if (!TitanStorageIo_AtomicReplace(
        TITAN_SHELL_LAYOUT_TEMP_PATH,
        TITAN_SHELL_LAYOUT_PATH,
        TITAN_SHELL_LAYOUT_BACKUP_PATH
    ))
    {
        TitanStorageIo_Remove(TITAN_SHELL_LAYOUT_TEMP_PATH);
        g_status = TITAN_SHELL_LAYOUT_IO_ERROR;
        return 0;
    }

    g_status = TITAN_SHELL_LAYOUT_READY;
    return 1;
}

int TitanShellLayout_ResetDefaults(void)
{
    TitanShellLayoutState previous;

    previous = g_layout;
    TitanShellLayout_SetDefaultsInMemory();
    if (TitanShellLayout_Save())
    {
        return 1;
    }

    g_layout = previous;
    return 0;
}

int TitanShellLayout_Export(void)
{
    return TitanShellLayout_WriteFile(
        TITAN_SHELL_LAYOUT_EXPORT_TEMP_PATH,
        TITAN_SHELL_LAYOUT_EXPORT_PATH,
        TITAN_SHELL_LAYOUT_EXPORT_BACKUP_PATH
    );
}

int TitanShellLayout_Import(void)
{
    TitanShellLayoutState imported;
    TitanShellLayoutState previous;
    TitanShellLayoutStatus status;

    if (TitanShellLayout_ReadFile(
        TITAN_SHELL_LAYOUT_EXPORT_PATH,
        &imported,
        &status
    ) <= 0)
    {
        return 0;
    }

    previous = g_layout;
    g_layout = imported;
    if (TitanShellLayout_Save())
    {
        return 1;
    }

    g_layout = previous;
    return 0;
}

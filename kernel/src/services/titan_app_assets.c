#include "titan_app_assets.h"

#include "titan_applications.h"
#include "titan_avatar_ids.h"
#include "titan_storage_io.h"
#include "titan_storage_scheduler.h"
#include "titan_video.h"
#include "titan_avatars.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TITAN_APP_ICON_BYTES (TITAN_APP_ICON_WIDTH * TITAN_APP_ICON_HEIGHT * 2u)
#define TITAN_APP_TILE_BYTES (TITAN_APP_TILE_WIDTH * TITAN_APP_TILE_HEIGHT * 2u)
#define TITAN_APP_BACKGROUND_PRIMARY_BYTES \
    (TITAN_APP_BACKGROUND_PRIMARY_WIDTH * TITAN_APP_BACKGROUND_PRIMARY_HEIGHT * 2u)
#define TITAN_APP_BACKGROUND_LEGACY_BYTES \
    (TITAN_APP_BACKGROUND_LEGACY_WIDTH * TITAN_APP_BACKGROUND_LEGACY_HEIGHT * 2u)
#define TITAN_APP_SELECTED_IO_CHUNK (16u * 1024u)
#define TITAN_APP_AVATAR_ATLAS_CELLS \
    ((TITAN_VIDEO_AVATAR_ATLAS_WIDTH / TITAN_AVATAR_CELL_SIZE) * \
     (TITAN_VIDEO_AVATAR_ATLAS_HEIGHT / TITAN_AVATAR_CELL_SIZE))
#define TITAN_APP_ICON_FIRST_CELL TITAN_AVATAR_COUNT
#define TITAN_APP_ICON_AVAILABLE_CELLS \
    (TITAN_APP_AVATAR_ATLAS_CELLS - TITAN_APP_ICON_FIRST_CELL)

#if TITAN_APP_ICON_CAPACITY > TITAN_APP_ICON_AVAILABLE_CELLS
#error "Titan App icon capacity exceeds unused avatar-atlas cells"
#endif

#if TITAN_APP_ICON_RUNTIME_SIZE != TITAN_AVATAR_CELL_SIZE
#error "Titan App runtime icon cell must match the avatar atlas cell"
#endif

typedef struct TitanAppAssetSlot
{
    unsigned int application_index;
    unsigned int atlas_cell;
    int valid;
    char title_id[TITAN_APPLICATION_TITLE_ID_CAPACITY];
    char icon_path[TITAN_APPLICATION_PATH_CAPACITY];
} TitanAppAssetSlot;

typedef enum TitanAppSelectedLoadPhase
{
    TITAN_APP_SELECTED_LOAD_IDLE = 0,
    TITAN_APP_SELECTED_LOAD_OPEN_TILE,
    TITAN_APP_SELECTED_LOAD_READ_TILE,
    TITAN_APP_SELECTED_LOAD_OPEN_BACKGROUND,
    TITAN_APP_SELECTED_LOAD_READ_BACKGROUND,
    TITAN_APP_SELECTED_LOAD_PUBLISH
} TitanAppSelectedLoadPhase;

typedef struct TitanAppAssetsState
{
    int initialized;
    volatile int build_requested;
    volatile int build_running;
    volatile int pending_ready;
    unsigned int build_index;
    unsigned int build_slot_count;
    unsigned int active_slot_count;
    int atlas_uploaded;
    TitanAppAssetSlot build_slots[TITAN_APP_ICON_CAPACITY];
    TitanAppAssetSlot active_slots[TITAN_APP_ICON_CAPACITY];
    unsigned short icon_cells[
        TITAN_APP_ICON_CAPACITY *
        TITAN_APP_ICON_RUNTIME_SIZE *
        TITAN_APP_ICON_RUNTIME_SIZE
    ] __attribute__((aligned(64)));
    unsigned short atlas[
        TITAN_VIDEO_AVATAR_ATLAS_WIDTH * TITAN_VIDEO_AVATAR_ATLAS_HEIGHT
    ] __attribute__((aligned(64)));

    /* Main-thread request side. */
    volatile unsigned int selection_generation;
    volatile int selection_requested;
    volatile int selection_loading;
    int selection_suspended;
    unsigned int requested_application_index;
    char requested_tile_path[TITAN_APPLICATION_PATH_CAPACITY];
    char requested_background_path[TITAN_APPLICATION_PATH_CAPACITY];

    /* Worker-owned in-flight side. */
    TitanAppSelectedLoadPhase selection_phase;
    unsigned int loading_generation;
    unsigned int loading_application_index;
    char loading_tile_path[TITAN_APPLICATION_PATH_CAPACITY];
    char loading_background_path[TITAN_APPLICATION_PATH_CAPACITY];
    int selection_descriptor;
    unsigned char *selection_buffer;
    unsigned int selection_size;
    unsigned int selection_offset;
    unsigned short *work_tile;
    unsigned short *work_background;
    unsigned int work_background_width;
    unsigned int work_background_height;

    /* Worker -> main publication boundary. */
    volatile int selection_pending_ready;
    unsigned int pending_generation;
    unsigned int pending_application_index;
    unsigned short *pending_tile;
    unsigned short *pending_background;
    unsigned int pending_background_width;
    unsigned int pending_background_height;

    /* Main-thread active side. */
    unsigned int active_selection_generation;
    unsigned int active_application_index;
    unsigned short *active_tile;
    unsigned short *active_background;
    unsigned int active_background_width;
    unsigned int active_background_height;
} TitanAppAssetsState;

static TitanAppAssetsState g_app_assets;

static void TitanAppAssets_CopyText(
    char *destination,
    unsigned int capacity,
    const char *source
)
{
    unsigned int index;

    if (destination == 0 || capacity == 0u)
        return;
    index = 0u;
    while (source != 0 && source[index] != '\0' && index + 1u < capacity)
    {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static unsigned short TitanAppAssets_Rgb565ToPsm16(unsigned short pixel)
{
    unsigned int red = (pixel >> 11u) & 0x1fu;
    unsigned int green = (pixel >> 5u) & 0x3fu;
    unsigned int blue = pixel & 0x1fu;

    green = (green + 1u) >> 1u;
    if (green > 0x1fu) green = 0x1fu;
    return (unsigned short)(red | (green << 5u) | (blue << 10u) | 0x8000u);
}

static void TitanAppAssets_ResetCompositeAtlas(void)
{
    memset(g_app_assets.atlas, 0, sizeof(g_app_assets.atlas));
    (void)TitanAvatars_CopyBaseAtlas(
        g_app_assets.atlas,
        TITAN_VIDEO_AVATAR_ATLAS_WIDTH * TITAN_VIDEO_AVATAR_ATLAS_HEIGHT
    );
}

/* Source icon files are RGB565. The avatar atlas is uploaded as GS PSM16, so
 * the downfilter must perform the channel-layout conversion here rather than
 * passing RGB565 words straight to GS memory. */
static unsigned short TitanAppAssets_Filter2x2(
    const unsigned short *source,
    unsigned int source_x,
    unsigned int source_y
)
{
    unsigned int red = 0u;
    unsigned int green = 0u;
    unsigned int blue = 0u;
    unsigned int dx;
    unsigned int dy;

    for (dy = 0u; dy < 2u; dy++)
    {
        for (dx = 0u; dx < 2u; dx++)
        {
            unsigned short pixel = source[
                (source_y + dy) * TITAN_APP_ICON_WIDTH + source_x + dx
            ];
            red += (pixel >> 11u) & 0x1fu;
            green += (pixel >> 5u) & 0x3fu;
            blue += pixel & 0x1fu;
        }
    }
    red = (red + 2u) / 4u;
    green = (green + 2u) / 4u;
    blue = (blue + 2u) / 4u;
    return TitanAppAssets_Rgb565ToPsm16(
        (unsigned short)((red << 11u) | (green << 5u) | blue)
    );
}

static void TitanAppAssets_FilterIcon(
    unsigned int slot_index,
    const unsigned short *icon
)
{
    unsigned int x;
    unsigned int y;
    unsigned short *destination = &g_app_assets.icon_cells[
        slot_index * TITAN_APP_ICON_RUNTIME_SIZE * TITAN_APP_ICON_RUNTIME_SIZE
    ];

    for (y = 0u; y < TITAN_APP_ICON_RUNTIME_SIZE; y++)
    {
        for (x = 0u; x < TITAN_APP_ICON_RUNTIME_SIZE; x++)
        {
            destination[y * TITAN_APP_ICON_RUNTIME_SIZE + x] =
                TitanAppAssets_Filter2x2(icon, x * 2u, y * 2u);
        }
    }
}

static void TitanAppAssets_CopyIconToAtlas(
    const TitanAppAssetSlot *slot,
    unsigned int slot_index
)
{
    unsigned int destination_x =
        (slot->atlas_cell % TITAN_AVATAR_ATLAS_COLUMNS) * TITAN_AVATAR_CELL_SIZE;
    unsigned int destination_y =
        (slot->atlas_cell / TITAN_AVATAR_ATLAS_COLUMNS) * TITAN_AVATAR_CELL_SIZE;
    unsigned int row;
    const unsigned short *source = &g_app_assets.icon_cells[
        slot_index * TITAN_APP_ICON_RUNTIME_SIZE * TITAN_APP_ICON_RUNTIME_SIZE
    ];

    for (row = 0u; row < TITAN_APP_ICON_RUNTIME_SIZE; row++)
    {
        memcpy(
            &g_app_assets.atlas[
                (destination_y + row) * TITAN_VIDEO_AVATAR_ATLAS_WIDTH +
                destination_x
            ],
            &source[row * TITAN_APP_ICON_RUNTIME_SIZE],
            TITAN_APP_ICON_RUNTIME_SIZE * sizeof(unsigned short)
        );
    }
}

static void TitanAppAssets_CloseSelectionDescriptor(void)
{
    if (g_app_assets.selection_descriptor >= 0)
    {
        (void)TitanStorageIo_Close(g_app_assets.selection_descriptor);
        g_app_assets.selection_descriptor = -1;
    }
}

static void TitanAppAssets_FreeWorkerSelection(void)
{
    TitanAppAssets_CloseSelectionDescriptor();
    free(g_app_assets.work_tile);
    free(g_app_assets.work_background);
    g_app_assets.work_tile = 0;
    g_app_assets.work_background = 0;
    g_app_assets.work_background_width = 0u;
    g_app_assets.work_background_height = 0u;
    g_app_assets.selection_buffer = 0;
    g_app_assets.selection_size = 0u;
    g_app_assets.selection_offset = 0u;
}

static void TitanAppAssets_FreePendingSelection(void)
{
    free(g_app_assets.pending_tile);
    free(g_app_assets.pending_background);
    g_app_assets.pending_tile = 0;
    g_app_assets.pending_background = 0;
    g_app_assets.pending_background_width = 0u;
    g_app_assets.pending_background_height = 0u;
    g_app_assets.selection_pending_ready = 0;
}

static void TitanAppAssets_FreeActiveSelection(void)
{
    free(g_app_assets.active_tile);
    free(g_app_assets.active_background);
    g_app_assets.active_tile = 0;
    g_app_assets.active_background = 0;
    g_app_assets.active_background_width = 0u;
    g_app_assets.active_background_height = 0u;
    g_app_assets.active_application_index = TITAN_APP_ASSET_NO_SELECTION;
    g_app_assets.active_selection_generation = 0u;
}

static void TitanAppAssets_BeginSelectionLoad(void)
{
    TitanAppAssets_FreeWorkerSelection();
    g_app_assets.loading_generation = g_app_assets.selection_generation;
    g_app_assets.loading_application_index = g_app_assets.requested_application_index;
    TitanAppAssets_CopyText(
        g_app_assets.loading_tile_path,
        sizeof(g_app_assets.loading_tile_path),
        g_app_assets.requested_tile_path
    );
    TitanAppAssets_CopyText(
        g_app_assets.loading_background_path,
        sizeof(g_app_assets.loading_background_path),
        g_app_assets.requested_background_path
    );
    g_app_assets.selection_requested = 0;
    g_app_assets.selection_loading = 1;
    g_app_assets.selection_phase = TITAN_APP_SELECTED_LOAD_OPEN_TILE;
}

static void TitanAppAssets_RestartSelectionIfStale(void)
{
    if (g_app_assets.loading_generation == g_app_assets.selection_generation)
        return;
    TitanAppAssets_FreeWorkerSelection();
    g_app_assets.selection_loading = 0;
    g_app_assets.selection_phase = TITAN_APP_SELECTED_LOAD_IDLE;
    g_app_assets.selection_requested = g_app_assets.selection_suspended ? 0 : 1;
}

static int TitanAppAssets_PrepareRead(
    const char *path,
    unsigned int expected_a,
    unsigned int expected_b,
    unsigned char **buffer,
    unsigned int *size
)
{
    TitanStorageIoSize file_size;
    int descriptor;

    *buffer = 0;
    *size = 0u;
    if (path == 0 || path[0] == '\0' ||
        !TitanStorageIo_PathInfo64(path, 0, &file_size) ||
        (file_size != (TitanStorageIoSize)expected_a &&
         (expected_b == 0u || file_size != (TitanStorageIoSize)expected_b)))
        return -1;
    if (file_size > (TitanStorageIoSize)UINT_MAX)
        return -1;
    *buffer = (unsigned char *)malloc((size_t)file_size);
    if (*buffer == 0)
        return -1;
    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor < 0)
    {
        free(*buffer);
        *buffer = 0;
        return -1;
    }
    *size = (unsigned int)file_size;
    return descriptor;
}

static int TitanAppAssets_StepSelectionLoad(void)
{
    unsigned int chunk;

    if (g_app_assets.selection_requested && !g_app_assets.selection_loading)
    {
        /* pending_* belongs to the worker->main handoff. Never free or
         * overwrite it from the worker while the main thread may commit it. */
        if (g_app_assets.selection_pending_ready)
            return 1;
        TitanAppAssets_BeginSelectionLoad();
    }
    if (!g_app_assets.selection_loading)
        return 0;

    TitanAppAssets_RestartSelectionIfStale();
    if (!g_app_assets.selection_loading)
        return 1;

    switch (g_app_assets.selection_phase)
    {
        case TITAN_APP_SELECTED_LOAD_OPEN_TILE:
        {
            unsigned char *buffer = 0;
            unsigned int size = 0u;
            int descriptor;
            if (g_app_assets.loading_application_index == TITAN_APP_ASSET_NO_SELECTION)
            {
                g_app_assets.selection_phase = TITAN_APP_SELECTED_LOAD_PUBLISH;
                return 1;
            }
            descriptor = TitanAppAssets_PrepareRead(
                g_app_assets.loading_tile_path,
                TITAN_APP_TILE_BYTES,
                0u,
                &buffer,
                &size
            );
            if (descriptor < 0)
            {
                g_app_assets.selection_phase = TITAN_APP_SELECTED_LOAD_OPEN_BACKGROUND;
                return 1;
            }
            g_app_assets.selection_descriptor = descriptor;
            g_app_assets.selection_buffer = buffer;
            g_app_assets.selection_size = size;
            g_app_assets.selection_offset = 0u;
            g_app_assets.work_tile = (unsigned short *)buffer;
            g_app_assets.selection_phase = TITAN_APP_SELECTED_LOAD_READ_TILE;
            return 1;
        }

        case TITAN_APP_SELECTED_LOAD_READ_TILE:
            chunk = g_app_assets.selection_size - g_app_assets.selection_offset;
            if (chunk > TITAN_APP_SELECTED_IO_CHUNK)
                chunk = TITAN_APP_SELECTED_IO_CHUNK;
            if (chunk != 0u &&
                !TitanStorageIo_ReadExact(
                    g_app_assets.selection_descriptor,
                    g_app_assets.selection_buffer + g_app_assets.selection_offset,
                    chunk))
            {
                TitanAppAssets_CloseSelectionDescriptor();
                free(g_app_assets.work_tile);
                g_app_assets.work_tile = 0;
                g_app_assets.selection_buffer = 0;
                g_app_assets.selection_phase = TITAN_APP_SELECTED_LOAD_OPEN_BACKGROUND;
                return 1;
            }
            g_app_assets.selection_offset += chunk;
            if (g_app_assets.selection_offset >= g_app_assets.selection_size)
            {
                unsigned int pixel;
                TitanAppAssets_CloseSelectionDescriptor();
                for (pixel = 0u; pixel < TITAN_APP_TILE_WIDTH * TITAN_APP_TILE_HEIGHT; pixel++)
                    g_app_assets.work_tile[pixel] =
                        TitanAppAssets_Rgb565ToPsm16(g_app_assets.work_tile[pixel]);
                g_app_assets.selection_buffer = 0;
                g_app_assets.selection_phase = TITAN_APP_SELECTED_LOAD_OPEN_BACKGROUND;
            }
            return 1;

        case TITAN_APP_SELECTED_LOAD_OPEN_BACKGROUND:
        {
            unsigned char *buffer = 0;
            unsigned int size = 0u;
            int descriptor = TitanAppAssets_PrepareRead(
                g_app_assets.loading_background_path,
                TITAN_APP_BACKGROUND_PRIMARY_BYTES,
                TITAN_APP_BACKGROUND_LEGACY_BYTES,
                &buffer,
                &size
            );
            if (descriptor < 0)
            {
                g_app_assets.selection_phase = TITAN_APP_SELECTED_LOAD_PUBLISH;
                return 1;
            }
            g_app_assets.selection_descriptor = descriptor;
            g_app_assets.selection_buffer = buffer;
            g_app_assets.selection_size = size;
            g_app_assets.selection_offset = 0u;
            g_app_assets.work_background = (unsigned short *)buffer;
            if (size == TITAN_APP_BACKGROUND_PRIMARY_BYTES)
            {
                g_app_assets.work_background_width = TITAN_APP_BACKGROUND_PRIMARY_WIDTH;
                g_app_assets.work_background_height = TITAN_APP_BACKGROUND_PRIMARY_HEIGHT;
            }
            else
            {
                g_app_assets.work_background_width = TITAN_APP_BACKGROUND_LEGACY_WIDTH;
                g_app_assets.work_background_height = TITAN_APP_BACKGROUND_LEGACY_HEIGHT;
            }
            g_app_assets.selection_phase = TITAN_APP_SELECTED_LOAD_READ_BACKGROUND;
            return 1;
        }

        case TITAN_APP_SELECTED_LOAD_READ_BACKGROUND:
            chunk = g_app_assets.selection_size - g_app_assets.selection_offset;
            if (chunk > TITAN_APP_SELECTED_IO_CHUNK)
                chunk = TITAN_APP_SELECTED_IO_CHUNK;
            if (chunk != 0u &&
                !TitanStorageIo_ReadExact(
                    g_app_assets.selection_descriptor,
                    g_app_assets.selection_buffer + g_app_assets.selection_offset,
                    chunk))
            {
                TitanAppAssets_CloseSelectionDescriptor();
                free(g_app_assets.work_background);
                g_app_assets.work_background = 0;
                g_app_assets.work_background_width = 0u;
                g_app_assets.work_background_height = 0u;
                g_app_assets.selection_buffer = 0;
                g_app_assets.selection_phase = TITAN_APP_SELECTED_LOAD_PUBLISH;
                return 1;
            }
            g_app_assets.selection_offset += chunk;
            if (g_app_assets.selection_offset >= g_app_assets.selection_size)
            {
                TitanAppAssets_CloseSelectionDescriptor();
                g_app_assets.selection_buffer = 0;
                g_app_assets.selection_phase = TITAN_APP_SELECTED_LOAD_PUBLISH;
            }
            return 1;

        case TITAN_APP_SELECTED_LOAD_PUBLISH:
            if (g_app_assets.loading_generation != g_app_assets.selection_generation)
            {
                TitanAppAssets_FreeWorkerSelection();
                g_app_assets.selection_loading = 0;
                g_app_assets.selection_requested =
                    g_app_assets.selection_suspended ? 0 : 1;
                g_app_assets.selection_phase = TITAN_APP_SELECTED_LOAD_IDLE;
                return 1;
            }
            g_app_assets.pending_generation = g_app_assets.loading_generation;
            g_app_assets.pending_application_index = g_app_assets.loading_application_index;
            g_app_assets.pending_tile = g_app_assets.work_tile;
            g_app_assets.pending_background = g_app_assets.work_background;
            g_app_assets.pending_background_width = g_app_assets.work_background_width;
            g_app_assets.pending_background_height = g_app_assets.work_background_height;
            g_app_assets.work_tile = 0;
            g_app_assets.work_background = 0;
            g_app_assets.work_background_width = 0u;
            g_app_assets.work_background_height = 0u;
            g_app_assets.selection_pending_ready = 1;
            g_app_assets.selection_loading = 0;
            g_app_assets.selection_phase = TITAN_APP_SELECTED_LOAD_IDLE;
            return 0;

        case TITAN_APP_SELECTED_LOAD_IDLE:
        default:
            g_app_assets.selection_loading = 0;
            return 0;
    }
}

void TitanAppAssets_Initialize(void)
{
    memset(&g_app_assets, 0, sizeof(g_app_assets));
    g_app_assets.selection_descriptor = -1;
    g_app_assets.requested_application_index = TITAN_APP_ASSET_NO_SELECTION;
    g_app_assets.loading_application_index = TITAN_APP_ASSET_NO_SELECTION;
    g_app_assets.pending_application_index = TITAN_APP_ASSET_NO_SELECTION;
    g_app_assets.active_application_index = TITAN_APP_ASSET_NO_SELECTION;
    TitanAppAssets_ResetCompositeAtlas();
    g_app_assets.initialized = 1;
}

void TitanAppAssets_RequestRebuild(void)
{
    unsigned int index;
    unsigned int count;

    if (!g_app_assets.initialized)
        TitanAppAssets_Initialize();

    memset(g_app_assets.build_slots, 0, sizeof(g_app_assets.build_slots));
    memset(g_app_assets.icon_cells, 0, sizeof(g_app_assets.icon_cells));
    g_app_assets.build_slot_count = 0u;
    count = TitanApplications_GetCount();
    for (
        index = 0u;
        index < count && g_app_assets.build_slot_count < TITAN_APP_ICON_CAPACITY;
        index++
    )
    {
        const TitanApplication *application;
        TitanAppAssetSlot *slot;

        application = TitanApplications_Get(index);
        if (application == 0 || !application->installed || application->icon_path[0] == '\0')
            continue;
        slot = &g_app_assets.build_slots[g_app_assets.build_slot_count];
        slot->application_index = index;
        slot->atlas_cell = TITAN_APP_ICON_FIRST_CELL + g_app_assets.build_slot_count;
        TitanAppAssets_CopyText(slot->title_id, sizeof(slot->title_id), application->title_id);
        TitanAppAssets_CopyText(slot->icon_path, sizeof(slot->icon_path), application->icon_path);
        g_app_assets.build_slot_count++;
    }

    g_app_assets.build_index = 0u;
    g_app_assets.pending_ready = g_app_assets.build_slot_count == 0u;
    g_app_assets.build_running = 0;
    g_app_assets.build_requested = g_app_assets.build_slot_count > 0u;
    g_app_assets.atlas_uploaded = 0;
}

void TitanAppAssets_Select(unsigned int application_index)
{
    const TitanApplication *application = 0;
    int same_request;

    if (!g_app_assets.initialized)
        TitanAppAssets_Initialize();

    if (application_index != TITAN_APP_ASSET_NO_SELECTION)
        application = TitanApplications_Get(application_index);
    if (application == 0)
        application_index = TITAN_APP_ASSET_NO_SELECTION;

    same_request = g_app_assets.requested_application_index == application_index;
    if (same_request && !g_app_assets.selection_suspended)
        return;

    /* Main thread drops the previous selected live-area allocation before the
     * worker begins the next one. This keeps ownership at one selected app and
     * prevents a 640x480 double-buffer peak in EE RAM. */
    if (!same_request)
        TitanAppAssets_FreeActiveSelection();

    g_app_assets.requested_application_index = application_index;
    g_app_assets.requested_tile_path[0] = '\0';
    g_app_assets.requested_background_path[0] = '\0';
    if (application != 0)
    {
        TitanAppAssets_CopyText(
            g_app_assets.requested_tile_path,
            sizeof(g_app_assets.requested_tile_path),
            application->tile_path
        );
        TitanAppAssets_CopyText(
            g_app_assets.requested_background_path,
            sizeof(g_app_assets.requested_background_path),
            application->background_path
        );
    }
    g_app_assets.selection_suspended = 0;
    g_app_assets.selection_generation++;
    if (g_app_assets.selection_generation == 0u)
        g_app_assets.selection_generation = 1u;
    g_app_assets.selection_requested = 1;
    TitanStorageScheduler_NotifyBackgroundWork();
}

int TitanAppAssets_HasBuildWork(void)
{
    return g_app_assets.build_requested || g_app_assets.build_running ||
        g_app_assets.selection_requested || g_app_assets.selection_loading;
}

int TitanAppAssets_StepBuild(void)
{
    TitanAppAssetSlot *slot;
    unsigned short icon[TITAN_APP_ICON_WIDTH * TITAN_APP_ICON_HEIGHT];
    TitanStorageIoSize size;
    int descriptor;

    if (!g_app_assets.initialized)
        return 0;

    /* Selected art is latency-sensitive after horizontal HOME navigation. Give
     * it one bounded 16 KiB storage quantum before continuing the background
     * atlas rebuild. */
    if (g_app_assets.selection_requested || g_app_assets.selection_loading)
        return TitanAppAssets_StepSelectionLoad();

    if (g_app_assets.build_requested)
    {
        g_app_assets.build_requested = 0;
        g_app_assets.build_running = 1;
        g_app_assets.build_index = 0u;
    }
    if (!g_app_assets.build_running)
        return 0;
    if (g_app_assets.build_index >= g_app_assets.build_slot_count)
    {
        g_app_assets.build_running = 0;
        g_app_assets.pending_ready = 1;
        return 0;
    }

    slot = &g_app_assets.build_slots[g_app_assets.build_index];
    slot->valid = 0;
    descriptor = -1;
    if (TitanStorageIo_PathInfo64(slot->icon_path, 0, &size) && size == TITAN_APP_ICON_BYTES)
    {
        descriptor = TitanStorageIo_Open(slot->icon_path, TITAN_STORAGE_IO_READ);
        if (descriptor >= 0 && TitanStorageIo_ReadExact(descriptor, icon, sizeof(icon)))
        {
            TitanAppAssets_FilterIcon(g_app_assets.build_index, icon);
            slot->valid = 1;
        }
    }
    if (descriptor >= 0)
        TitanStorageIo_Close(descriptor);

    g_app_assets.build_index++;
    if (g_app_assets.build_index >= g_app_assets.build_slot_count)
    {
        g_app_assets.build_running = 0;
        g_app_assets.pending_ready = 1;
        return 0;
    }
    return 1;
}

int TitanAppAssets_CommitPendingAtlas(void)
{
    unsigned int index;
    int committed = 0;

    if (g_app_assets.selection_pending_ready)
    {
        if (g_app_assets.pending_generation == g_app_assets.selection_generation)
        {
            TitanAppAssets_FreeActiveSelection();
            g_app_assets.active_selection_generation = g_app_assets.pending_generation;
            g_app_assets.active_application_index = g_app_assets.pending_application_index;
            g_app_assets.active_tile = g_app_assets.pending_tile;
            g_app_assets.active_background = g_app_assets.pending_background;
            g_app_assets.active_background_width = g_app_assets.pending_background_width;
            g_app_assets.active_background_height = g_app_assets.pending_background_height;
            g_app_assets.pending_tile = 0;
            g_app_assets.pending_background = 0;
            g_app_assets.pending_background_width = 0u;
            g_app_assets.pending_background_height = 0u;
            g_app_assets.selection_pending_ready = 0;
            committed = 1;
        }
        else
        {
            TitanAppAssets_FreePendingSelection();
        }
    }

    if (!g_app_assets.pending_ready)
        return committed;
    g_app_assets.pending_ready = 0;

    TitanAppAssets_ResetCompositeAtlas();
    for (index = 0u; index < g_app_assets.build_slot_count; index++)
    {
        if (g_app_assets.build_slots[index].valid)
            TitanAppAssets_CopyIconToAtlas(&g_app_assets.build_slots[index], index);
    }
    memcpy(g_app_assets.active_slots, g_app_assets.build_slots, sizeof(g_app_assets.active_slots));
    g_app_assets.active_slot_count = g_app_assets.build_slot_count;

    if (TitanVideo_UploadAvatarAtlas16(
            g_app_assets.atlas,
            TITAN_VIDEO_AVATAR_ATLAS_WIDTH,
            TITAN_VIDEO_AVATAR_ATLAS_HEIGHT,
            TITAN_VIDEO_AVATAR_ATLAS_WIDTH) != TITAN_RESULT_OK)
    {
        g_app_assets.atlas_uploaded = 0;
        return committed;
    }
    g_app_assets.atlas_uploaded = 1;
    return 1;
}

void TitanAppAssets_EnsureUploaded(void)
{
    /* TitanVideo keeps the atlas source pointer across mode rebuilds. Selected
     * tile/background art is streamed from EE RAM only when visible. */
}

int TitanAppAssets_DrawIcon(
    unsigned int application_index,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
)
{
    unsigned int index;

    if (!g_app_assets.atlas_uploaded)
        return 0;
    for (index = 0u; index < g_app_assets.active_slot_count; index++)
    {
        unsigned int source_x;
        unsigned int source_y;
        TitanAppAssetSlot *slot = &g_app_assets.active_slots[index];

        if (!slot->valid || slot->application_index != application_index)
            continue;
        source_x = (slot->atlas_cell % TITAN_AVATAR_ATLAS_COLUMNS) * TITAN_AVATAR_CELL_SIZE;
        source_y = (slot->atlas_cell / TITAN_AVATAR_ATLAS_COLUMNS) * TITAN_AVATAR_CELL_SIZE;
        TitanVideo_DrawAvatarAtlasRegion16(
            x, y, width, height,
            source_x, source_y,
            TITAN_APP_ICON_RUNTIME_SIZE,
            TITAN_APP_ICON_RUNTIME_SIZE,
            alpha
        );
        return 1;
    }
    return 0;
}

int TitanAppAssets_IsSelectionLoading(void)
{
    if (!g_app_assets.initialized || g_app_assets.selection_suspended)
        return 0;
    return g_app_assets.selection_requested || g_app_assets.selection_loading ||
        (g_app_assets.selection_pending_ready &&
         g_app_assets.pending_generation == g_app_assets.selection_generation);
}

int TitanAppAssets_DrawSelectedTile(
    unsigned int application_index,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
)
{
    TitanResult result;
    float top_height;

    if (!g_app_assets.active_tile ||
        g_app_assets.active_application_index != application_index ||
        application_index != g_app_assets.requested_application_index)
        return 0;

    top_height = height * 0.5f;
    result = TitanVideo_DrawAdaptiveImagePsm16(
        g_app_assets.active_tile,
        TITAN_APP_TILE_WIDTH,
        TITAN_APP_TILE_HEIGHT / 2u,
        TITAN_APP_TILE_WIDTH,
        x, y, width, top_height, alpha
    );
    if (result != TITAN_RESULT_OK)
        return 0;
    result = TitanVideo_DrawAdaptiveImagePsm16(
        g_app_assets.active_tile +
            TITAN_APP_TILE_WIDTH * (TITAN_APP_TILE_HEIGHT / 2u),
        TITAN_APP_TILE_WIDTH,
        TITAN_APP_TILE_HEIGHT / 2u,
        TITAN_APP_TILE_WIDTH,
        x, y + top_height, width, height - top_height, alpha
    );
    return result == TITAN_RESULT_OK;
}

int TitanAppAssets_DrawSelectedBackground(
    unsigned int application_index,
    float x,
    float y,
    float width,
    float height,
    unsigned char alpha
)
{
    if (!g_app_assets.active_background ||
        g_app_assets.active_application_index != application_index ||
        application_index != g_app_assets.requested_application_index)
        return 0;
    return TitanVideo_DrawMediaImage16(
        g_app_assets.active_background,
        g_app_assets.active_background_width,
        g_app_assets.active_background_height,
        g_app_assets.active_background_width,
        x, y, width, height, alpha
    ) == TITAN_RESULT_OK;
}

void TitanAppAssets_ReleaseSelectedBackground(void)
{
    if (!g_app_assets.initialized)
        return;

    free(g_app_assets.active_background);
    g_app_assets.active_background = 0;
    g_app_assets.active_background_width = 0u;
    g_app_assets.active_background_height = 0u;

    /* Invalidate any in-flight/pending live-area generation as well. Otherwise
     * a worker that was halfway through the 640x480 read could publish it again
     * after launch started and steal RAM back from TitanEYE. Worker-owned
     * buffers are freed by its stale-generation path; main-owned pending data
     * is discarded by CommitPendingAtlas(). */
    g_app_assets.selection_suspended = 1;
    g_app_assets.selection_requested = 0;
    g_app_assets.selection_generation++;
    if (g_app_assets.selection_generation == 0u)
        g_app_assets.selection_generation = 1u;
    if (g_app_assets.selection_loading || g_app_assets.selection_pending_ready)
        TitanStorageScheduler_NotifyBackgroundWork();
}

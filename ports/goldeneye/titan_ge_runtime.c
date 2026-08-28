#include "titan_ge_runtime.h"

#include <string.h>

typedef struct TitanGeRuntimeState
{
    TitanGePlatform platform;
    TitanGeRom rom;
    int attached;
    int rom_open;
    int audio_open;
    int failed;
    char app_root[512];
} TitanGeRuntimeState;

static TitanGeRuntimeState g_titan_ge;

int TitanGeRuntime_Attach(const TitanGameApiV1 *api)
{
    if (g_titan_ge.attached || !TitanGePlatform_Attach(&g_titan_ge.platform, api))
        return 0;
    g_titan_ge.attached = 1;
    return 1;
}

int TitanGeRuntime_SetAppRoot(const char *app_root)
{
    size_t length;
    if (!g_titan_ge.attached || app_root == 0 || app_root[0] == '\0') return 0;
    length = strlen(app_root);
    if (length >= sizeof(g_titan_ge.app_root)) return 0;
    memcpy(g_titan_ge.app_root, app_root, length + 1u);
    while (length > 1u &&
        (g_titan_ge.app_root[length - 1u] == '/' ||
         g_titan_ge.app_root[length - 1u] == '\\'))
        g_titan_ge.app_root[--length] = '\0';
    return 1;
}

int TitanGeRuntime_MakePath(
    const char *relative_path, char *output, unsigned int capacity
)
{
    size_t root_length, relative_length;
    if (output == 0 || capacity == 0u || relative_path == 0 ||
        relative_path[0] == '\0' || g_titan_ge.app_root[0] == '\0' ||
        relative_path[0] == '/' || relative_path[0] == '\\' ||
        strstr(relative_path, "..") != 0 || strchr(relative_path, ':') != 0)
        return 0;
    root_length = strlen(g_titan_ge.app_root);
    relative_length = strlen(relative_path);
    if (root_length + 1u + relative_length + 1u > capacity) return 0;
    memcpy(output, g_titan_ge.app_root, root_length);
    output[root_length] = '/';
    memcpy(output + root_length + 1u, relative_path, relative_length + 1u);
    return 1;
}

int TitanGeRuntime_OpenRom(const char *path)
{
    if (!g_titan_ge.attached || g_titan_ge.rom_open ||
        !TitanGeRom_Open(&g_titan_ge.rom, g_titan_ge.platform.api, path))
        return 0;
    g_titan_ge.rom_open = 1;
    return 1;
}

void TitanGeRuntime_Fail(void) { g_titan_ge.failed = 1; }
int TitanGeRuntime_HasFailed(void) { return g_titan_ge.failed; }

void TitanGeRuntime_Detach(void)
{
    TitanGeRuntime_AudioEnd();
    if (g_titan_ge.rom_open) TitanGeRom_Close(&g_titan_ge.rom);
    memset(&g_titan_ge, 0, sizeof(g_titan_ge));
}

TitanGePlatform *TitanGeRuntime_GetPlatform(void)
{ return g_titan_ge.attached ? &g_titan_ge.platform : 0; }

const TitanGameApiV1 *TitanGeRuntime_GetApi(void)
{ return g_titan_ge.attached ? g_titan_ge.platform.api : 0; }

int TitanGeRuntime_RomRead(
    void *context, unsigned int offset, void *destination, unsigned int bytes
)
{
    TitanGeRom *rom = context != 0 ? (TitanGeRom *)context : &g_titan_ge.rom;
    return TitanGeRom_Read(rom, offset, destination, bytes);
}

int TitanGeRuntime_AudioBegin(void)
{
    if (!g_titan_ge.attached) return 0;
    if (g_titan_ge.audio_open) return 1;
    if (!TitanGePlatform_BeginAudio(&g_titan_ge.platform)) return 0;
    g_titan_ge.audio_open = 1;
    return 1;
}

unsigned int TitanGeRuntime_AudioSubmit(
    const short *samples, unsigned int frames
)
{
    if (!g_titan_ge.audio_open || samples == 0 || frames == 0u) return 0u;
    return TitanGePlatform_SubmitPcm(&g_titan_ge.platform, samples, frames);
}

unsigned int TitanGeRuntime_AudioFreeFrames(void)
{
    const TitanGameApiV1 *api = TitanGeRuntime_GetApi();
    return g_titan_ge.audio_open && api != 0 ? api->audio_free_frames() : 0u;
}

void TitanGeRuntime_AudioEnd(void)
{
    if (!g_titan_ge.audio_open) return;
    TitanGePlatform_EndAudio(&g_titan_ge.platform);
    g_titan_ge.audio_open = 0;
}


#define TITAN_GE_TRACE_PATH "data/titaneye_boot.trace"

static void TitanGeRuntime_TraceWrite(const char *stage, uint32_t open_flags)
{
    const TitanGameApiV1 *api = TitanGeRuntime_GetApi();
    int descriptor;
    size_t length;
    static const char newline = '\n';

    if (api == 0 || stage == 0 || stage[0] == '\0' ||
        api->storage_open == 0 || api->storage_close == 0 ||
        api->storage_write == 0 || api->storage_seek == 0)
        return;

    if (api->storage_mkdir != 0)
        (void)api->storage_mkdir("data");
    descriptor = api->storage_open(TITAN_GE_TRACE_PATH, open_flags);
    if (descriptor < 0) return;
    if ((open_flags & TITAN_GAME_STORAGE_TRUNCATE) == 0u)
        (void)api->storage_seek(descriptor, 0, TITAN_GAME_STORAGE_SEEK_END);
    length = strlen(stage);
    if (length > 0u)
        (void)api->storage_write(descriptor, stage, (uint32_t)length);
    (void)api->storage_write(descriptor, &newline, 1u);
    (void)api->storage_close(descriptor);
}

void TitanGeRuntime_TraceReset(void)
{
    TitanGeRuntime_TraceWrite(
        "P111_TRACE_BEGIN",
        TITAN_GAME_STORAGE_WRITE | TITAN_GAME_STORAGE_CREATE |
        TITAN_GAME_STORAGE_TRUNCATE
    );
}

void TitanGeRuntime_Trace(const char *stage)
{
    TitanGeRuntime_TraceWrite(
        stage,
        TITAN_GAME_STORAGE_WRITE | TITAN_GAME_STORAGE_CREATE
    );
}

static char TitanGeRuntime_HexDigit(unsigned int value)
{
    value &= 15u;
    return value < 10u ? (char)('0' + value) : (char)('A' + (value - 10u));
}

static char *TitanGeRuntime_AppendHex32(char *cursor, unsigned int value)
{
    int shift;
    for (shift = 28; shift >= 0; shift -= 4)
        *cursor++ = TitanGeRuntime_HexDigit(value >> (unsigned int)shift);
    return cursor;
}

static char *TitanGeRuntime_AppendLiteral(char *cursor, const char *text)
{
    while (*text != '\0') *cursor++ = *text++;
    return cursor;
}

void TitanGeRuntime_TraceHeap(const char *stage, unsigned int request_bytes)
{
    const TitanGameApiV1 *api = TitanGeRuntime_GetApi();
    char line[160];
    char *cursor = line;
    unsigned int used = 0xffffffffu;
    unsigned int limit = 0xffffffffu;

    if (stage == 0 || stage[0] == '\0') return;
    if (api != 0) {
        if (api->heap_used != 0) used = api->heap_used();
        if (api->heap_limit != 0) limit = api->heap_limit();
    }

    cursor = TitanGeRuntime_AppendLiteral(cursor, stage);
    cursor = TitanGeRuntime_AppendLiteral(cursor, " used=0x");
    cursor = TitanGeRuntime_AppendHex32(cursor, used);
    cursor = TitanGeRuntime_AppendLiteral(cursor, " limit=0x");
    cursor = TitanGeRuntime_AppendHex32(cursor, limit);
    cursor = TitanGeRuntime_AppendLiteral(cursor, " req=0x");
    cursor = TitanGeRuntime_AppendHex32(cursor, request_bytes);
    *cursor = '\0';

    TitanGeRuntime_Trace(line);
}

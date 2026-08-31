#include "titan_ge_runtime.h"

#include <string.h>

#define TITAN_GE_TRACE_PATH "data/titaneye_boot.trace"
#define TITAN_GE_TRACE_BUFFER_BYTES (32u * 1024u)
#define TITAN_GE_FAILURE_STAGE_BYTES 64u

typedef struct TitanGeRuntimeState
{
    TitanGePlatform platform;
    TitanGeRom rom;
    int attached;
    int rom_open;
    int audio_open;
    int failed;
    char failure_stage[TITAN_GE_FAILURE_STAGE_BYTES];
    char app_root[512];
    char trace_buffer[TITAN_GE_TRACE_BUFFER_BYTES];
    uint32_t trace_used;
    uint32_t trace_dropped;
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

void TitanGeRuntime_Fail(void)
{
    if (!g_titan_ge.failed)
        TitanGeRuntime_Trace("E_RUNTIME_FAILED");
    g_titan_ge.failed = 1;
    TitanGeRuntime_TraceFlush();
}
int TitanGeRuntime_HasFailed(void) { return g_titan_ge.failed; }
const char *TitanGeRuntime_GetFailureStage(void)
{
    if (!g_titan_ge.failed) return 0;
    return g_titan_ge.failure_stage[0] != '\0'
        ? g_titan_ge.failure_stage : "E_RUNTIME_FAILED";
}

void TitanGeRuntime_Detach(void)
{
    TitanGeRuntime_TraceFlush();
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

static void TitanGeRuntime_TraceAppendBytes(const char *bytes, size_t length)
{
    if (bytes == 0 || length == 0u)
        return;
    if (length > sizeof(g_titan_ge.trace_buffer) - g_titan_ge.trace_used)
    {
        g_titan_ge.trace_dropped++;
        return;
    }
    memcpy(g_titan_ge.trace_buffer + g_titan_ge.trace_used, bytes, length);
    g_titan_ge.trace_used += (uint32_t)length;
}

void TitanGeRuntime_TraceReset(void)
{
    const TitanGameApiV1 *api = TitanGeRuntime_GetApi();
    int descriptor;

    g_titan_ge.trace_used = 0u;
    g_titan_ge.trace_dropped = 0u;
    g_titan_ge.failed = 0;
    g_titan_ge.failure_stage[0] = '\0';
    if (api != 0 && api->storage_open != 0 && api->storage_close != 0)
    {
        if (api->storage_mkdir != 0)
            (void)api->storage_mkdir("data");
        descriptor = api->storage_open(
            TITAN_GE_TRACE_PATH,
            TITAN_GAME_STORAGE_WRITE | TITAN_GAME_STORAGE_CREATE |
            TITAN_GAME_STORAGE_TRUNCATE
        );
        if (descriptor >= 0)
            (void)api->storage_close(descriptor);
    }
    TitanGeRuntime_Trace("P111_TRACE_BEGIN");
}

void TitanGeRuntime_Trace(const char *stage)
{
    static const char newline = '\n';
    size_t length;
    size_t failure_length;

    if (stage == 0 || stage[0] == '\0') return;
    length = strlen(stage);
    if (stage[0] == 'E' && stage[1] == '_' &&
        strcmp(stage, "E_RUNTIME_FAILED") != 0 &&
        g_titan_ge.failure_stage[0] == '\0')
    {
        failure_length = length;
        if (failure_length >= sizeof(g_titan_ge.failure_stage))
            failure_length = sizeof(g_titan_ge.failure_stage) - 1u;
        memcpy(g_titan_ge.failure_stage, stage, failure_length);
        g_titan_ge.failure_stage[failure_length] = '\0';
    }
    if (length + 1u > sizeof(g_titan_ge.trace_buffer) - g_titan_ge.trace_used)
    {
        g_titan_ge.trace_dropped++;
        return;
    }
    TitanGeRuntime_TraceAppendBytes(stage, length);
    TitanGeRuntime_TraceAppendBytes(&newline, 1u);
}

void TitanGeRuntime_TraceFlush(void)
{
    const TitanGameApiV1 *api = TitanGeRuntime_GetApi();
    static const char dropped[] = "W_TRACE_BUFFER_FULL\n";
    uint32_t written = 0u;
    int descriptor;

    if (api == 0 || g_titan_ge.trace_used == 0u ||
        api->storage_open == 0 || api->storage_close == 0 ||
        api->storage_write == 0 || api->storage_seek == 0)
        return;

    if (g_titan_ge.trace_dropped != 0u &&
        sizeof(dropped) - 1u <=
            sizeof(g_titan_ge.trace_buffer) - g_titan_ge.trace_used)
    {
        TitanGeRuntime_TraceAppendBytes(dropped, sizeof(dropped) - 1u);
        g_titan_ge.trace_dropped = 0u;
    }

    descriptor = api->storage_open(
        TITAN_GE_TRACE_PATH,
        TITAN_GAME_STORAGE_WRITE | TITAN_GAME_STORAGE_CREATE
    );
    if (descriptor < 0) return;
    if (api->storage_seek(descriptor, 0, TITAN_GAME_STORAGE_SEEK_END) < 0)
    {
        (void)api->storage_close(descriptor);
        return;
    }
    while (written < g_titan_ge.trace_used)
    {
        int64_t result = api->storage_write(
            descriptor,
            g_titan_ge.trace_buffer + written,
            g_titan_ge.trace_used - written
        );
        if (result <= 0 || result > (int64_t)(g_titan_ge.trace_used - written))
            break;
        written += (uint32_t)result;
    }
    (void)api->storage_close(descriptor);
    if (written == g_titan_ge.trace_used)
    {
        g_titan_ge.trace_used = 0u;
        g_titan_ge.trace_dropped = 0u;
    }
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

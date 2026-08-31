#include "../../ports/goldeneye/titan_ge_runtime.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static char g_trace_file[128u * 1024u];
static uint32_t g_trace_size;
static int g_open_calls;
static int g_close_calls;

static int storage_open(const char *path, uint32_t flags)
{
    assert(strcmp(path, "data/titaneye_boot.trace") == 0);
    g_open_calls++;
    if ((flags & TITAN_GAME_STORAGE_TRUNCATE) != 0u) g_trace_size = 0u;
    return 7;
}

static int storage_close(int descriptor)
{
    assert(descriptor == 7);
    g_close_calls++;
    return 1;
}

static int64_t storage_write(int descriptor, const void *bytes, uint32_t count)
{
    assert(descriptor == 7);
    assert(g_trace_size + count <= sizeof(g_trace_file));
    memcpy(g_trace_file + g_trace_size, bytes, count);
    g_trace_size += count;
    return count;
}

static int64_t storage_seek(int descriptor, int64_t offset, int origin)
{
    assert(descriptor == 7);
    assert(offset == 0);
    assert(origin == TITAN_GAME_STORAGE_SEEK_END);
    return g_trace_size;
}

static int storage_mkdir(const char *path)
{
    assert(strcmp(path, "data") == 0);
    return 1;
}

int TitanGePlatform_Attach(TitanGePlatform *platform, const TitanGameApiV1 *api)
{
    memset(platform, 0, sizeof(*platform));
    platform->api = api;
    return 1;
}
int TitanGePlatform_BeginAudio(TitanGePlatform *platform)
{ (void)platform; return 1; }
unsigned int TitanGePlatform_SubmitPcm(
    TitanGePlatform *platform, const short *samples, unsigned int frames)
{ (void)platform; (void)samples; return frames; }
void TitanGePlatform_EndAudio(TitanGePlatform *platform) { (void)platform; }

int TitanGeRom_Open(
    TitanGeRom *rom, const TitanGameApiV1 *api, const char *path)
{ (void)rom; (void)api; (void)path; return 0; }
void TitanGeRom_Close(TitanGeRom *rom) { (void)rom; }
int TitanGeRom_Read(
    TitanGeRom *rom, unsigned int offset, void *destination,
    unsigned int bytes)
{ (void)rom; (void)offset; (void)destination; (void)bytes; return 0; }

int main(void)
{
    TitanGameApiV1 api;
    unsigned int index;

    memset(&api, 0, sizeof(api));
    api.abi_version = TITAN_GAME_API_VERSION;
    api.struct_size = sizeof(api);
    api.storage_open = storage_open;
    api.storage_close = storage_close;
    api.storage_write = storage_write;
    api.storage_seek = storage_seek;
    api.storage_mkdir = storage_mkdir;

    assert(TitanGeRuntime_Attach(&api));
    TitanGeRuntime_TraceReset();
    assert(g_open_calls == 1);
    assert(g_close_calls == 1);

    for (index = 0u; index < 100u; index++)
        TitanGeRuntime_Trace("FRAME_TELEMETRY");
    assert(g_open_calls == 1);
    assert(g_trace_size == 0u);

    TitanGeRuntime_TraceFlush();
    assert(g_open_calls == 2);
    assert(g_close_calls == 2);
    assert(g_trace_size > 100u * sizeof("FRAME_TELEMETRY"));
    assert(memcmp(g_trace_file, "P111_TRACE_BEGIN\n", 17u) == 0);

    TitanGeRuntime_Detach();
    assert(g_open_calls == 2);
    puts("titaneye-tgr-runtime: OK RAM trace batching");
    return 0;
}

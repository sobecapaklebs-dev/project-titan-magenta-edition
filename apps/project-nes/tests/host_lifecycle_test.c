#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../src/project_nes.c"

#define GUARD 32u
#define MAX_ALLOCS 8u

typedef struct GuardAlloc
{
    unsigned char *base;
    void *user;
    uint32_t size;
} GuardAlloc;

static GuardAlloc g_allocs[MAX_ALLOCS];
static unsigned int g_alloc_count;
static const uint8_t *g_rom;
static uint32_t g_rom_size;
static unsigned int g_draw_calls;
static unsigned int g_media_present_calls;
static unsigned int g_media_draw_calls;
static unsigned int g_audio_frames;
static unsigned int g_save_writes;
static unsigned int g_save_attempts;
static unsigned int g_save_failures;
static int g_fail_next_save_write;
static uint32_t g_save_size;
static uint8_t g_save_first;

static int guard_ok(const GuardAlloc *a)
{
    uint32_t i;
    if (a->base == 0) return 1;
    for (i = 0u; i < GUARD; i++)
    {
        if (a->base[i] != 0xa5u) return 0;
        if (a->base[GUARD + a->size + i] != 0x5au) return 0;
    }
    return 1;
}

static void *fake_alloc(uint32_t size)
{
    unsigned int i;
    unsigned char *base;
    if (size == 0u) return 0;
    for (i = 0u; i < MAX_ALLOCS; i++) if (g_allocs[i].base == 0) break;
    if (i == MAX_ALLOCS) return 0;
    base = (unsigned char *)malloc((size_t)size + GUARD * 2u);
    if (base == 0) return 0;
    memset(base, 0xa5, GUARD);
    memset(base + GUARD, 0, size);
    memset(base + GUARD + size, 0x5a, GUARD);
    g_allocs[i].base = base;
    g_allocs[i].user = base + GUARD;
    g_allocs[i].size = size;
    g_alloc_count++;
    return g_allocs[i].user;
}

static void fake_release(void *pointer)
{
    unsigned int i;
    if (pointer == 0) return;
    for (i = 0u; i < MAX_ALLOCS; i++)
    {
        if (g_allocs[i].user == pointer)
        {
            if (!guard_ok(&g_allocs[i]))
            {
                fprintf(stderr, "guard corruption on allocation %u\n", i);
                exit(30);
            }
            free(g_allocs[i].base);
            memset(&g_allocs[i], 0, sizeof(g_allocs[i]));
            g_alloc_count--;
            return;
        }
    }
    fprintf(stderr, "release of unknown pointer\n");
    exit(31);
}

static void fake_request_exit(void) {}
static uint64_t fake_uptime(void) { return 1u; }
static void fake_draw_text(float x,float y,float s,const char*t,uint8_t r,uint8_t g,uint8_t b,uint8_t a)
{ (void)x;(void)y;(void)s;(void)t;(void)r;(void)g;(void)b;(void)a; g_draw_calls++; }
static void fake_draw_text_shadow(float x,float y,float s,const char*t,uint8_t r,uint8_t g,uint8_t b,uint8_t a)
{ fake_draw_text(x,y,s,t,r,g,b,a); }
static void fake_draw_rect(float x,float y,float w,float h,uint8_t r,uint8_t g,uint8_t b,uint8_t a)
{ (void)x;(void)y;(void)w;(void)h;(void)r;(void)g;(void)b;(void)a; g_draw_calls++; }
static void fake_draw_panel(float x,float y,float w,float h,uint8_t a)
{ (void)x;(void)y;(void)w;(void)h;(void)a; g_draw_calls++; }
static void fake_draw_progress(float x,float y,float w,float h,uint32_t p,uint8_t a)
{ (void)x;(void)y;(void)w;(void)h;(void)p;(void)a; g_draw_calls++; }
static int fake_notify(TitanAppNotificationKind k,const char*t,const char*m)
{ (void)k;(void)t;(void)m; return 1; }

static int fake_file_read(const char *path,uint32_t offset,void *buffer,uint32_t capacity)
{
    uint32_t remaining;
    uint32_t amount;
    if (strstr(path, "data/saves/") != 0) return 0;
    if (offset >= g_rom_size) return 0;
    remaining = g_rom_size - offset;
    amount = remaining < capacity ? remaining : capacity;
    memcpy(buffer, g_rom + offset, amount);
    return (int)amount;
}
static int fake_file_write(const char *path,const void *buffer,uint32_t size)
{
    if (strstr(path,"data/saves/") != 0)
    {
        const uint8_t *b=(const uint8_t*)buffer;
        g_save_attempts++;
        if (g_fail_next_save_write)
        {
            g_fail_next_save_write=0;
            g_save_failures++;
            return -1;
        }
        g_save_writes++; g_save_size=size; g_save_first=b!=0?b[0x123u]:0u;
    }
    return (int)size;
}
static int fake_file_exists(const char *path) { (void)path; return 0; }
static int fake_media_begin(void) { return 1; }
static uint32_t fake_media_submit(const int16_t *samples,uint32_t frames)
{
    uint32_t i;
    int nonzero = 0;
    for (i = 0u; i < frames * 2u; i++) if (samples[i] != 0) nonzero = 1;
    if (nonzero) g_audio_frames += frames;
    return frames;
}
static uint32_t fake_media_free(void) { return 4096u; }
static void fake_media_end(void) {}
static int fake_media_present(const uint16_t *pixels,uint32_t width,uint32_t height,uint32_t stride)
{
    uint32_t i;
    unsigned int nonzero = 0u;
    if (pixels == 0 || width != PN_MEDIA_W || height != PN_MEDIA_H || stride != PN_MEDIA_W)
        return 0;
    for (i = 0u; i < width * height; i++) if (pixels[i] != 0u) nonzero++;
    if (nonzero == 0u) return 0;
    g_media_present_calls++;
    return 1;
}
static void fake_media_draw(float x,float y,float w,float h,uint8_t a)
{ (void)x;(void)y;(void)w;(void)h;(void)a; g_media_draw_calls++; }

static int load_rom_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *data;
    if (f == 0) return 0;
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0 || (unsigned long)size > 0xfffffffful) { fclose(f); return 0; }
    data = (uint8_t *)malloc((size_t)size);
    if (data == 0) { fclose(f); return 0; }
    if (fread(data, 1, (size_t)size, f) != (size_t)size) { free(data); fclose(f); return 0; }
    fclose(f);
    data[6] |= 0x02u;
    g_rom = data;
    g_rom_size = (uint32_t)size;
    return 1;
}

static int run_cycle(TitanAppApiV1 *api, unsigned int cycle)
{
    TitanAppInput input;
    unsigned int i;
    unsigned int writes_before=g_save_writes;
    if (titan_app_init(api, "mass:/TITAN/apps/PROJECTNES") != 0) return 40;
    if (g_alloc_count != 0u) return 41;
    titan_app_draw();
    if (g_draw_calls < 4u) return 42;

    memset(&input, 0, sizeof(input));
    input.connected = 1u;
    input.pressed = TITAN_APP_BUTTON_CROSS;
    input.held = TITAN_APP_BUTTON_CROSS;
    titan_app_update(&input, 16u);
    if (g_pn.screen != PN_SCREEN_BOOTING || g_alloc_count != 1u) return 43;

    memset(&input, 0, sizeof(input));
    input.connected = 1u;
    for (i = 0u; i < PN_BOOT_WARMUP_FRAMES; i++) titan_app_update(&input, 16u);
    if (g_pn.screen != PN_SCREEN_RUNNING || g_media_present_calls == 0u) return 44;
    for (i = 0u; i < 12u; i++) titan_app_update(&input, 17u);
    titan_app_draw();
    if (g_media_draw_calls == 0u) return 45;

    for (i = 0u; i < MAX_ALLOCS; i++) if (!guard_ok(&g_allocs[i])) return 46;
    pn_cpu_write(&g_pn.nes, 0x6123u, 0x5au);
    if (!g_pn.nes.prg_ram_dirty) return 47;

    if (cycle == 0u)
    {
        g_fail_next_save_write=1;
        memset(&input,0,sizeof(input)); input.connected=1u; input.pressed=TITAN_APP_BUTTON_TRIANGLE;
        titan_app_update(&input,16u);
        if (g_pn.screen != PN_SCREEN_PAUSED || !g_pn.nes.prg_ram_dirty) return 48;
        if (g_save_failures != 1u || g_fail_next_save_write) return 49;
        memset(&input,0,sizeof(input)); input.connected=1u; input.pressed=TITAN_APP_BUTTON_TRIANGLE;
        titan_app_update(&input,16u);
        if (g_pn.screen != PN_SCREEN_RUNNING) return 50;
    }

    titan_app_shutdown();
    if (g_alloc_count != 0u) return 51;
    if (g_save_writes != writes_before + 1u) return 52;
    if (g_save_size != 8192u || g_save_first != 0x5au) return 53;
    return 0;
}

int main(int argc, char **argv)
{
    TitanAppApiV1 api;
    unsigned int cycle;
    int rc;
    if (argc != 2 || !load_rom_file(argv[1])) return 2;
    memset(&api, 0, sizeof(api));
    api.abi_version = TITAN_APP_API_VERSION;
    api.struct_size = sizeof(api);
    api.request_exit = fake_request_exit;
    api.uptime_ms = fake_uptime;
    api.alloc = fake_alloc;
    api.release = fake_release;
    api.draw_text = fake_draw_text;
    api.draw_text_shadow = fake_draw_text_shadow;
    api.draw_rect = fake_draw_rect;
    api.draw_panel = fake_draw_panel;
    api.draw_progress = fake_draw_progress;
    api.notify = fake_notify;
    api.file_read = fake_file_read;
    api.file_write_data = fake_file_write;
    api.file_exists = fake_file_exists;
    api.media_begin_audio = fake_media_begin;
    api.media_submit_pcm = fake_media_submit;
    api.media_audio_free_frames = fake_media_free;
    api.media_end_audio = fake_media_end;
    api.media_present_rgb565 = fake_media_present;
    api.media_draw = fake_media_draw;

    for (cycle=0u; cycle<10u; cycle++)
    {
        rc=run_cycle(&api,cycle);
        if (rc != 0) { fprintf(stderr,"cycle %u failed rc=%d\n",cycle,rc); return rc; }
    }
    if (g_save_attempts != 11u || g_save_writes != 10u || g_save_failures != 1u) return 60;
    free((void *)g_rom);
    printf("project-nes-lifecycle: OK cycles=10 save_retry=1 media=%u audio=%u draw=%u\n",
        g_media_present_calls, g_audio_frames, g_draw_calls);
    return 0;
}

#include "../src/services/titan_game_patches.h"
#include "../src/services/titan_storage_io.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int TitanStorageIo_Open(const char *path, int flags)
{
    int oflags = 0;
    if ((flags & TITAN_STORAGE_IO_READ) && (flags & TITAN_STORAGE_IO_WRITE)) oflags |= O_RDWR;
    else if (flags & TITAN_STORAGE_IO_WRITE) oflags |= O_WRONLY;
    else oflags |= O_RDONLY;
    if (flags & TITAN_STORAGE_IO_CREATE) oflags |= O_CREAT;
    if (flags & TITAN_STORAGE_IO_TRUNCATE) oflags |= O_TRUNC;
    if (flags & TITAN_STORAGE_IO_EXCLUSIVE) oflags |= O_EXCL;
    return open(path, oflags, 0666);
}
int TitanStorageIo_Close(int fd) { return close(fd); }
int TitanStorageIo_ReadExact(int fd, void *buffer, size_t size)
{
    unsigned char *p = buffer;
    while (size) { ssize_t n = read(fd, p, size); if (n <= 0) return 0; p += n; size -= (size_t)n; }
    return 1;
}
int TitanStorageIo_WriteExact(int fd, const void *buffer, size_t size)
{
    const unsigned char *p = buffer;
    while (size) { ssize_t n = write(fd, p, size); if (n <= 0) return 0; p += n; size -= (size_t)n; }
    return 1;
}
int TitanStorageIo_Flush(int fd) { return fsync(fd); }
int TitanStorageIo_PathInfo64(const char *path, int *is_directory, TitanStorageIoSize *size)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (is_directory) *is_directory = S_ISDIR(st.st_mode) ? 1 : 0;
    if (size) *size = (TitanStorageIoSize)st.st_size;
    return 1;
}
int TitanStorageIo_CreateDirectory(const char *path)
{
    if (mkdir(path, 0777) == 0 || errno == EEXIST) return 0;
    return -1;
}
int TitanStorageIo_Remove(const char *path) { return unlink(path); }
int TitanStorageIo_ListDirectory(
    const char *path,
    unsigned int start_index,
    TitanStorageIoEntry *entries,
    unsigned int capacity
)
{
    DIR *dir;
    struct dirent *de;
    unsigned int logical = 0u, produced = 0u;
    if (path == NULL || entries == NULL || capacity == 0u) return -1;
    dir = opendir(path);
    if (dir == NULL) return -1;
    while ((de = readdir(dir)) != NULL && produced < capacity) {
        char full[512];
        struct stat st;
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (logical++ < start_index) continue;
        memset(&entries[produced], 0, sizeof(entries[produced]));
        strncpy(entries[produced].name, de->d_name, sizeof(entries[produced].name)-1u);
        snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
        if (stat(full, &st) == 0) {
            entries[produced].size = (TitanStorageIoSize)st.st_size;
            if (S_ISDIR(st.st_mode)) entries[produced].flags |= TITAN_STORAGE_IO_ENTRY_DIRECTORY;
        }
        produced++;
    }
    closedir(dir);
    return (int)produced;
}
int TitanStorageIo_AtomicReplace(const char *tmp, const char *target, const char *backup)
{
    int had_target = access(target, F_OK) == 0;
    if (backup) unlink(backup);
    if (had_target && backup && rename(target, backup) != 0) return 0;
    if (had_target && !backup && unlink(target) != 0) return 0;
    if (rename(tmp, target) != 0) {
        if (had_target && backup) rename(backup, target);
        return 0;
    }
    return 1;
}

static void mkdir_ok(const char *p) { assert(mkdir(p, 0777) == 0 || errno == EEXIST); }
static void write_text(const char *p, const char *text)
{
    FILE *f = fopen(p, "wb"); assert(f != NULL); assert(fwrite(text, 1, strlen(text), f) == strlen(text)); assert(fclose(f) == 0);
}

static char *read_all_text(const char *p)
{
    FILE *f = fopen(p, "rb");
    long n;
    char *b;
    assert(f != NULL);
    assert(fseek(f, 0, SEEK_END) == 0);
    n = ftell(f);
    assert(n >= 0);
    assert(fseek(f, 0, SEEK_SET) == 0);
    b = (char *)malloc((size_t)n + 1u);
    assert(b != NULL);
    assert(fread(b, 1u, (size_t)n, f) == (size_t)n);
    b[n] = '\0';
    assert(fclose(f) == 0);
    return b;
}

static void write_many_codes(const char *p, unsigned int count)
{
    FILE *f = fopen(p, "wb");
    unsigned int i;
    assert(f != NULL);
    assert(fputs("Too Many Codes\n", f) >= 0);
    for (i = 0u; i < count; i++)
        assert(fprintf(f, "2%07X %08X\n", i & 0x0fffffffu, i + 1u) > 0);
    assert(fclose(f) == 0);
}

static void write_many_hooks(const char *p, unsigned int count)
{
    FILE *f = fopen(p, "wb");
    unsigned int i;
    assert(f != NULL);
    assert(fputs("Hook Overflow\n", f) >= 0);
    for (i = 0u; i < count; i++)
        assert(fprintf(f, "9%07X %08X\n", i & 0x0fffffffu, 0x0C000000u + i) > 0);
    assert(fclose(f) == 0);
}

#pragma pack(push, 1)
typedef struct TestPayloadHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t pair_count;
    uint32_t checksum;
} TestPayloadHeader;
#pragma pack(pop)

int main(void)
{
    const char *id = "SLUS_209.46";
    TitanGamePatchEntryInfo info;
    TitanGamePatchLaunchState launch;
    TitanGamePatchResult result;
    TestPayloadHeader h;
    FILE *f;

    assert(system("rm -rf /tmp/project_titan_game_patches_host && mkdir -p /tmp/project_titan_game_patches_host") == 0);
    assert(chdir("/tmp/project_titan_game_patches_host") == 0);
    mkdir_ok("mass:");
    mkdir_ok("mass:/CHT");
    mkdir_ok("mass:/ENH");
    mkdir_ok("mass:/CFG");
    mkdir_ok("mass:/TITAN");

    /* Missing databases are normal; malformed existing paths are not. */
    TitanGamePatches_Initialize();
    assert(TitanGamePatches_Load("SCUS_999.97", TITAN_GAME_PATCH_CHEAT) == TITAN_GAME_PATCH_OK);
    assert(TitanGamePatches_GetEntryCount(TITAN_GAME_PATCH_CHEAT) == 0u);
    mkdir_ok("mass:/CHT/SCUS_999.98.cht");
    assert(TitanGamePatches_Load("SCUS_999.98", TITAN_GAME_PATCH_CHEAT) == TITAN_GAME_PATCH_INVALID_FILE);

    write_text("mass:/CHT/SLUS_209.46.cht",
        "Master Code\n90100000 0C040000\n\n"
        "Infinite Health\n20123456 00000063\n\n"
        "Infinite Ammo\n20123460 00000063\n");
    write_text("mass:/ENH/SLUS_209.46.enh",
        "# @auto\n"
        "Widescreen 16:9\n20101010 3C013F40\n\n"
        "# @experimental\n"
        "60 FPS\n20102020 00000001\n");

    TitanGamePatches_Initialize();
    assert(TitanGamePatches_Load(id, TITAN_GAME_PATCH_CHEAT) == TITAN_GAME_PATCH_OK);
    assert(TitanGamePatches_GetEntryCount(TITAN_GAME_PATCH_CHEAT) == 2u);
    assert(TitanGamePatches_GetEntry(TITAN_GAME_PATCH_CHEAT, 0u, &info));
    assert(strcmp(info.name, "Infinite Health") == 0 && !info.enabled);
    assert(TitanGamePatches_ToggleEntry(TITAN_GAME_PATCH_CHEAT, 0u) == TITAN_GAME_PATCH_OK);

    assert(TitanGamePatches_Load(id, TITAN_GAME_PATCH_ENHANCEMENT) == TITAN_GAME_PATCH_OK);
    assert(TitanGamePatches_GetEntryCount(TITAN_GAME_PATCH_ENHANCEMENT) == 2u);
    assert(TitanGamePatches_GetEntry(TITAN_GAME_PATCH_ENHANCEMENT, 0u, &info));
    assert(strcmp(info.name, "Widescreen 16:9") == 0 && info.enabled && info.automatic && !info.experimental);
    assert(TitanGamePatches_GetEntry(TITAN_GAME_PATCH_ENHANCEMENT, 1u, &info));
    assert(strcmp(info.name, "60 FPS") == 0 && !info.enabled && info.experimental);

    result = TitanGamePatches_PrepareLaunch(id, &launch);
    assert(result == TITAN_GAME_PATCH_OK);
    assert(launch.gameplay_active && launch.gameplay_count == 1u);
    assert(launch.enhancement_active && launch.enhancement_count == 1u);
    assert(launch.taunt && strcmp(launch.taunt, "Damage was too scary, huh?") == 0);
    assert(strstr(launch.payload_path, "mass:/TITAN/gamepatches/SLUS_209.46.tpch") != NULL);

    f = fopen(launch.payload_path, "rb"); assert(f != NULL);
    assert(fread(&h, sizeof(h), 1, f) == 1); fclose(f);
    assert(h.magic == 0x48435054u && h.version == 1u);
    assert(h.flags == 3u);
    assert(h.pair_count == 3u); /* master + health + widescreen */

    /* Golden UL consumes only its existing OPL CHT/CFG interface. Titan stages
     * an active-only CHT and per-game cheat keys, then must restore both
     * canonical files byte-for-byte after launch/return. */
    {
        const char *original_cht =
            "Master Code\n90100000 0C040000\n\n"
            "Infinite Health\n20123456 00000063\n\n"
            "Infinite Ammo\n20123460 00000063\n";
        const char *original_cfg = "$Compatibility=5\r\n$GSMSource=1\r\n$EnableGSM=1\r\n";
        char *staged_cht;
        char *staged_cfg;
        char *restored_cht;
        char *restored_cfg;

        write_text("mass:/CFG/SLUS_209.46.cfg", original_cfg);
        assert(TitanGamePatches_PrepareUlGoldenRuntime(id) == TITAN_GAME_PATCH_OK);
        staged_cht = read_all_text("mass:/CHT/SLUS_209.46.cht");
        staged_cfg = read_all_text("mass:/CFG/SLUS_209.46.cfg");
        assert(strstr(staged_cht, "90100000 0C040000") != NULL);
        assert(strstr(staged_cht, "20123456 00000063") != NULL);
        assert(strstr(staged_cht, "20101010 3C013F40") != NULL);
        assert(strstr(staged_cht, "20123460 00000063") == NULL);
        assert(strstr(staged_cht, "20102020 00000001") == NULL);
        assert(strstr(staged_cfg, "$Compatibility=5") != NULL);
        assert(strstr(staged_cfg, "$GSMSource=1") != NULL);
        assert(strstr(staged_cfg, "$CheatsSource=1") != NULL);
        assert(strstr(staged_cfg, "$EnableCheat=1") != NULL);
        assert(strstr(staged_cfg, "$CheatMode=0") != NULL);
        free(staged_cht);
        free(staged_cfg);

        TitanGamePatches_RecoverUlGoldenRuntime();
        TitanGamePatches_RecoverUlGoldenRuntime(); /* recovery is idempotent */
        restored_cht = read_all_text("mass:/CHT/SLUS_209.46.cht");
        restored_cfg = read_all_text("mass:/CFG/SLUS_209.46.cfg");
        assert(strcmp(restored_cht, original_cht) == 0);
        assert(strcmp(restored_cfg, original_cfg) == 0);
        free(restored_cht);
        free(restored_cfg);
        assert(access("mass:/TITAN/gamepatches/SLUS_209.46.ul.journal", F_OK) != 0);
    }

    /* If no canonical CHT existed before staging, recovery removes Titan's
     * temporary effective CHT instead of leaving a generated cheat DB behind. */
    write_text("mass:/ENH/SCUS_777.77.enh", "# @auto\nWidescreen\n20111110 3C013F40\n");
    assert(access("mass:/CHT/SCUS_777.77.cht", F_OK) != 0);
    assert(TitanGamePatches_PrepareUlGoldenRuntime("SCUS_777.77") == TITAN_GAME_PATCH_OK);
    assert(access("mass:/CHT/SCUS_777.77.cht", F_OK) == 0);
    TitanGamePatches_RecoverUlGoldenRuntime();
    assert(access("mass:/CHT/SCUS_777.77.cht", F_OK) != 0);
    assert(access("mass:/CFG/SCUS_777.77.cfg", F_OK) != 0);

    /* State survives a catalog reset/reload. */
    TitanGamePatches_Initialize();
    assert(TitanGamePatches_Load(id, TITAN_GAME_PATCH_CHEAT) == TITAN_GAME_PATCH_OK);
    assert(TitanGamePatches_GetEntry(TITAN_GAME_PATCH_CHEAT, 0u, &info) && info.enabled);
    assert(TitanGamePatches_Load(id, TITAN_GAME_PATCH_ENHANCEMENT) == TITAN_GAME_PATCH_OK);
    assert(TitanGamePatches_GetEntry(TITAN_GAME_PATCH_ENHANCEMENT, 0u, &info) && info.enabled);

    /* Disable the two active user entries; payload must disappear. */
    assert(TitanGamePatches_ToggleEntry(TITAN_GAME_PATCH_CHEAT, 0u) == TITAN_GAME_PATCH_OK);
    assert(TitanGamePatches_ToggleEntry(TITAN_GAME_PATCH_ENHANCEMENT, 0u) == TITAN_GAME_PATCH_OK);
    assert(TitanGamePatches_PrepareLaunch(id, &launch) == TITAN_GAME_PATCH_OK);
    assert(!launch.gameplay_active && !launch.enhancement_active);
    assert(access("mass:/TITAN/gamepatches/SLUS_209.46.tpch", F_OK) != 0);

    /* PS2RD has 250 regular code slots even though the transport has room for
     * more pairs when hooks are present. Fail closed before codelist overflow. */
    write_many_codes("mass:/CHT/SLUS_209.46.cht", 251u);
    TitanGamePatches_Initialize();
    assert(TitanGamePatches_Load(id, TITAN_GAME_PATCH_CHEAT) == TITAN_GAME_PATCH_OK);
    assert(TitanGamePatches_ToggleEntry(TITAN_GAME_PATCH_CHEAT, 0u) == TITAN_GAME_PATCH_OK);
    assert(TitanGamePatches_PrepareLaunch(id, &launch) == TITAN_GAME_PATCH_CODE_OVERFLOW);
    assert(access("mass:/TITAN/gamepatches/SLUS_209.46.tpch", F_OK) != 0);

    /* PS2RD also has a hard five-hook list. Six valid 0x9 hook codes must
     * fail before the runtime can write past hooklist[]. */
    write_many_hooks("mass:/CHT/SCUS_999.99.cht", 6u);
    TitanGamePatches_Initialize();
    assert(TitanGamePatches_Load("SCUS_999.99", TITAN_GAME_PATCH_CHEAT) == TITAN_GAME_PATCH_OK);
    assert(TitanGamePatches_ToggleEntry(TITAN_GAME_PATCH_CHEAT, 0u) == TITAN_GAME_PATCH_OK);
    assert(TitanGamePatches_PrepareLaunch("SCUS_999.99", &launch) == TITAN_GAME_PATCH_CODE_OVERFLOW);
    assert(access("mass:/TITAN/gamepatches/SCUS_999.99.tpch", F_OK) != 0);

    puts("titan-game-patches-host: OK CHT/ENH parse, missing/malformed paths, defaults, persistence, taunt, payload, PS2RD code+hook bounds, cleanup");
    return 0;
}

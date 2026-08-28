#include "titan_profile_snapshot.h"
#include "titan_profile_storage.h"
#include "titan_storage_paths.h"
#include "titan_user_profile.h"
#include "titan_vmc.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static off_t Test_FileSize(const char *path)
{
    struct stat info;

    assert(stat(path, &info) == 0);
    return info.st_size;
}


static unsigned int Test_ReadLe32(const unsigned char *value)
{
    return
        (unsigned int)value[0] |
        ((unsigned int)value[1] << 8u) |
        ((unsigned int)value[2] << 16u) |
        ((unsigned int)value[3] << 24u);
}

static void Test_ReadAt(
    const char *path,
    off_t offset,
    unsigned char *buffer,
    unsigned int size
)
{
    int descriptor;

    descriptor = open(path, O_RDONLY);
    assert(descriptor >= 0);
    assert(lseek(descriptor, offset, SEEK_SET) == offset);
    assert(read(descriptor, buffer, size) == (ssize_t)size);
    assert(close(descriptor) == 0);
}

static void Test_WriteLe32At(
    const char *path,
    off_t offset,
    unsigned int value
)
{
    unsigned char bytes[4];
    int descriptor;

    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8u) & 0xFFu);
    bytes[2] = (unsigned char)((value >> 16u) & 0xFFu);
    bytes[3] = (unsigned char)((value >> 24u) & 0xFFu);

    descriptor = open(path, O_WRONLY);
    assert(descriptor >= 0);
    assert(lseek(descriptor, offset, SEEK_SET) == offset);
    assert(write(descriptor, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes));
    assert(close(descriptor) == 0);
}

static void Test_WriteLe16(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)(value & 0xFFu);
    p[1] = (unsigned char)((value >> 8u) & 0xFFu);
}

static void Test_WriteLe32(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)(value & 0xFFu);
    p[1] = (unsigned char)((value >> 8u) & 0xFFu);
    p[2] = (unsigned char)((value >> 16u) & 0xFFu);
    p[3] = (unsigned char)((value >> 24u) & 0xFFu);
}

static void Test_WriteAt(
    const char *path,
    off_t offset,
    const unsigned char *buffer,
    unsigned int size
)
{
    int descriptor = open(path, O_WRONLY);
    assert(descriptor >= 0);
    assert(lseek(descriptor, offset, SEEK_SET) == offset);
    assert(write(descriptor, buffer, size) == (ssize_t)size);
    assert(close(descriptor) == 0);
}

static void Test_BuildDirectoryEntry(
    unsigned char *entry,
    unsigned int mode,
    unsigned int length,
    unsigned int cluster,
    const char *name
)
{
    size_t n = strlen(name);
    assert(n < 32u);
    memset(entry, 0, 512u);
    Test_WriteLe16(entry + 0x00u, mode);
    Test_WriteLe32(entry + 0x04u, length);
    Test_WriteLe32(entry + 0x10u, cluster);
    memcpy(entry + 0x40u, name, n + 1u);
}

static void Test_VmcRootListing(
    const TitanUserProfile *profile,
    const char *path
)
{
    unsigned char cluster[1024];
    TitanVmcDirectoryEntry entry;
    const unsigned int exists = 0x8000u;
    const unsigned int directory = 0x0020u;
    const unsigned int read = 0x0001u;
    const unsigned int write = 0x0002u;

    /* Root has 4 entries: . and .. in allocation cluster 0, followed by two
     * real save entries in allocation cluster 1. */
    Test_WriteLe32At(path, (41 * 1024) + 0x04u, 4u);
    Test_WriteLe32At(path, (9 * 1024) + 0u, 0x80000001u);
    Test_WriteLe32At(path, (9 * 1024) + 4u, 0xFFFFFFFFu);

    memset(cluster, 0, sizeof(cluster));
    Test_BuildDirectoryEntry(
        cluster, exists | directory | read | write, 3u, 2u, "BASLUS-TESTSAVE");
    Test_BuildDirectoryEntry(
        cluster + 512u, exists | read, 12345u, 0u, "icon.sys");
    Test_WriteAt(path, 42 * 1024, cluster, sizeof(cluster));

    /* Subdirectory allocation 2 spans allocations 2->3 and contains one
     * visible file. Allocation 4 is the file payload head for CP33. */
    Test_WriteLe32At(path, (9 * 1024) + (2 * 4), 0x80000003u);
    Test_WriteLe32At(path, (9 * 1024) + (3 * 4), 0xFFFFFFFFu);
    Test_WriteLe32At(path, (9 * 1024) + (4 * 4), 0xFFFFFFFFu);
    memset(cluster, 0, sizeof(cluster));
    Test_BuildDirectoryEntry(cluster, exists | directory | read | write, 3u, 2u, ".");
    Test_BuildDirectoryEntry(cluster + 512u, exists | directory | read, 2u, 0u, "..");
    Test_WriteAt(path, 43 * 1024, cluster, sizeof(cluster));
    memset(cluster, 0, sizeof(cluster));
    Test_BuildDirectoryEntry(cluster, exists | read, 6u, 4u, "DATA.BIN");
    Test_WriteAt(path, 44 * 1024, cluster, sizeof(cluster));
    memset(cluster, 0, sizeof(cluster));
    memcpy(cluster, "ABCDEF", 6u);
    Test_WriteAt(path, 45 * 1024, cluster, sizeof(cluster));

    assert(TitanVmc_ListRoot(profile, 0u, &entry) == 1);
    assert(strcmp(entry.name, "BASLUS-TESTSAVE") == 0);
    assert(entry.size == 3u);
    assert((entry.flags & TITAN_VMC_DIRECTORY_ENTRY_DIRECTORY) != 0u);
    assert((entry.flags & TITAN_VMC_DIRECTORY_ENTRY_READABLE) != 0u);
    assert((entry.flags & TITAN_VMC_DIRECTORY_ENTRY_WRITABLE) != 0u);

    assert(TitanVmc_ListRoot(profile, 1u, &entry) == 1);
    assert(strcmp(entry.name, "icon.sys") == 0);
    assert(entry.size == 12345u);
    assert((entry.flags & TITAN_VMC_DIRECTORY_ENTRY_DIRECTORY) == 0u);
    assert(TitanVmc_ListRoot(profile, 2u, &entry) == 0);
    assert(TitanVmc_ListDirectory(profile, "BASLUS-TESTSAVE", 0u, &entry) == 1);
    assert(strcmp(entry.name, "DATA.BIN") == 0);
    assert(entry.size == 6u);
    assert(TitanVmc_ListDirectory(profile, "BASLUS-TESTSAVE", 1u, &entry) == 0);
    assert(TitanVmc_ListDirectory(profile, "../BASLUS-TESTSAVE", 0u, &entry) == -1);
    assert(TitanVmc_ListDirectory(profile, "/BASLUS-TESTSAVE", 0u, &entry) == -1);
    assert(TitanVmc_ListDirectory(profile, "icon.sys", 0u, &entry) == -1);
    {
        unsigned char data[8];
        memset(data, 0, sizeof(data));
        assert(TitanVmc_ReadFile(profile, "BASLUS-TESTSAVE/DATA.BIN", 0u, data, sizeof(data)) == 6);
        assert(memcmp(data, "ABCDEF", 6u) == 0);
        memset(data, 0, sizeof(data));
        assert(TitanVmc_ReadFile(profile, "BASLUS-TESTSAVE/DATA.BIN", 2u, data, 3u) == 3);
        assert(memcmp(data, "CDE", 3u) == 0);
        assert(TitanVmc_ReadFile(profile, "BASLUS-TESTSAVE/DATA.BIN", 6u, data, 3u) == 0);
        assert(TitanVmc_ReadFile(profile, "BASLUS-TESTSAVE", 0u, data, 3u) == -1);
        assert(TitanVmc_ReadFile(profile, "../DATA.BIN", 0u, data, 3u) == -1);
    }

    /* A root FAT link without the allocated bit is rejected rather than
     * traversed as raw image data. */
    Test_WriteLe32At(path, (9 * 1024) + 0u, 0x00000001u);
    assert(TitanVmc_ListRoot(profile, 0u, &entry) == -1);
    Test_WriteLe32At(path, (9 * 1024) + 0u, 0x80000001u);
}

static void Test_ValidateFormattedVmc(const char *path)
{
    unsigned char cluster[1024];

    Test_ReadAt(path, 0, cluster, sizeof(cluster));
    assert(memcmp(cluster, "Sony PS2 Memory Card Format ", 28u) == 0);
    assert(memcmp(cluster + 0x1Cu, "1.2.0.0", 7u) == 0);
    assert(Test_ReadLe32(cluster + 0x30u) == 8192u);
    assert(Test_ReadLe32(cluster + 0x34u) == 41u);
    assert(Test_ReadLe32(cluster + 0x38u) == 8135u);
    assert(Test_ReadLe32(cluster + 0x50u) == 8u);
    assert(cluster[0x150u] == 2u);
    assert(cluster[0x151u] == 0x2Bu);
    assert(Test_ReadLe32(cluster + 0x160u) == 1u);

    Test_ReadAt(path, 8 * 1024, cluster, sizeof(cluster));
    assert(Test_ReadLe32(cluster + 0u) == 9u);
    assert(Test_ReadLe32(cluster + (31u * 4u)) == 40u);

    Test_ReadAt(path, 9 * 1024, cluster, sizeof(cluster));
    assert(Test_ReadLe32(cluster + 0u) == 0xFFFFFFFFu);
    assert(Test_ReadLe32(cluster + 4u) == 0x7FFFFFFFu);

    Test_ReadAt(path, 41 * 1024, cluster, sizeof(cluster));
    assert(strcmp((const char *)(cluster + 0x40u), ".") == 0);
    assert(strcmp((const char *)(cluster + 512u + 0x40u), "..") == 0);
}

static void Test_ReadProfileData(unsigned char *buffer)
{
    int descriptor;

    descriptor = open(TITAN_PROFILE_DATA_PATH, O_RDONLY);
    assert(descriptor >= 0);
    assert(
        read(descriptor, buffer, TITAN_PROFILE_SNAPSHOT_SIZE) ==
        (ssize_t)TITAN_PROFILE_SNAPSHOT_SIZE
    );
    assert(close(descriptor) == 0);
}

static void Test_CopyFile(const char *source, const char *destination)
{
    unsigned char buffer[4096];
    int input;
    int output;
    ssize_t count;

    input = open(source, O_RDONLY);
    assert(input >= 0);
    output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    assert(output >= 0);

    while ((count = read(input, buffer, sizeof(buffer))) > 0)
    {
        assert(write(output, buffer, (size_t)count) == count);
    }
    assert(count == 0);
    assert(close(input) == 0);
    assert(close(output) == 0);
}

static void Test_NormalFlow(void)
{
    unsigned char snapshot[TITAN_PROFILE_SNAPSHOT_SIZE];
    TitanUserProfileState decoded;
    const TitanUserProfile *profile;
    unsigned int generation;
    char first_vmc[TITAN_USER_PROFILE_VMC_PATH_CAPACITY];
    char second_vmc[TITAN_USER_PROFILE_VMC_PATH_CAPACITY];
    char argument[128];

    assert(
        TitanProfileStorage_Initialize(0) ==
        TITAN_PROFILE_STORAGE_READY_LOADED
    );
    assert(
        Test_FileSize(TITAN_PROFILE_DATA_PATH) ==
        (off_t)TITAN_PROFILE_SNAPSHOT_SIZE
    );

    profile = TitanUserProfiles_GetActive();
    assert(profile != 0);
    strcpy(first_vmc, profile->vmc_path);

    /* Profile boot/save is metadata-only now: the 8 MiB card is created lazily
     * by explicit VMC work or launch, never before the first animated frames. */
    assert(TitanVmc_GetCachedStatus(profile) == TITAN_VMC_STATUS_CHECKING);
    assert(access(first_vmc, F_OK) != 0);
    assert(TitanVmc_Ensure(profile) == TITAN_VMC_RESULT_CREATED);
    assert(Test_FileSize(first_vmc) == (off_t)TITAN_VMC_SIZE_BYTES);
    assert(TitanVmc_GetCachedStatus(profile) == TITAN_VMC_STATUS_READY);
    assert(TitanVmc_GetStatus(profile) == TITAN_VMC_STATUS_READY);
    assert(
        TitanVmc_BuildNeutrinoArgument(
            profile,
            0,
            argument,
            sizeof(argument)
        )
    );
    assert(TitanVmc_GetStatus(profile) == TITAN_VMC_STATUS_READY);
    Test_ValidateFormattedVmc(first_vmc);
    Test_VmcRootListing(profile, first_vmc);

    /* A real PS2 card extends the root-directory FAT chain after enough save
     * entries are created. FAT[0] is therefore not required to stay EOF. */
    Test_WriteLe32At(first_vmc, (9 * 1024) + 0, 0x80000001u);
    Test_WriteLe32At(first_vmc, (9 * 1024) + 4, 0xFFFFFFFFu);
    assert(TitanVmc_Verify(profile) == TITAN_VMC_STATUS_READY);

    /* The same low-31-bit link without the allocation marker is not a valid
     * PS2 FAT chain entry. Ensure the deep verifier distinguishes the two. */
    Test_WriteLe32At(first_vmc, (9 * 1024) + 0, 0x00000001u);
    assert(TitanVmc_Verify(profile) == TITAN_VMC_STATUS_CORRUPT);
    Test_WriteLe32At(first_vmc, (9 * 1024) + 0, 0x80000001u);
    assert(TitanVmc_Verify(profile) == TITAN_VMC_STATUS_READY);

    assert(
        TitanVmc_BuildNeutrinoArgument(
            profile,
            0,
            argument,
            sizeof(argument)
        )
    );
    assert(strncmp(argument, "-mc0=", 5u) == 0);
    assert(strcmp(&argument[5], first_vmc) == 0);
    assert(
        TitanVmc_BuildNeutrinoArgument(
            profile,
            1,
            argument,
            sizeof(argument)
        )
    );
    assert(strncmp(argument, "-mc1=", 5u) == 0);
    assert(strcmp(&argument[5], first_vmc) == 0);

    Test_ReadProfileData(snapshot);
    assert(
        TitanProfileSnapshot_Decode(
            snapshot,
            sizeof(snapshot),
            &decoded,
            &generation
        ) == TITAN_PROFILE_SNAPSHOT_OK
    );
    assert(decoded.profile_count == 1u);
    assert(strcmp(decoded.profiles[0].vmc_path, first_vmc) == 0);

    assert(TitanUserProfiles_AddNamedWithAvatar("Titan Test", 1u));
    assert(
        TitanProfileStorage_Save() ==
        TITAN_PROFILE_STORAGE_SAVE_OK
    );
    profile = TitanUserProfiles_GetActive();
    assert(profile != 0);
    strcpy(second_vmc, profile->vmc_path);
    assert(strcmp(first_vmc, second_vmc) != 0);
    assert(access(second_vmc, F_OK) != 0);
    assert(TitanVmc_Ensure(profile) == TITAN_VMC_RESULT_CREATED);
    assert(Test_FileSize(second_vmc) == (off_t)TITAN_VMC_SIZE_BYTES);

    {
        int descriptor;
        unsigned char value;

        descriptor = open(second_vmc, O_WRONLY | O_TRUNC);
        assert(descriptor >= 0);
        value = 0x5Au;
        assert(write(descriptor, &value, 1u) == 1);
        assert(close(descriptor) == 0);
    }

    assert(TitanVmc_GetStatus(profile) == TITAN_VMC_STATUS_INVALID_SIZE);
    assert(TitanVmc_Ensure(profile) == TITAN_VMC_RESULT_INVALID);
    assert(TitanVmc_Recreate(profile) == TITAN_VMC_RESULT_FORMATTED);
    assert(Test_FileSize(second_vmc) == (off_t)TITAN_VMC_SIZE_BYTES);
    assert(TitanVmc_GetStatus(profile) == TITAN_VMC_STATUS_READY);
    Test_ValidateFormattedVmc(second_vmc);

    assert(TitanUserProfiles_RemoveActive());
    assert(
        TitanProfileStorage_Save() ==
        TITAN_PROFILE_STORAGE_SAVE_OK
    );
    assert(Test_FileSize(second_vmc) == (off_t)TITAN_VMC_SIZE_BYTES);

    Test_ReadProfileData(snapshot);
    assert(
        TitanProfileSnapshot_Decode(
            snapshot,
            sizeof(snapshot),
            &decoded,
            &generation
        ) == TITAN_PROFILE_SNAPSHOT_OK
    );
    assert(decoded.profile_count == 1u);
}

static void Test_InterruptedCommitRecovery(void)
{
    unsigned char snapshot[TITAN_PROFILE_SNAPSHOT_SIZE];
    TitanUserProfileState decoded;
    unsigned int generation;
    int descriptor;
    unsigned char damaged = 0x5Au;

    Test_CopyFile(
        TITAN_PROFILE_DATA_PATH,
        TITAN_PROFILE_DATA_BACKUP_PATH
    );
    descriptor = open(TITAN_PROFILE_DATA_PATH, O_WRONLY | O_TRUNC);
    assert(descriptor >= 0);
    assert(write(descriptor, &damaged, 1u) == 1);
    assert(close(descriptor) == 0);

    assert(
        TitanProfileStorage_Initialize(0) ==
        TITAN_PROFILE_STORAGE_READY_LOADED
    );
    Test_ReadProfileData(snapshot);
    assert(
        TitanProfileSnapshot_Decode(
            snapshot,
            sizeof(snapshot),
            &decoded,
            &generation
        ) == TITAN_PROFILE_SNAPSHOT_OK
    );
    assert(access(TITAN_PROFILE_DATA_BACKUP_PATH, F_OK) != 0);
}

static void Test_CorruptProtection(void)
{
    unsigned char before[TITAN_PROFILE_SNAPSHOT_SIZE];
    unsigned char after[TITAN_PROFILE_SNAPSHOT_SIZE];
    int descriptor;

    Test_ReadProfileData(before);
    before[120u] ^= 0xA5u;
    descriptor = open(TITAN_PROFILE_DATA_PATH, O_WRONLY | O_TRUNC);
    assert(descriptor >= 0);
    assert(
        write(descriptor, before, sizeof(before)) ==
        (ssize_t)sizeof(before)
    );
    assert(close(descriptor) == 0);

    assert(
        TitanProfileStorage_Initialize(0) ==
        TITAN_PROFILE_STORAGE_CORRUPT
    );
    assert(TitanUserProfiles_AddNamedWithAvatar("Protected", 2u));
    assert(
        TitanProfileStorage_Save() ==
        TITAN_PROFILE_STORAGE_SAVE_PROTECTED
    );
    Test_ReadProfileData(after);
    assert(memcmp(before, after, sizeof(before)) == 0);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "recover") == 0)
    {
        Test_InterruptedCommitRecovery();
    }
    else if (argc == 2 && strcmp(argv[1], "corrupt") == 0)
    {
        Test_CorruptProtection();
    }
    else
    {
        Test_NormalFlow();
    }

    puts("profile storage + VMC tests passed");
    return 0;
}

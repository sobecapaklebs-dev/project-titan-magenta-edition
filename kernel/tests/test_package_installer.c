#include "titan_applications.h"
#include "titan_package_installer.h"
#include "titan_storage_io.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_ROOT "/tmp/project_titan_package_installer_test"
#define TEST_TITAN_ROOT TEST_ROOT "/TITAN"
#define TEST_PACKAGE_ROOT TEST_TITAN_ROOT "/packages"

#define PACKAGE_HEADER_SIZE 256u
#define PACKAGE_ENTRY_SIZE 144u
#define PACKAGE_PAYLOAD_OFFSET (PACKAGE_HEADER_SIZE + (2u * PACKAGE_ENTRY_SIZE))

static void write_le16(unsigned char *destination, unsigned int value)
{
    destination[0] = (unsigned char)(value & 0xffu);
    destination[1] = (unsigned char)((value >> 8u) & 0xffu);
}

static void write_le32(unsigned char *destination, unsigned int value)
{
    destination[0] = (unsigned char)(value & 0xffu);
    destination[1] = (unsigned char)((value >> 8u) & 0xffu);
    destination[2] = (unsigned char)((value >> 16u) & 0xffu);
    destination[3] = (unsigned char)((value >> 24u) & 0xffu);
}

static uint32_t crc32_update(uint32_t checksum, const void *data, unsigned int size)
{
    const unsigned char *bytes;
    unsigned int index;

    bytes = (const unsigned char *)data;
    for (index = 0u; index < size; index++)
    {
        unsigned int bit;

        checksum ^= bytes[index];
        for (bit = 0u; bit < 8u; bit++)
        {
            checksum = (checksum >> 1u) ^
                (0xEDB88320u & (0u - (checksum & 1u)));
        }
    }
    return checksum;
}

static uint32_t crc32(const void *data, unsigned int size)
{
    return crc32_update(0xFFFFFFFFu, data, size) ^ 0xFFFFFFFFu;
}

static void create_mips_elf(unsigned char image[0x104])
{
    memset(image, 0, 0x104u);
    image[0] = 0x7fu;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    image[4] = 1u;
    image[5] = 1u;
    image[6] = 1u;
    write_le16(&image[16], 2u);
    write_le16(&image[18], 8u);
    write_le32(&image[20], 1u);
    write_le32(&image[24], 0x00100000u);
    write_le32(&image[28], 52u);
    write_le16(&image[40], 52u);
    write_le16(&image[42], 32u);
    write_le16(&image[44], 1u);
    write_le32(&image[52], 1u);
    write_le32(&image[56], 0x100u);
    write_le32(&image[60], 0x00100000u);
    write_le32(&image[64], 0x00100000u);
    write_le32(&image[68], 4u);
    write_le32(&image[72], 4u);
    write_le32(&image[76], 5u);
    write_le32(&image[80], 0x100u);
}

static void put_text(unsigned char *destination, unsigned int capacity, const char *text)
{
    size_t length;

    length = strlen(text);
    assert(length < capacity);
    memcpy(destination, text, length);
}

static void write_entry(
    unsigned char entry[PACKAGE_ENTRY_SIZE],
    const char *path,
    unsigned int offset,
    const unsigned char *payload,
    unsigned int size,
    int corrupt_checksum
)
{
    unsigned int checksum;

    memset(entry, 0, PACKAGE_ENTRY_SIZE);
    put_text(entry, 128u, path);
    write_le32(entry + 128u, offset);
    write_le32(entry + 132u, size);
    checksum = crc32(payload, size);
    write_le32(entry + 136u, corrupt_checksum ? checksum ^ 1u : checksum);
}

static void write_package(
    const char *filename,
    const char *title_id,
    const char *title,
    int corrupt_checksum,
    int unsafe_path
)
{
    unsigned char header[PACKAGE_HEADER_SIZE];
    unsigned char table[2u * PACKAGE_ENTRY_SIZE];
    unsigned char elf[0x104u];
    static const unsigned char data[] = "titan\n";
    unsigned int package_size;
    uint32_t header_crc;
    FILE *package;

    create_mips_elf(elf);
    write_entry(
        table,
        unsafe_path ? "../demo.elf" : "demo.elf",
        0u,
        elf,
        sizeof(elf),
        corrupt_checksum
    );
    write_entry(
        table + PACKAGE_ENTRY_SIZE,
        "data/config.txt",
        sizeof(elf),
        data,
        sizeof(data) - 1u,
        0
    );

    package_size = PACKAGE_PAYLOAD_OFFSET + sizeof(elf) + sizeof(data) - 1u;
    memset(header, 0, sizeof(header));
    memcpy(header, "TPKG", 4u);
    write_le32(header + 4u, 1u);
    write_le32(header + 8u, PACKAGE_HEADER_SIZE);
    write_le32(header + 12u, PACKAGE_ENTRY_SIZE);
    write_le32(header + 16u, 2u);
    write_le32(header + 20u, PACKAGE_HEADER_SIZE);
    write_le32(header + 24u, PACKAGE_PAYLOAD_OFFSET);
    write_le32(header + 28u, package_size);
    write_le32(header + 32u, crc32(table, sizeof(table)));
    put_text(header + 40u, 16u, title_id);
    put_text(header + 56u, 80u, title);
    put_text(header + 136u, 16u, "1.0.0");
    put_text(header + 152u, 104u, "demo.elf");
    header_crc = crc32(header, sizeof(header));
    write_le32(header + 36u, header_crc);

    package = fopen(filename, "wb");
    assert(package != 0);
    assert(fwrite(header, 1u, sizeof(header), package) == sizeof(header));
    assert(fwrite(table, 1u, sizeof(table), package) == sizeof(table));
    assert(fwrite(elf, 1u, sizeof(elf), package) == sizeof(elf));
    assert(fwrite(data, 1u, sizeof(data) - 1u, package) == sizeof(data) - 1u);
    assert(fclose(package) == 0);
}

static void write_package_v2(const char *filename)
{
    enum { V2_COUNT = 2u };
    unsigned char header[PACKAGE_HEADER_SIZE];
    unsigned char table[V2_COUNT * PACKAGE_ENTRY_SIZE];
    unsigned char elf[0x104u];
    unsigned char icon[64u * 64u * 2u];
    unsigned int payload_offset;
    unsigned int package_size;
    uint32_t header_crc;
    FILE *package;

    create_mips_elf(elf);
    memset(icon, 0x5au, sizeof(icon));
    write_entry(table, "demo.elf", 0u, elf, sizeof(elf), 0);
    write_entry(
        table + PACKAGE_ENTRY_SIZE,
        "icon.rgb565",
        sizeof(elf),
        icon,
        sizeof(icon),
        0
    );

    payload_offset = PACKAGE_HEADER_SIZE + sizeof(table);
    package_size = payload_offset + sizeof(elf) + sizeof(icon);
    memset(header, 0, sizeof(header));
    memcpy(header, "TPKG", 4u);
    write_le32(header + 4u, 2u);
    write_le32(header + 8u, PACKAGE_HEADER_SIZE);
    write_le32(header + 12u, PACKAGE_ENTRY_SIZE);
    write_le32(header + 16u, V2_COUNT);
    write_le32(header + 20u, PACKAGE_HEADER_SIZE);
    write_le32(header + 24u, payload_offset);
    write_le32(header + 28u, package_size);
    write_le32(header + 32u, crc32(table, sizeof(table)));
    put_text(header + 40u, 16u, "V2APP01");
    put_text(header + 56u, 80u, "TPKG v2 ELF App");
    put_text(header + 136u, 16u, "2.0.0");
    put_text(header + 152u, 96u, "demo.elf");
    write_le32(header + 248u, TITAN_PACKAGE_APP_EXTERNAL_ELF);
    write_le32(header + 252u, 0u);
    header_crc = crc32(header, sizeof(header));
    write_le32(header + 36u, header_crc);

    package = fopen(filename, "wb");
    assert(package != 0);
    assert(fwrite(header, 1u, sizeof(header), package) == sizeof(header));
    assert(fwrite(table, 1u, sizeof(table), package) == sizeof(table));
    assert(fwrite(elf, 1u, sizeof(elf), package) == sizeof(elf));
    assert(fwrite(icon, 1u, sizeof(icon), package) == sizeof(icon));
    assert(fclose(package) == 0);
}

static unsigned int find_package(const char *title_id)
{
    unsigned int index;

    for (index = 0u; index < TitanPackageInstaller_GetCount(); index++)
    {
        const TitanPackageInfo *package;

        package = TitanPackageInstaller_Get(index);
        if (package != 0 && strcmp(package->title_id, title_id) == 0)
        {
            return index;
        }
    }
    assert(!"package not found");
    return 0u;
}

static void finish_install(void)
{
    unsigned int steps;

    steps = 0u;
    while (TitanPackageInstaller_IsActive())
    {
        TitanPackageInstaller_Update(7u);
        steps++;
        assert(steps < 10000u);
    }
}

int main(void)
{
    const TitanApplication *application;
    const TitanPackageInfo *package;
    const TitanPackageInstallerStatus *status;
    FILE *manifest;
    char contents[512];
    size_t size;
    unsigned int index;

#if defined(TITAN_STORAGE_IO_TESTING)
    /* Force both package reads and writes to complete in tiny partial
     * transfers. Extraction must preserve state across scheduler turns rather
     * than relying on ReadExact/WriteExact inside one quantum. */
    TitanStorageIo_TestSetMaxTransfer(3u);
#endif

    assert(system("rm -rf " TEST_ROOT) == 0);
    assert(mkdir(TEST_ROOT, 0700) == 0);
    assert(mkdir(TEST_TITAN_ROOT, 0700) == 0);
    assert(mkdir(TEST_PACKAGE_ROOT, 0700) == 0);
    assert(mkdir(TEST_TITAN_ROOT "/apps", 0700) == 0);
    /* Simulate a staging generation left behind by a power loss/old frozen
     * installer. A new transaction must skip it, never recursively clean it
     * on the interactive preparation path. */
    assert(mkdir(TEST_TITAN_ROOT "/apps/.install-DEMO001-00000001", 0700) == 0);

    write_package(TEST_PACKAGE_ROOT "/demo.pkg", "DEMO001", "Demo USB App", 0, 0);
    write_package(TEST_PACKAGE_ROOT "/cancel.pkg", "CANCEL1", "Cancel App", 0, 0);
    write_package(TEST_PACKAGE_ROOT "/corrupt.pkg", "BADCRC1", "Bad CRC App", 1, 0);
    write_package(TEST_PACKAGE_ROOT "/unsafe.pkg", "UNSAFE1", "Unsafe App", 0, 1);
    write_package_v2(TEST_PACKAGE_ROOT "/v2-app.pkg");

    TitanApplications_Initialize();
    TitanPackageInstaller_Initialize();
    assert(TitanPackageInstaller_RefreshFromRoot(TEST_PACKAGE_ROOT) == 4u);

    index = find_package("DEMO001");
    assert(TitanPackageInstaller_BeginInstall(index) == TITAN_PACKAGE_INSTALLER_OK);
    status = TitanPackageInstaller_GetStatus();
    assert(status->phase == TITAN_PACKAGE_PHASE_PREPARING);
    TitanPackageInstaller_Update(7u);
    status = TitanPackageInstaller_GetStatus();
    assert(strncmp(status->message, "Preparando:", 10u) == 0);
    finish_install();
    status = TitanPackageInstaller_GetStatus();
    assert(status->result == TITAN_PACKAGE_INSTALLER_OK);
    assert(status->progress_percent == 100u);
    assert(access(TEST_TITAN_ROOT "/apps/DEMO001/demo.elf", F_OK) == 0);
    assert(access(TEST_TITAN_ROOT "/apps/DEMO001/data/config.txt", F_OK) == 0);
    assert(access(TEST_TITAN_ROOT "/apps/.install-DEMO001", F_OK) != 0);
    assert(access(TEST_TITAN_ROOT "/apps/.install-DEMO001-00000001", F_OK) == 0);
    assert(access(TEST_TITAN_ROOT "/apps/.install-DEMO001-00000002", F_OK) != 0);

    manifest = fopen(TEST_TITAN_ROOT "/apps/DEMO001/titan_app.ini", "rb");
    assert(manifest != 0);
    size = fread(contents, 1u, sizeof(contents) - 1u, manifest);
    assert(fclose(manifest) == 0);
    contents[size] = '\0';
    assert(strstr(contents, "title=Demo USB App\n") != 0);
    assert(strstr(contents, "executable=demo.elf\n") != 0);

    /* A second package must install in the same Package Installer session:
     * no Home visit, no global library rescan and no installer reinitialize. */
    assert(TitanApplications_GetCount() == 1u);
    index = find_package("V2APP01");
    assert(TitanPackageInstaller_BeginInstall(index) == TITAN_PACKAGE_INSTALLER_OK);
    finish_install();
    assert(TitanPackageInstaller_GetStatus()->result == TITAN_PACKAGE_INSTALLER_OK);
    assert(TitanApplications_GetCount() == 2u);

    TitanApplications_Initialize();
    assert(TitanApplications_RefreshFromRoot(TEST_TITAN_ROOT "/apps") == 2u);
    application = TitanApplications_Get(0u);
    assert(application != 0);
    assert(strcmp(application->title, "Demo USB App") == 0);
    assert(strcmp(application->path, TEST_TITAN_ROOT "/apps/DEMO001/demo.elf") == 0);

    TitanPackageInstaller_Initialize();
    assert(TitanPackageInstaller_RefreshFromRoot(TEST_PACKAGE_ROOT) == 4u);
    index = find_package("V2APP01");
    package = TitanPackageInstaller_Get(index);
    assert(package != 0);
    assert(strcmp(package->filename, "v2-app.pkg") == 0);
    assert(package->format_version == 2u);
    assert(package->app_type == TITAN_PACKAGE_APP_EXTERNAL_ELF);
    assert(package->installed);
    assert(access(TEST_TITAN_ROOT "/apps/V2APP01/icon.rgb565", F_OK) == 0);
    manifest = fopen(TEST_TITAN_ROOT "/apps/V2APP01/titan_app.ini", "rb");
    assert(manifest != 0);
    size = fread(contents, 1u, sizeof(contents) - 1u, manifest);
    assert(fclose(manifest) == 0);
    contents[size] = '\0';
    assert(strstr(contents, "format=2\n") != 0);
    assert(strstr(contents, "type=elf\n") != 0);
    assert(strstr(contents, "entrypoint=demo.elf\n") != 0);
    assert(strstr(contents, "icon=icon.rgb565\n") != 0);

    TitanPackageInstaller_Initialize();
    assert(TitanPackageInstaller_RefreshFromRoot(TEST_PACKAGE_ROOT) == 4u);
    package = TitanPackageInstaller_Get(find_package("DEMO001"));
    assert(package != 0 && package->installed);
    assert(
        TitanPackageInstaller_BeginInstall(find_package("DEMO001")) ==
        TITAN_PACKAGE_INSTALLER_ALREADY_INSTALLED
    );

    index = find_package("BADCRC1");
    assert(TitanPackageInstaller_BeginInstall(index) == TITAN_PACKAGE_INSTALLER_OK);
    finish_install();
    status = TitanPackageInstaller_GetStatus();
    assert(status->result == TITAN_PACKAGE_INSTALLER_CHECKSUM_FAILED);
    assert(access(TEST_TITAN_ROOT "/apps/BADCRC1", F_OK) != 0);
    assert(access(TEST_TITAN_ROOT "/apps/.install-BADCRC1", F_OK) != 0);

    index = find_package("CANCEL1");
    assert(TitanPackageInstaller_BeginInstall(index) == TITAN_PACKAGE_INSTALLER_OK);
    TitanPackageInstaller_Update(1u);
    TitanPackageInstaller_Cancel();
    status = TitanPackageInstaller_GetStatus();
    /* Cancel is intentionally request-only on the UI thread. The storage
     * worker owns descriptor close + staging cleanup. */
    assert(status->active != 0u);
    TitanPackageInstaller_Update(1u);
    status = TitanPackageInstaller_GetStatus();
    assert(status->result == TITAN_PACKAGE_INSTALLER_CANCELLED);
    assert(status->active == 0u);
    assert(access(TEST_TITAN_ROOT "/apps/CANCEL1", F_OK) != 0);
    assert(access(TEST_TITAN_ROOT "/apps/.install-CANCEL1", F_OK) != 0);

    puts("Package installer transaction tests passed");
    return 0;
}

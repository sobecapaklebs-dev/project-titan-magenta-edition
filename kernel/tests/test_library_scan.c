#include "titan_applications.h"
#include "titan_library_scan.h"
#include "titan_neutrino.h"
#include "titan_notifications.h"
#include "titan_user_profile.h"
#include "titan_vmc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SECTOR_SIZE 2048u

static TitanUserProfile g_test_profile;
static unsigned int g_activity_progress;
static unsigned int g_activity_updates;

static void write_le32(unsigned char *value, unsigned int number)
{
    value[0] = (unsigned char)number;
    value[1] = (unsigned char)(number >> 8);
    value[2] = (unsigned char)(number >> 16);
    value[3] = (unsigned char)(number >> 24);
}


static void write_le16(unsigned char *value, unsigned int number)
{
    value[0] = (unsigned char)number;
    value[1] = (unsigned char)(number >> 8);
}

static void create_elf(const char *path)
{
    unsigned char image[0x104u];
    FILE *file;

    memset(image, 0, sizeof(image));
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

    file = fopen(path, "wb");
    assert(file != 0);
    assert(fwrite(image, 1u, sizeof(image), file) == sizeof(image));
    fclose(file);
}

static void create_file(const char *path, const char *contents)
{
    FILE *file;

    file = fopen(path, "wb");
    assert(file != 0);
    assert(fwrite(contents, 1u, strlen(contents), file) == strlen(contents));
    fclose(file);
}

static void write_ul_entry(
    FILE *file,
    const char *title,
    const char *startup,
    unsigned char parts,
    unsigned char media
)
{
    unsigned char entry[64u];

    assert(file != 0);
    assert(strlen(title) <= 32u);
    assert(strlen(startup) == 11u);
    memset(entry, 0, sizeof(entry));
    memcpy(entry, title, strlen(title));
    memcpy(&entry[32], "ul.", 3u);
    memcpy(&entry[35], startup, 11u);
    entry[47] = parts;
    entry[48] = media;
    entry[53] = 0x08u;
    assert(fwrite(entry, 1u, sizeof(entry), file) == sizeof(entry));
}

static void create_ul_cfg(const char *path)
{
    FILE *file;

    file = fopen(path, "wb");
    assert(file != 0);
    write_ul_entry(file, "UL Alpha", "SLUS_100.01", 1u, 0x14u);
    write_ul_entry(file, "UL Beta", "SLES_200.02", 2u, 0x14u);
    /* A truncated final record must not discard the valid records before it. */
    assert(fputc(0x55, file) != EOF);
    fclose(file);
}

static void create_iso(const char *path)
{
    unsigned char image[22u * SECTOR_SIZE];
    unsigned char *pvd;
    unsigned char *root;
    unsigned char *cnf;
    const char *name;
    FILE *file;

    memset(image, 0, sizeof(image));
    pvd = &image[16u * SECTOR_SIZE];
    pvd[0] = 1u;
    memcpy(&pvd[1], "CD001", 5u);
    pvd[6] = 1u;
    pvd[156] = 34u;
    write_le32(&pvd[158], 20u);
    write_le32(&pvd[166], SECTOR_SIZE);

    name = "SYSTEM.CNF;1";
    root = &image[20u * SECTOR_SIZE];
    root[0] = (unsigned char)(33u + strlen(name));
    write_le32(&root[2], 21u);
    write_le32(&root[10], 32u);
    root[32] = (unsigned char)strlen(name);
    memcpy(&root[33], name, strlen(name));

    cnf = &image[21u * SECTOR_SIZE];
    memcpy(
        cnf,
        "BOOT2 = cdrom0:\\SLUS_390.00;1\r\n",
        strlen("BOOT2 = cdrom0:\\SLUS_390.00;1\r\n")
    );

    file = fopen(path, "wb");
    assert(file != 0);
    assert(fwrite(image, 1u, sizeof(image), file) == sizeof(image));
    fclose(file);
}

const char *TitanVideoOutput_GetGameGsmArgument(void)
{
    return 0;
}

const TitanUserProfile *TitanUserProfiles_GetActive(void)
{
    return &g_test_profile;
}

TitanVmcStatus TitanVmc_GetStatus(
    const TitanUserProfile *profile
)
{
    (void)profile;
    return TITAN_VMC_STATUS_READY;
}
TitanVmcStatus TitanVmc_GetCachedStatus(
    const TitanUserProfile *profile
)
{
    (void)profile;
    return TITAN_VMC_STATUS_READY;
}

TitanVmcStatus TitanVmc_RefreshStatus(
    const TitanUserProfile *profile
)
{
    (void)profile;
    return TITAN_VMC_STATUS_READY;
}

TitanVmcResult TitanVmc_Ensure(
    const TitanUserProfile *profile
)
{
    (void)profile;
    return TITAN_VMC_RESULT_OK;
}


int TitanVmc_BuildNeutrinoArgument(
    const TitanUserProfile *profile,
    int port,
    char *destination,
    unsigned int capacity
)
{
    (void)profile;
    (void)port;
    if (capacity < 16u)
    {
        return 0;
    }
    strcpy(destination, "-mc0=test.vmc");
    return 1;
}

void TitanNotifications_Initialize(void)
{
}

int TitanNotifications_Push(
    TitanNotificationKind kind,
    const char *title,
    const char *message
)
{
    (void)kind;
    (void)title;
    (void)message;
    return 1;
}

void TitanNotifications_BeginBackgroundActivity(
    const char *title,
    const char *message
)
{
    (void)title;
    (void)message;
    g_activity_progress = 0u;
}

void TitanNotifications_UpdateBackgroundActivity(
    const char *message,
    unsigned int progress_percent,
    unsigned int processed,
    unsigned int total,
    unsigned int games_found,
    unsigned int applications_found
)
{
    (void)message;
    (void)processed;
    (void)total;
    (void)games_found;
    (void)applications_found;
    assert(progress_percent <= 100u);
    g_activity_progress = progress_percent;
    g_activity_updates++;
}

void TitanNotifications_CompleteBackgroundActivity(
    const char *message,
    unsigned int games_found,
    unsigned int applications_found
)
{
    (void)message;
    assert(games_found == 3u);
    assert(applications_found == 1u);
    g_activity_progress = 100u;
}

void TitanNotifications_FailBackgroundActivity(const char *message)
{
    (void)message;
    assert(!"background scan failed");
}

void TitanNotifications_ClearBackgroundActivity(void)
{
}

const TitanBackgroundActivityStatus *TitanNotifications_GetBackgroundActivity(void)
{
    return 0;
}

const char *TitanNotifications_GetHeaderStatus(void)
{
    return "";
}

int main(void)
{
    const TitanLibraryScanStatus *status;
    const TitanNeutrinoGame *game;
    const TitanApplication *application;
    unsigned int guard;

    assert(system("rm -rf /tmp/project_titan_library_scan_test") == 0);
    assert(mkdir("/tmp/project_titan_library_scan_test", 0700) == 0);
    assert(chdir("/tmp/project_titan_library_scan_test") == 0);
    assert(mkdir("mass:", 0700) == 0);
    assert(mkdir("mass0:", 0700) == 0);
    assert(mkdir("mass:/DVD", 0700) == 0);
    assert(mkdir("mass:/APPS", 0700) == 0);
    assert(mkdir("mass:/TITAN", 0700) == 0);
    assert(mkdir("mass:/TITAN/apps", 0700) == 0);
    assert(mkdir("mass:/TITAN/apps/DEMO001", 0700) == 0);
    assert(mkdir("mass:/TITAN/apps/DEMO001/data", 0700) == 0);
    assert(mkdir("mass:/neutrino", 0700) == 0);

    create_iso("mass:/DVD/Titan Scan Test.iso");
    create_ul_cfg("mass0:/ul.cfg");
    create_elf("mass:/APPS/file_manager.elf");
    create_elf("mass:/TITAN/apps/DEMO001/demo.elf");
    create_elf("mass:/TITAN/apps/DEMO001/data/helper.elf");
    create_file(
        "mass:/TITAN/apps/DEMO001/titan_app.ini",
        "format=1\n"
        "title_id=DEMO001\n"
        "title=Demo USB App\n"
        "version=1.0.0\n"
        "executable=demo.elf\n"
    );
    create_file("mass:/neutrino/neutrino.elf", "ELF");

    memset(&g_test_profile, 0, sizeof(g_test_profile));
    TitanApplications_Initialize();
    TitanNeutrino_Initialize();
    TitanLibraryScan_Initialize();
    assert(TitanLibraryScan_GetStatus()->cache_loaded == 0u);
    TitanLibraryScan_Begin(0);

    guard = 0u;
    while (TitanLibraryScan_IsActive() && guard < 4096u)
    {
        TitanLibraryScan_Update(2u);
        guard++;
    }

    assert(guard < 4096u);
    status = TitanLibraryScan_GetStatus();
    assert(status->phase == TITAN_LIBRARY_SCAN_COMPLETE);
    assert(status->progress_percent == 100u);
    assert(g_activity_progress == 100u);
    assert(g_activity_updates > 0u);
    assert(TitanNeutrino_GetGameCount() == 3u);
    assert(TitanApplications_GetCount() == 1u);
    assert(TitanApplications_GetHomeTileCount() == 1u);

    game = TitanNeutrino_GetGame(0u);
    assert(game != 0);
    assert(strcmp(game->title, "Titan Scan Test") == 0);
    assert(strcmp(game->startup, "SLUS_390.00") == 0);
    game = TitanNeutrino_GetGame(1u);
    assert(game != 0);
    assert(game->format == TITAN_NEUTRINO_GAME_USBEXTREME);
    assert(strcmp(game->title, "UL Alpha") == 0);
    assert(strcmp(game->startup, "SLUS_100.01") == 0);
    assert(strcmp(game->path, "mass0:/ul.cfg") == 0);
    game = TitanNeutrino_GetGame(2u);
    assert(game != 0);
    assert(game->format == TITAN_NEUTRINO_GAME_USBEXTREME);
    assert(strcmp(game->title, "UL Beta") == 0);
    assert(strcmp(game->startup, "SLES_200.02") == 0);
    /* Automatic application discovery is intentionally canonical-only.
     * A legacy mass:/APPS ELF exists above to prove it is not probed. */
    application = TitanApplications_Get(0u);
    assert(application != 0);
    assert(strcmp(application->title, "Demo USB App") == 0);
    assert(strcmp(application->path, "mass:/TITAN/apps/DEMO001/demo.elf") == 0);
    application = TitanApplications_GetHomeTile(0u, 0);
    assert(application != 0);
    assert(strcmp(application->title, "Demo USB App") == 0);

    TitanApplications_Initialize();
    TitanNeutrino_Initialize();
    TitanLibraryScan_Initialize();
    assert(TitanLibraryScan_GetStatus()->cache_loaded == 1u);
    assert(TitanNeutrino_GetGameCount() == 3u);
    assert(TitanApplications_GetCount() == 1u);

    /* A normal verification scan must reuse startup metadata from the valid
     * persistent catalog instead of reopening the unchanged ISO metadata. */
    TitanLibraryScan_Begin(0);
    guard = 0u;
    while (TitanLibraryScan_IsActive() && guard < 4096u)
    {
        TitanLibraryScan_Update(2u);
        guard++;
    }
    assert(guard < 4096u);
    status = TitanLibraryScan_GetStatus();
    assert(status->phase == TITAN_LIBRARY_SCAN_COMPLETE);
    assert(status->metadata_reused >= 1u);
    assert(status->scan_steps > 0u);

    puts("Fast boot catalog + incremental metadata-reuse scanner tests passed");
    return 0;
}

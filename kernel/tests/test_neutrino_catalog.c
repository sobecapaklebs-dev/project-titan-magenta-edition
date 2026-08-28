#include "titan_neutrino.h"
#include "titan_user_profile.h"
#include "titan_vmc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SECTOR_SIZE 2048u

static void write_le32(unsigned char *value, unsigned int number)
{
    value[0] = (unsigned char)number;
    value[1] = (unsigned char)(number >> 8);
    value[2] = (unsigned char)(number >> 16);
    value[3] = (unsigned char)(number >> 24);
}

static void create_iso(const char *path)
{
    unsigned char image[22u * SECTOR_SIZE];
    unsigned char *pvd;
    unsigned char *root;
    unsigned char *cnf;
    const char *name = "SYSTEM.CNF;1";
    FILE *file;

    memset(image, 0, sizeof(image));
    pvd = &image[16u * SECTOR_SIZE];
    pvd[0] = 1u;
    memcpy(&pvd[1], "CD001", 5u);
    pvd[6] = 1u;
    pvd[156] = 34u;
    write_le32(&pvd[158], 20u);
    write_le32(&pvd[166], SECTOR_SIZE);

    root = &image[20u * SECTOR_SIZE];
    root[0] = (unsigned char)(33u + strlen(name));
    write_le32(&root[2], 21u);
    write_le32(
        &root[10],
        (unsigned int)strlen("BOOT2 = cdrom0:\\SLUS_999.99;1\r\nVER = 1.00\r\n")
    );
    root[32] = (unsigned char)strlen(name);
    memcpy(&root[33], name, strlen(name));

    cnf = &image[21u * SECTOR_SIZE];
    memcpy(
        cnf,
        "BOOT2 = cdrom0:\\SLUS_999.99;1\r\nVER = 1.00\r\n",
        strlen("BOOT2 = cdrom0:\\SLUS_999.99;1\r\nVER = 1.00\r\n")
    );

    file = fopen(path, "wb");
    assert(file != 0);
    assert(fwrite(image, 1u, sizeof(image), file) == sizeof(image));
    fclose(file);
}

static void create_ul_cfg(const char *path)
{
    unsigned char entry[64];
    FILE *file;

    memset(entry, 0, sizeof(entry));
    memcpy(entry, "USBExtreme Test", 15u);
    memcpy(&entry[32], "ul.", 3u);
    memcpy(&entry[35], "SCES_555.55", 11u);
    entry[47] = 2u;
    entry[48] = 0x14u;
    entry[53] = 0x08u;
    file = fopen(path, "wb");
    assert(file != 0);
    assert(fwrite(entry, 1u, sizeof(entry), file) == sizeof(entry));
    fclose(file);
}

static void create_ul_parts(void)
{
    FILE *file;

    file = fopen("mass:/ul.7D3BE5AE.SCES_555.55.00", "wb");
    assert(file != 0);
    fputs("part zero", file);
    fclose(file);

    file = fopen("mass:/ul.7D3BE5AE.SCES_555.55.01", "wb");
    assert(file != 0);
    fputs("part one", file);
    fclose(file);
}

static TitanUserProfile g_test_profile;
static const char *g_test_gsm_argument;

const char *TitanVideoOutput_GetGameGsmArgument(void)
{
    return g_test_gsm_argument;
}

const TitanUserProfile *TitanUserProfiles_GetActive(void)
{
    return &g_test_profile;
}

TitanVmcStatus TitanVmc_GetStatus(
    const TitanUserProfile *profile
)
{
    assert(profile == &g_test_profile);
    return TITAN_VMC_STATUS_READY;
}
TitanVmcStatus TitanVmc_GetCachedStatus(
    const TitanUserProfile *profile
)
{
    assert(profile == &g_test_profile);
    return TITAN_VMC_STATUS_READY;
}

TitanVmcStatus TitanVmc_RefreshStatus(
    const TitanUserProfile *profile
)
{
    assert(profile == &g_test_profile);
    return TITAN_VMC_STATUS_READY;
}

TitanVmcResult TitanVmc_Ensure(
    const TitanUserProfile *profile
)
{
    assert(profile == &g_test_profile);
    return TITAN_VMC_RESULT_OK;
}


int TitanVmc_BuildNeutrinoArgument(
    const TitanUserProfile *profile,
    int port,
    char *destination,
    unsigned int capacity
)
{
    const char *prefix;
    unsigned int required;

    assert(profile == &g_test_profile);
    assert(port == 0);
    prefix = "-mc0=";
    required = (unsigned int)(strlen(prefix) + strlen(profile->vmc_path) + 1u);
    if (required > capacity)
    {
        return 0;
    }
    strcpy(destination, prefix);
    strcat(destination, profile->vmc_path);
    return 1;
}

int main(void)
{
    const TitanNeutrinoGame *iso;
    const TitanNeutrinoGame *ul;
    char launch_command[768];

    assert(system("rm -rf /tmp/project_titan_neutrino_catalog_test") == 0);
    assert(mkdir("/tmp/project_titan_neutrino_catalog_test", 0700) == 0);
    assert(chdir("/tmp/project_titan_neutrino_catalog_test") == 0);
    assert(mkdir("mass:", 0700) == 0);
    assert(mkdir("mass:/DVD", 0700) == 0);
    assert(mkdir("mass:/neutrino", 0700) == 0);
    assert(mkdir("mass:/TITAN", 0700) == 0);
    {
        FILE *neutrino;
        FILE *opl_ul_loader;

        neutrino = fopen("mass:/neutrino/neutrino.elf", "wb");
        assert(neutrino != 0);
        fputs("test", neutrino);
        fclose(neutrino);

        opl_ul_loader = fopen("mass:/TITAN/titan_ul_loader.elf", "wb");
        assert(opl_ul_loader != 0);
        fputs("test", opl_ul_loader);
        fclose(opl_ul_loader);
    }
    memset(&g_test_profile, 0, sizeof(g_test_profile));
    g_test_profile.occupied = 1;
    strcpy(g_test_profile.vmc_path, "mass:/TITAN/users/user_0001/profile.vmc");
    g_test_gsm_argument = 0;
    create_iso("mass:/DVD/ISO Test.iso");
    create_ul_cfg("mass:/ul.cfg");
    create_ul_parts();

    TitanNeutrino_Initialize();
    assert(TitanNeutrino_Refresh() == 2u);
    iso = TitanNeutrino_GetGame(0u);
    ul = TitanNeutrino_GetGame(1u);
    assert(iso != 0 && iso->format == TITAN_NEUTRINO_GAME_ISO);
    assert(strcmp(iso->title, "ISO Test") == 0);
    assert(strcmp(iso->startup, "SLUS_999.99") == 0);
    assert(strcmp(iso->version, "1.00") == 0);
    assert(ul != 0 && ul->format == TITAN_NEUTRINO_GAME_USBEXTREME);
    assert(strcmp(ul->title, "USBExtreme Test") == 0);
    assert(strcmp(ul->startup, "SCES_555.55") == 0);
    assert(ul->version[0] == '\0');
    assert(ul->parts == 2u);
    assert(ul->media == 0x14u);
    assert(strcmp(TitanNeutrino_GetElfPath(), "mass:/neutrino/neutrino.elf") == 0);
    assert(TitanNeutrino_GetLaunchCommand(
        0u,
        launch_command,
        sizeof(launch_command)
    ) == TITAN_NEUTRINO_OK);
    assert(strcmp(
        launch_command,
        "mass:/neutrino/neutrino.elf "
        "-cwd=mass:/neutrino "
        "-bsd=usb "
        "-dvd=usb:DVD/ISO Test.iso "
        "-mc0=mass:/TITAN/users/user_0001/profile.vmc "
        "-elf=auto -logo "
        "-titan-igr=return-only -titan-igr-profile=0 "
        "-titan-exit=mc0:/BOOT/BOOT.ELF"
    ) == 0);

    assert(TitanNeutrino_GetPhysicalDiscLaunchCommand(
        "cdrom0:\\SLUS_123.45;1",
        "-mc0=mass:/TITAN/users/user_0001/profile.vmc",
        launch_command,
        sizeof(launch_command)
    ) == TITAN_NEUTRINO_OK);
    assert(strcmp(
        launch_command,
        "mass:/neutrino/neutrino.elf "
        "-cwd=mass:/neutrino -bsd=no -dvd=no "
        "-mc0=mass:/TITAN/users/user_0001/profile.vmc "
        "-elf=cdrom0:\\SLUS_123.45;1 -logo "
        "-titan-igr=return-only -titan-igr-profile=0 "
        "-titan-exit=mc0:/BOOT/BOOT.ELF"
    ) == 0);
    assert(TitanNeutrino_GetPhysicalDiscLaunchCommand(
        "mass:/DVD/not-a-disc.iso", "", launch_command, sizeof(launch_command)
    ) == TITAN_NEUTRINO_INVALID_GAME);


    g_test_gsm_argument = "-gsm=fp2:1";
    assert(TitanNeutrino_GetLaunchCommand(
        0u,
        launch_command,
        sizeof(launch_command)
    ) == TITAN_NEUTRINO_OK);
    assert(strstr(launch_command, " -gsm=fp2:1") != 0);

    g_test_gsm_argument = 0;
    assert(TitanNeutrino_GetLaunchCommand(
        1u,
        launch_command,
        sizeof(launch_command)
    ) == TITAN_NEUTRINO_OK);
    assert(strcmp(
        launch_command,
        "mass:/TITAN/titan_ul_loader.elf SCES_555.55 titan-ul "
        "-vmc0=mass:/TITAN/users/user_0001/profile.vmc "
        "-ulname=USBExtreme Test -ulparts=2 -ulmedia=0x14 -ulcfg=mass:/ul.cfg "
        "-titan-igr=return-only -titan-igr-profile=0 "
        "-titan-exit=mc0:/BOOT/BOOT.ELF"
    ) == 0);
    assert(TitanNeutrino_Launch(1u) == TITAN_NEUTRINO_EXEC_FAILED);
    {
        char log_buffer[512];
        FILE *log_file;
        size_t log_length;

        log_file = fopen("mass:/TITAN/opl_ul_launch.log", "rb");
        assert(log_file != 0);
        log_length = fread(log_buffer, 1u, sizeof(log_buffer) - 1u, log_file);
        fclose(log_file);
        log_buffer[log_length] = '\0';
        assert(strstr(log_buffer, "Project Titan OPL BDM UL launch") != 0);
        assert(strstr(log_buffer, "argv[1]=SCES_555.55") != 0);
        assert(strstr(log_buffer, "argv[2]=titan-ul") != 0);
        assert(strstr(
            log_buffer,
            "argv[3]=-vmc0=mass:/TITAN/users/user_0001/profile.vmc"
        ) != 0);
        assert(strstr(log_buffer, "argv[4]=-ulname=USBExtreme Test") != 0);
        assert(strstr(log_buffer, "argv[5]=-ulparts=2") != 0);
        assert(strstr(log_buffer, "argv[6]=-ulmedia=0x14") != 0);
        assert(strstr(log_buffer, "argv[7]=-ulcfg=mass:/ul.cfg") != 0);
        assert(strstr(log_buffer, "argv[8]=-titan-igr=return-only") != 0);
        assert(strstr(log_buffer, "argv[9]=-titan-igr-profile=0") != 0);
        assert(strstr(log_buffer, "argv[10]=-titan-exit=mc0:/BOOT/BOOT.ELF") != 0);
    }

    assert(remove("mass:/ul.7D3BE5AE.SCES_555.55.01") == 0);
    /*
     * Titan passes the already-scanned ul.cfg metadata to the dedicated OPL
     * backend. The backend still owns physical split-file/fragment validation,
     * so removing a later part does not invalidate command construction.
     */
    assert(TitanNeutrino_GetLaunchCommand(
        1u,
        launch_command,
        sizeof(launch_command)
    ) == TITAN_NEUTRINO_OK);
    assert(TitanNeutrino_Launch(0u) == TITAN_NEUTRINO_EXEC_FAILED);

    puts("Neutrino ISO + OPL UL backend launch tests passed");
    return 0;
}

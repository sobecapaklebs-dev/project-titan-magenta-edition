#include "titan_applications.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_le16(unsigned char *destination, unsigned int value)
{
    destination[0] = (unsigned char)(value & 0xffu);
    destination[1] = (unsigned char)((value >> 8) & 0xffu);
}

static void write_le32(unsigned char *destination, unsigned int value)
{
    destination[0] = (unsigned char)(value & 0xffu);
    destination[1] = (unsigned char)((value >> 8) & 0xffu);
    destination[2] = (unsigned char)((value >> 16) & 0xffu);
    destination[3] = (unsigned char)((value >> 24) & 0xffu);
}

static void create_file(const char *path)
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
    image[0x100] = 0x00u;
    image[0x101] = 0x00u;
    image[0x102] = 0x00u;
    image[0x103] = 0x00u;

    file = fopen(path, "wb");
    assert(file != 0);
    assert(fwrite(image, 1u, sizeof(image), file) == sizeof(image));
    fclose(file);
}

static void create_text_file(const char *path, const char *contents)
{
    FILE *file;

    file = fopen(path, "wb");
    assert(file != 0);
    assert(fwrite(contents, 1u, strlen(contents), file) == strlen(contents));
    fclose(file);
}

static const TitanApplication *find_by_title(const char *title)
{
    unsigned int index;
    for (index = 0u; index < TitanApplications_GetCount(); index++)
    {
        const TitanApplication *application = TitanApplications_Get(index);
        if (application != 0 && strcmp(application->title, title) == 0)
            return application;
    }
    return 0;
}

int main(void)
{
    const TitanApplication *application;

    assert(system("rm -rf /tmp/project_titan_applications_test") == 0);
    assert(mkdir("/tmp/project_titan_applications_test", 0700) == 0);
    assert(chdir("/tmp/project_titan_applications_test") == 0);
    assert(mkdir("mass:", 0700) == 0);
    assert(mkdir("mass:/APPS", 0700) == 0);
    assert(mkdir("mass:/APPS/TOOLS", 0700) == 0);
    assert(mkdir("mass:/DVD", 0700) == 0);
    assert(mkdir("mass:/TITAN", 0700) == 0);
    assert(mkdir("mass:/TITAN/apps", 0700) == 0);
    assert(mkdir("mass:/TITAN/apps/DEMO001", 0700) == 0);
    assert(mkdir("mass:/SAS", 0700) == 0);
    assert(mkdir("mass:/SAS/CFGTEST", 0700) == 0);
    assert(mkdir("mass:/SAS/TITLETEST", 0700) == 0);
    assert(mkdir("mass:/SAS/APP_OPL-1.2.0", 0700) == 0);

    create_file("mass:/APPS/open_ps2_loader.ELF");
    create_file("mass:/APPS/TOOLS/wLaunchELF.elf");
    create_file("mass:/APPS/kernel.elf");
    create_file("mass:/DVD/not_an_application.elf");
    create_file("mass:/APPS/readme.txt");
    create_file("mass:/TITAN/apps/DEMO001/demo.elf");
    create_text_file(
        "mass:/TITAN/apps/DEMO001/titan_app.ini",
        "format=1\n"
        "title_id=DEMO001\n"
        "title=Demo USB App\n"
        "version=1.0.0\n"
        "executable=demo.elf\n"
    );
    create_file("mass:/SAS/CFGTEST/main.elf");
    create_text_file(
        "mass:/SAS/CFGTEST/app.cfg",
        "title=AppCfg Fallback\n"
        "version=2.0\n"
        "executable=main.elf\n"
    );
    create_text_file(
        "mass:/SAS/CFGTEST/title.cfg",
        "title=TitleCfg Canonical\n"
        "boot=main.elf\n"
        "version=2.1\n"
    );
    create_file("mass:/SAS/TITLETEST/tool.elf");
    create_text_file(
        "mass:/SAS/TITLETEST/title.cfg",
        "name=TitleCfg Tool\n"
        "version=3.1\n"
    );
    create_file("mass:/SAS/APP_OPL-1.2.0/opl.elf");

    TitanApplications_Initialize();
    assert(TitanApplications_RefreshFromRoot("mass:/APPS") == 2u);

    application = TitanApplications_Get(0u);
    assert(application != 0);
    assert(strcmp(application->title, "Open Ps2 Loader") == 0);
    assert(strcmp(application->path, "mass:/APPS/open_ps2_loader.ELF") == 0);

    application = TitanApplications_Get(1u);
    assert(application != 0);
    assert(strcmp(application->title, "WLaunchELF") == 0);
    assert(strcmp(application->path, "mass:/APPS/TOOLS/wLaunchELF.elf") == 0);

    assert(TitanApplications_Get(2u) == 0);
    assert(TitanApplications_GetHomeTileCount() == 0u);
    assert(
        TitanApplications_Launch(0u) ==
        TITAN_APPLICATION_EXEC_FAILED
    );

    assert(TitanApplications_RefreshFromRoot("mass:/SAS") == 5u);
    application = find_by_title("TitleCfg Canonical");
    assert(application != 0);
    assert(application->classification == TITAN_APPLICATION_CLASS_SAS);
    assert(application->metadata_source == TITAN_APPLICATION_METADATA_TITLE_CFG);
    assert(TitanApplications_IsSas(0u));
    assert(TitanApplications_GetExecutionModel(0u) == TITAN_APPLICATION_EXECUTION_EXTERNAL_HANDOFF);
    assert(strcmp(TitanApplications_GetExecutionModelName(TITAN_APPLICATION_EXECUTION_EXTERNAL_HANDOFF), "External ELF handoff") == 0);
    assert(strcmp(application->version, "2.1") == 0);
    application = find_by_title("TitleCfg Tool");
    assert(application != 0);
    assert(application->classification == TITAN_APPLICATION_CLASS_SAS);
    assert(application->metadata_source == TITAN_APPLICATION_METADATA_TITLE_CFG);
    application = find_by_title("OPL");
    assert(application != 0);
    assert(application->classification == TITAN_APPLICATION_CLASS_SAS);
    assert(application->metadata_source == TITAN_APPLICATION_METADATA_FOLDER_PREFIX);
    assert(strcmp(application->version, "1.2.0") == 0);
    application = find_by_title("WLaunchELF");
    assert(application != 0);
    assert(application->classification == TITAN_APPLICATION_CLASS_UNKNOWN_LEGACY);
    assert(application->metadata_source == TITAN_APPLICATION_METADATA_NAME_HEURISTIC);
    assert(TitanApplications_GetExecutionModel(3u) == TITAN_APPLICATION_EXECUTION_EXTERNAL_HANDOFF);

    assert(
        TitanApplications_RefreshFromRoot("mass:/TITAN/apps") == 6u
    );
    assert(TitanApplications_GetHomeTileCount() == 1u);
    application = TitanApplications_GetHomeTile(0u, 0);
    assert(application != 0);
    assert(strcmp(application->title, "Demo USB App") == 0);
    assert(
        strcmp(
            application->path,
            "mass:/TITAN/apps/DEMO001/demo.elf"
        ) == 0
    );
    assert(TitanApplications_GetHomeTile(1u, 0) == 0);

    puts("Application ELF discovery tests passed");
    return 0;
}

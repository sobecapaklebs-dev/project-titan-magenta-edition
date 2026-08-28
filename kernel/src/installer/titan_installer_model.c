#include "titan_installer_model.h"

#include <fcntl.h>
#include <libcdvd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct TitanInstallerModelRecord
{
    u16 id;
    const char *name;
} TitanInstallerModelRecord;

/* Read-only model database. IDs are public console-identification facts also
 * used by MechaPwn. We never issue NVRAM writes or MechaCon unlock commands. */
static const TitanInstallerModelRecord g_model_db[] = {
    /* Public model-ID catalogue synchronized with MechaResearch/MechaPwn.
     * Only read-only identification data is copied here; the installer never
     * writes NVRAM or modifies MechaCon state. */
    {0xd200, "DTL-H10000"}, {0xd201, "SCPH-10000"}, {0xd202, "SCPH-15000/18000"},
    {0xd203, "SCPH-30001"}, {0xd204, "SCPH-30002/R"}, {0xd205, "SCPH-30003/R"},
    {0xd206, "SCPH-30004/R"}, {0xd207, "DTL-H30001"}, {0xd208, "DTL-H30002"},
    {0xd209, "COH-H30000"}, {0xd20a, "SCPH-18000"}, {0xd20b, "COH-H31000"},
    {0xd20c, "SCPH-30000"}, {0xd20d, "DTL-H30000"}, {0xd20e, "COH-H31100"},
    {0xd20f, "SCPH-35001 GT"}, {0xd210, "SCPH-35002 GT"}, {0xd211, "SCPH-35003 GT"},
    {0xd212, "SCPH-35004 GT"}, {0xd213, "SCPH-35000 GT"}, {0xd214, "SCPH-30001/R"},
    {0xd215, "SCPH-30005 R"}, {0xd216, "SCPH-30006 R"}, {0xd217, "SCPH-39000"},
    {0xd218, "SCPH-39001"}, {0xd219, "SCPH-39002"}, {0xd21a, "SCPH-39003"},
    {0xd21b, "SCPH-39004"}, {0xd21d, "SCPH-37000 L"}, {0xd21e, "SCPH-37000 B"},
    {0xd21f, "SCPH-39008"}, {0xd220, "SCPH-39000 TB"}, {0xd221, "SCPH-39000 RC"},
    {0xd222, "SCPH-39006"}, {0xd225, "DTL-H10100"}, {0xd226, "DTL-H30100"},
    {0xd227, "DTL-H30101"}, {0xd228, "DTL-H30102"}, {0xd229, "DTL-H30105"},
    {0xd22a, "SCPH-39000 S"}, {0xd22b, "SCPH-39000 AQ"}, {0xd22c, "SCPH-39000 SA"},
    {0xd22d, "SCPH-39010/N"}, {0xd22e, "SCPH-39005"},

    {0xd301, "DTL-H50000"}, {0xd302, "DTL-H50001"}, {0xd303, "DTL-H50002"},
    {0xd304, "DTL-H50009"}, {0xd31e, "DTL-H70002"}, {0xd322, "DTL-H75000A"},
    {0xd326, "DTL-H90000(a)"},

    {0xd380, "DESR-7000"}, {0xd381, "DESR-5000"}, {0xd382, "DESR-7100"},
    {0xd383, "DESR-5100"}, {0xd384, "DESR-5100/S"}, {0xd385, "DESR-7500"},
    {0xd386, "DESR-5500"}, {0xd387, "DESR-7700"}, {0xd388, "DESR-5700"},

    {0xd401, "SCPH-50001/N"}, {0xd402, "SCPH-50010/N"}, {0xd403, "SCPH-50000"},
    {0xd404, "SCPH-50000 MB/NH"}, {0xd405, "SCPH-50002"}, {0xd406, "SCPH-50003"},
    {0xd407, "SCPH-50004"}, {0xd408, "SCPH-50002 SS"}, {0xd409, "SCPH-50003 SS"},
    {0xd40a, "SCPH-50004 SS"}, {0xd40b, "SCPH-50001"}, {0xd40c, "SCPH-50005/N"},
    {0xd40d, "SCPH-50006"}, {0xd40e, "SCPH-50007"}, {0xd40f, "SCPH-50008"},
    {0xd411, "SCPH-50000 NB"}, {0xd412, "SCPH-50000 TSS"}, {0xd413, "SCPH-55000 GU"},
    {0xd414, "SCPH-55000 GT"}, {0xd415, "SCPH-50009 SS"}, {0xd416, "SCPH-50003 AQ"},
    {0xd417, "SCPH-55005 GT/N"}, {0xd418, "SCPH-55006 GT"}, {0xd419, "SCPH-55007 GT"},
    {0xd41a, "SCPH-50008 SS"}, {0xd41b, "SCPH-50004 AQ"}, {0xd41c, "SCPH-50005 SS/N"},
    {0xd41d, "SCPH-50005 AQ/N"}, {0xd41e, "SCPH-50000 CW"}, {0xd41f, "SCPH-50000 SA"},
    {0xd420, "SCPH-50004 ESS"}, {0xd422, "SCPH-50002 ESS"}, {0xd423, "SCPH-50003 ESS"},
    {0xd424, "SCPH-50000 PW"}, {0xd425, "SCPH-50011"},

    {0xd426, "SCPH-70004"}, {0xd427, "SCPH-70003"}, {0xd428, "SCPH-70002"},
    {0xd429, "SCPH-70011"}, {0xd42a, "SCPH-70012"}, {0xd42b, "SCPH-70000"},
    {0xd42c, "SCPH-70005"}, {0xd42d, "SCPH-70006"}, {0xd42e, "SCPH-70007"},
    {0xd42f, "SCPH-70000 GT"}, {0xd430, "SCPH-70008"}, {0xd431, "SCPH-70002 SS"},
    {0xd432, "SCPH-70003 SS"}, {0xd433, "SCPH-70004 SS"}, {0xd434, "SCPH-70008 SS"},
    {0xd435, "SCPH-70001"}, {0xd436, "SCPH-70010"}, {0xd437, "SCPH-70000 CW"},
    {0xd438, "SCPH-70003 SS"}, {0xd439, "SCPH-70004 SS"}, {0xd43a, "SCPH-70008 SS"},
    {0xd43b, "SCPH-75001"}, {0xd43c, "SCPH-75002"}, {0xd43d, "SCPH-75003"},
    {0xd43e, "SCPH-75004"}, {0xd43f, "SCPH-75000 SSS"}, {0xd440, "SCPH-75002 SS"},
    {0xd441, "SCPH-75003 SS"}, {0xd442, "SCPH-75004 SS"}, {0xd443, "SCPH-75000"},
    {0xd444, "SCPH-75000 CW"}, {0xd445, "SCPH-75006"}, {0xd446, "SCPH-75007"},
    {0xd447, "SCPH-75005"}, {0xd448, "SCPH-75010"}, {0xd449, "SCPH-75000 FF"},
    {0xd44c, "SCPH-75008"}, {0xd44d, "SCPH-75008 SS"}, {0xd44e, "SCPH-77001"},
    {0xd44f, "SCPH-77002"}, {0xd450, "SCPH-77003"}, {0xd451, "SCPH-77004"},
    {0xd452, "SCPH-77002 SS"}, {0xd453, "SCPH-77003 SS"}, {0xd454, "SCPH-77004 SS"},
    {0xd455, "SCPH-77000"}, {0xd456, "SCPH-77000 CW"}, {0xd457, "SCPH-77005"},
    {0xd458, "SCPH-77006"}, {0xd459, "SCPH-77007"}, {0xd45a, "SCPH-77008"},
    {0xd45b, "SCPH-77008 SS"}, {0xd45d, "SCPH-77001 SS"}, {0xd45e, "SCPH-77003 PK"},
    {0xd45f, "SCPH-77004 PK"}, {0xd461, "SCPH-77000 SS"}, {0xd462, "SCPH-77000 PK"},
    {0xd463, "SCPH-77008 PK"}, {0xd464, "SCPH-77002 PK"}, {0xd465, "SCPH-79001"},
    {0xd466, "SCPH-79000"}, {0xd467, "SCPH-79000 CW"}, {0xd468, "SCPH-79002"},
    {0xd469, "SCPH-79001 SS"}, {0xd46b, "SCPH-79006"}, {0xd46d, "SCPH-79000 SS"},
    {0xd46e, "SCPH-79003"}, {0xd46f, "SCPH-79004"}, {0xd470, "SCPH-79010"},
    {0xd471, "SCPH-79003 SS"}, {0xd473, "SCPH-79008"}, {0xd474, "SCPH-79001 CW"},

    {0xd475, "SCPH-90000"}, {0xd476, "SCPH-90000 CW"}, {0xd477, "SCPH-90000 SS"},
    {0xd478, "SCPH-90006"}, {0xd479, "SCPH-90006 CW"}, {0xd47a, "SCPH-90006 SS"},
    {0xd47b, "SCPH-90005"}, {0xd47e, "SCPH-90007"}, {0xd47f, "SCPH-90007 CW"},
    {0xd481, "SCPH-90001"}, {0xd482, "SCPH-90001 SS"}, {0xd483, "SCPH-90004"},
    {0xd484, "SCPH-90004 SS"}, {0xd485, "SCPH-90002"}, {0xd486, "SCPH-90003"},
    {0xd488, "SCPH-90006 CR"}, {0xd489, "SCPH-90007 CR"}, {0xd48a, "SCPH-90010"},
    {0xd48b, "SCPH-90000 CR"}, {0xd48c, "SCPH-90008"}, {0xd48d, "SCPH-90008 SS"},
    {0xd48f, "PX300-1"}, {0xd490, "PX300-2"}, {0xd491, "SCPH-90010 CR"}
};

static void copy_text(char *dst, unsigned int cap, const char *src)
{
    unsigned int i = 0;
    if (dst == 0 || cap == 0u) return;
    if (src == 0) src = "";
    while (i + 1u < cap && src[i] != '\0') { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

const char *TitanInstallerModel_LookupName(u16 model_id)
{
    unsigned int i;
    for (i = 0u; i < sizeof(g_model_db) / sizeof(g_model_db[0]); i++)
        if (g_model_db[i].id == model_id) return g_model_db[i].name;
    return 0;
}

static unsigned int scph_number(const char *name)
{
    const char *p;
    if (name == 0) return 0u;
    p = strstr(name, "SCPH-");
    if (p == 0) return 0u;
    return (unsigned int)strtoul(p + 5, 0, 10);
}

TitanInstallerConsoleFamily TitanInstallerModel_FamilyFromName(const char *name)
{
    unsigned int n = scph_number(name);
    const char *dtl;

    if (n >= 90000u) return TITAN_INSTALLER_CONSOLE_SLIM_90;
    if (n >= 70000u) return TITAN_INSTALLER_CONSOLE_SLIM_70;
    if (n >= 10000u) return TITAN_INSTALLER_CONSOLE_FAT;

    if (name == 0) return TITAN_INSTALLER_CONSOLE_UNKNOWN;
    dtl = strstr(name, "DTL-H");
    if (dtl != 0) {
        n = (unsigned int)strtoul(dtl + 5, 0, 10);
        if (n >= 90000u) return TITAN_INSTALLER_CONSOLE_SLIM_90;
        if (n >= 70000u) return TITAN_INSTALLER_CONSOLE_SLIM_70;
        if (n >= 10000u) return TITAN_INSTALLER_CONSOLE_FAT;
    }

    if (strstr(name, "COH-H") != 0)
        return TITAN_INSTALLER_CONSOLE_FAT;
    return TITAN_INSTALLER_CONSOLE_UNKNOWN;
}

const char *TitanInstallerModel_GetFamilyName(TitanInstallerConsoleFamily family)
{
    switch (family) {
        case TITAN_INSTALLER_CONSOLE_FAT: return "PlayStation 2";
        case TITAN_INSTALLER_CONSOLE_SLIM_70: return "PlayStation 2 slim (70/75/77/79)";
        case TITAN_INSTALLER_CONSOLE_SLIM_90: return "PlayStation 2 slim (90)";
        default: return "PlayStation 2";
    }
}

static unsigned int rom_version_code(const char *romver)
{
    unsigned int value = 0u;
    unsigned int index;
    if (romver == 0) return 0u;
    for (index = 0u; index < 4u; index++) {
        char ch = romver[index];
        if (ch < '0' || ch > '9') return 0u;
        value = value * 10u + (unsigned int)(ch - '0');
    }
    return value;
}

static TitanInstallerConsoleFamily family_from_model_id_range(u16 model_id)
{
    /* Known MechaPwn ID blocks. Keep unknown holes unknown by exact model,
     * but use the block to choose the correct physical illustration family. */
    if ((model_id >= 0xd200u && model_id <= 0xd22eu) ||
        (model_id >= 0xd300u && model_id <= 0xd304u) ||
        (model_id >= 0xd400u && model_id <= 0xd425u))
        return TITAN_INSTALLER_CONSOLE_FAT;
    if ((model_id >= 0xd31eu && model_id <= 0xd325u) ||
        (model_id >= 0xd426u && model_id <= 0xd474u))
        return TITAN_INSTALLER_CONSOLE_SLIM_70;
    if (model_id == 0xd48fu || model_id == 0xd490u)
        return TITAN_INSTALLER_CONSOLE_UNKNOWN;
    if (model_id == 0xd326u || (model_id >= 0xd475u && model_id <= 0xd491u))
        return TITAN_INSTALLER_CONSOLE_SLIM_90;
    return TITAN_INSTALLER_CONSOLE_UNKNOWN;
}

static TitanInstallerConsoleFamily family_from_romver(const char *romver)
{
    unsigned int version = rom_version_code(romver);

    /* ROM 2.00 is the first retail slim generation (the common 2004-06-14
     * image used by PCSX2 belongs here). 2.30 is the late 90k generation.
     * Exact SCPH remains ID-driven; ROMVER is only a safe visual-family fallback. */
    if (version >= 230u)
        return TITAN_INSTALLER_CONSOLE_SLIM_90;
    if (version >= 200u)
        return TITAN_INSTALLER_CONSOLE_SLIM_70;
    if (version != 0u)
        return TITAN_INSTALLER_CONSOLE_FAT;
    return TITAN_INSTALLER_CONSOLE_UNKNOWN;
}


unsigned int TitanInstallerModel_GetRomVersionCode(const TitanInstallerConsoleInfo *info)
{
    if (info == 0 || !info->romver[0]) return 0u;
    return rom_version_code(info->romver);
}

int TitanInstallerModel_IsAutonomousCompatible(const TitanInstallerConsoleInfo *info)
{
    unsigned int rom;
    if (info == 0 || !info->model_id_valid || !info->romver[0] || info->family == TITAN_INSTALLER_CONSOLE_UNKNOWN)
        return 0;
    rom = rom_version_code(info->romver);
    if (rom == 0u || rom > 220u) return 0; /* Product/release scope: FMCB autonomous <= ROM 2.20 only. */
    if (info->romver[4] != 'E' && info->romver[4] != 'A' && info->romver[4] != 'J' &&
        info->romver[4] != 'C' && info->romver[4] != 'H') return 0;
    /* Keep model/ROM consistency checks. Unknown or internally inconsistent
     * identity always fails closed even when the numeric ROM is <= 2.20. */
    if (info->family == TITAN_INSTALLER_CONSOLE_FAT) return rom < 200u;
    if (info->family == TITAN_INSTALLER_CONSOLE_SLIM_70) return rom >= 200u && rom <= 220u;
    if (info->family == TITAN_INSTALLER_CONSOLE_SLIM_90) return 0;
    return 0;
}

static const char *family_fallback_name(TitanInstallerConsoleFamily family)
{
    switch (family) {
        case TITAN_INSTALLER_CONSOLE_FAT: return "PS2 FAT family";
        case TITAN_INSTALLER_CONSOLE_SLIM_70: return "PS2 slim 70/75/77/79 family";
        case TITAN_INSTALLER_CONSOLE_SLIM_90: return "PS2 slim 90 family";
        default: return "Unknown PS2 model";
    }
}

void TitanInstallerModel_Detect(TitanInstallerConsoleInfo *info)
{
    int fd;
    u8 version[4] = {0,0,0,0};
    u32 status = 0u;
    u8 nvm_status = 0u;
    u16 model_id = 0u;
    const char *name = 0;

    if (info == 0) return;
    memset(info, 0, sizeof(*info));
    info->family = TITAN_INSTALLER_CONSOLE_UNKNOWN;
    copy_text(info->model_name, sizeof(info->model_name), "Unknown PS2 model");

    fd = open("rom0:ROMVER", O_RDONLY);
    if (fd >= 0) {
        int got = read(fd, info->romver, sizeof(info->romver) - 1u);
        if (got < 0) got = 0;
        info->romver[got] = '\0';
        close(fd);
    }

    /* sceCdMV is read-only. Its four-byte result is region/major/minor/type. */
    (void)sceCdInit(SCECdINIT);
    if (sceCdMV(version, &status)) {
        info->mechacon_major = version[1];
        info->mechacon_minor = version[2];
        /* MechaPwn established the model ID location: old MechaCon revisions
         * use NVRAM word 0xE4; v4+ use 0xF8. */
        if (sceCdReadNVM(version[1] < 4u ? 0xE4u : 0xF8u, &model_id, &nvm_status)) {
            info->model_id_valid = 1;
            info->model_id = model_id;
            name = TitanInstallerModel_LookupName(model_id);
            if (name != 0) copy_text(info->model_name, sizeof(info->model_name), name);
        }
    }

    info->family = TitanInstallerModel_FamilyFromName(info->model_name);
    if (info->family == TITAN_INSTALLER_CONSOLE_UNKNOWN && info->model_id_valid)
        info->family = family_from_model_id_range(info->model_id);
    if (info->family == TITAN_INSTALLER_CONSOLE_UNKNOWN)
        info->family = family_from_romver(info->romver);

    if (name == 0 && info->family != TITAN_INSTALLER_CONSOLE_UNKNOWN)
        copy_text(info->model_name, sizeof(info->model_name), family_fallback_name(info->family));
}

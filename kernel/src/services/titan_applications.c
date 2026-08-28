#include "titan_applications.h"

#include "titan_app_runtime.h"
#include "titan_elf_validate.h"
#include "titan_exec_handoff.h"
#include "titan_localization.h"
#include "titan_storage_io.h"
#include "titan_sas.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define TITAN_APPLICATION_MAX_SCAN_DEPTH 3u
#define TITAN_APPLICATION_MANIFEST_NAME "titan_app.ini"

static TitanApplication g_applications[TITAN_APPLICATION_CAPACITY];
static unsigned int g_application_count;
static unsigned int g_catalog_generation;

static void TitanApplications_CopyText(
    char *destination,
    unsigned int capacity,
    const char *source
)
{
    unsigned int index;

    if (destination == 0 || capacity == 0u)
        return;
    index = 0u;
    while (source != 0 && source[index] != '\0' && index + 1u < capacity)
    {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int TitanApplications_NameEqualsInsensitive(
    const char *left,
    const char *right
)
{
    if (left == 0 || right == 0)
        return 0;

    while (*left != '\0' && *right != '\0')
    {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return 0;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static int TitanApplications_HasExtension(const char *name, const char *extension)
{
    unsigned int name_length;
    unsigned int extension_length;

    if (name == 0 || extension == 0)
        return 0;
    name_length = (unsigned int)strlen(name);
    extension_length = (unsigned int)strlen(extension);
    if (name_length < extension_length)
        return 0;
    return TitanApplications_NameEqualsInsensitive(
        name + name_length - extension_length,
        extension
    );
}

static int TitanApplications_HasElfExtension(const char *name)
{
    return TitanApplications_HasExtension(name, ".elf");
}

static int TitanApplications_HasTmodExtension(const char *name)
{
    return TitanApplications_HasExtension(name, ".tmod");
}

static int TitanApplications_RootEntrySafe(const char *name)
{
    return name != 0 && name[0] != '\0' &&
        strchr(name, '/') == 0 && strchr(name, '\\') == 0 &&
        strchr(name, ':') == 0 && strcmp(name, ".") != 0 &&
        strcmp(name, "..") != 0;
}

static int TitanApplications_IsInstalledRoot(const char *root_path)
{
    static const char *prefixes[] =
    {
        "mass:/TITAN/apps/",
        "mass0:/TITAN/apps/"
    };
    unsigned int index;

    if (root_path == 0)
        return 0;

    for (index = 0u; index < sizeof(prefixes) / sizeof(prefixes[0]); index++)
    {
        unsigned int length;
        const char *tail;

        length = (unsigned int)strlen(prefixes[index]);
        if (strncmp(root_path, prefixes[index], length) != 0)
            continue;
        tail = root_path + length;
        if (tail[0] == '\0' || strchr(tail, '/') != 0 || strchr(tail, '\\') != 0 ||
            strcmp(tail, ".") == 0 || strcmp(tail, "..") == 0 || tail[0] == '.')
            return 0;
        return 1;
    }
    return 0;
}

static int TitanApplications_ShouldSkipDirectory(const char *name)
{
    static const char *skipped[] =
    {
        ".", "..", "DVD", "CD", "ART", "VMC", "THM", "CFG",
        "modules", "config"
    };
    unsigned int index;

    if (name == 0 || name[0] == '\0' || name[0] == '.')
        return 1;
    for (index = 0u; index < sizeof(skipped) / sizeof(skipped[0]); index++)
    {
        if (TitanApplications_NameEqualsInsensitive(name, skipped[index]))
            return 1;
    }
    return 0;
}

static int TitanApplications_IsInternalElf(const char *path)
{
    const char *name;

    if (path == 0)
        return 1;
    name = strrchr(path, '/');
    name = name != 0 ? name + 1 : path;
    return TitanApplications_NameEqualsInsensitive(name, "kernel.elf") ||
        TitanApplications_NameEqualsInsensitive(name, "titan.elf") ||
        TitanApplications_NameEqualsInsensitive(name, "neutrino.elf");
}

static void TitanApplications_BuildTitle(
    char *destination,
    unsigned int capacity,
    const char *filename
)
{
    unsigned int source_length;
    unsigned int index;
    unsigned int output;
    int capitalize_next;

    if (destination == 0 || capacity == 0u)
        return;
    destination[0] = '\0';
    if (filename == 0)
        return;

    source_length = (unsigned int)strlen(filename);
    if (source_length >= 4u && TitanApplications_HasElfExtension(filename))
        source_length -= 4u;
    else if (source_length >= 5u && TitanApplications_HasTmodExtension(filename))
        source_length -= 5u;

    output = 0u;
    capitalize_next = 1;
    for (index = 0u; index < source_length && output + 1u < capacity; index++)
    {
        unsigned char character;

        character = (unsigned char)filename[index];
        if (character == '_' || character == '-')
        {
            if (output > 0u && destination[output - 1u] != ' ')
                destination[output++] = ' ';
            capitalize_next = 1;
            continue;
        }
        if (capitalize_next && isalpha(character))
            character = (unsigned char)toupper(character);
        destination[output++] = (char)character;
        capitalize_next = character == ' ';
    }
    while (output > 0u && destination[output - 1u] == ' ')
        output--;
    destination[output] = '\0';
    if (destination[0] == '\0')
        TitanApplications_CopyText(destination, capacity, "Application");
}

static int TitanApplications_PathJoin(
    char *output,
    unsigned int capacity,
    const char *root,
    const char *name
);

static int TitanApplications_CopyDirectoryPath(
    char *directory,
    unsigned int capacity,
    const char *path
)
{
    const char *separator;
    unsigned int length;

    if (directory == 0 || capacity == 0u || path == 0)
        return 0;
    separator = strrchr(path, '/');
    if (separator == 0)
        return 0;
    length = (unsigned int)(separator - path);
    if (length == 0u || length + 1u > capacity)
        return 0;
    memcpy(directory, path, length);
    directory[length] = '\0';
    return 1;
}

static int TitanApplications_ConfigMetadata(
    const char *directory,
    const char *config_name,
    const char *filename,
    TitanApplication *application
)
{
    char path[TITAN_APPLICATION_PATH_CAPACITY];
    char title[TITAN_APPLICATION_TITLE_CAPACITY];
    char version[TITAN_APPLICATION_VERSION_CAPACITY];
    char author[TITAN_APPLICATION_AUTHOR_CAPACITY];
    char description[TITAN_APPLICATION_DESCRIPTION_CAPACITY];
    char credits[TITAN_APPLICATION_CREDITS_CAPACITY];
    char source[TITAN_APPLICATION_SOURCE_CAPACITY];
    char icon[128];
    char tile[128];
    char background[128];
    char executable[128];
    char line[320];
    FILE *file;
    int recognized = 0;
    int saw_boot = 0;

    if (!TitanApplications_PathJoin(path, sizeof(path), directory, config_name))
        return 0;
    file = fopen(path, "rb");
    if (file == 0)
        return 0;
    memset(title, 0, sizeof(title));
    memset(version, 0, sizeof(version));
    memset(author, 0, sizeof(author));
    memset(description, 0, sizeof(description));
    memset(credits, 0, sizeof(credits));
    memset(source, 0, sizeof(source));
    memset(icon, 0, sizeof(icon));
    memset(tile, 0, sizeof(tile));
    memset(background, 0, sizeof(background));
    memset(executable, 0, sizeof(executable));
    while (fgets(line, sizeof(line), file) != 0)
    {
        char *newline = strpbrk(line, "\r\n");
        if (newline != 0) *newline = '\0';
        if (line[0] == '#' || line[0] == ';' || line[0] == '\0')
            continue;
        if (strncmp(line, "title=", 6u) == 0)
        {
            TitanApplications_CopyText(title, sizeof(title), line + 6u);
            recognized = 1;
        }
        else if (strncmp(line, "name=", 5u) == 0)
        {
            TitanApplications_CopyText(title, sizeof(title), line + 5u);
            recognized = 1;
        }
        else if (strncmp(line, "version=", 8u) == 0)
        {
            TitanApplications_CopyText(version, sizeof(version), line + 8u);
            recognized = 1;
        }
        else if (strncmp(line, "author=", 7u) == 0)
        {
            TitanApplications_CopyText(author, sizeof(author), line + 7u);
            recognized = 1;
        }
        else if (strncmp(line, "description=", 12u) == 0)
        {
            TitanApplications_CopyText(description, sizeof(description), line + 12u);
            recognized = 1;
        }
        else if (strncmp(line, "credits=", 8u) == 0)
        {
            TitanApplications_CopyText(credits, sizeof(credits), line + 8u);
            recognized = 1;
        }
        else if (strncmp(line, "source=", 7u) == 0)
        {
            TitanApplications_CopyText(source, sizeof(source), line + 7u);
            recognized = 1;
        }
        else if (strncmp(line, "icon=", 5u) == 0)
        {
            TitanApplications_CopyText(icon, sizeof(icon), line + 5u);
            recognized = 1;
        }
        else if (strncmp(line, "tile=", 5u) == 0)
        {
            TitanApplications_CopyText(tile, sizeof(tile), line + 5u);
            recognized = 1;
        }
        else if (strncmp(line, "background=", 11u) == 0)
        {
            TitanApplications_CopyText(background, sizeof(background), line + 11u);
            recognized = 1;
        }
        else if (strncmp(line, "entrypoint=", 11u) == 0)
        {
            TitanApplications_CopyText(executable, sizeof(executable), line + 11u);
            recognized = 1;
        }
        else if (strncmp(line, "executable=", 11u) == 0)
        {
            TitanApplications_CopyText(executable, sizeof(executable), line + 11u);
            recognized = 1;
        }
        else if (strncmp(line, "elf=", 4u) == 0)
        {
            TitanApplications_CopyText(executable, sizeof(executable), line + 4u);
            recognized = 1;
        }
        else if (strncmp(line, "boot=", 5u) == 0)
        {
            TitanApplications_CopyText(executable, sizeof(executable), line + 5u);
            recognized = 1;
            saw_boot = 1;
        }
    }
    fclose(file);
    if (!recognized)
        return 0;
    if (executable[0] != '\0' &&
        !TitanApplications_NameEqualsInsensitive(executable, filename))
        return 0;
    if (TitanApplications_NameEqualsInsensitive(config_name, "title.cfg"))
        application->sas_title_has_boot = saw_boot ? 1u : 0u;

    TitanApplications_CopyText(
        application->root_path, sizeof(application->root_path), directory
    );
    if (title[0] != '\0')
        TitanApplications_CopyText(application->title, sizeof(application->title), title);
    if (version[0] != '\0')
        TitanApplications_CopyText(application->version, sizeof(application->version), version);
    if (author[0] != '\0')
        TitanApplications_CopyText(application->author, sizeof(application->author), author);
    if (description[0] != '\0')
        TitanApplications_CopyText(
            application->description, sizeof(application->description), description
        );
    if (credits[0] != '\0')
        TitanApplications_CopyText(
            application->credits, sizeof(application->credits), credits
        );
    if (source[0] != '\0')
        TitanApplications_CopyText(
            application->source, sizeof(application->source), source
        );
    if (icon[0] != '\0' && TitanApplications_RootEntrySafe(icon))
        (void)TitanApplications_PathJoin(
            application->icon_path, sizeof(application->icon_path), directory, icon
        );
    if (tile[0] != '\0' && TitanApplications_RootEntrySafe(tile))
        (void)TitanApplications_PathJoin(
            application->tile_path, sizeof(application->tile_path), directory, tile
        );
    else
        (void)TitanApplications_PathJoin(
            application->tile_path, sizeof(application->tile_path), directory, "tile.rgb565"
        );
    if (background[0] != '\0' && TitanApplications_RootEntrySafe(background))
        (void)TitanApplications_PathJoin(
            application->background_path,
            sizeof(application->background_path),
            directory,
            background
        );
    return 1;
}

static void TitanApplications_SetSasState(
    TitanApplication *application,
    int compatible,
    int compliant
)
{
    if (application == 0)
        return;
    application->sas_compliant = compliant ? 1u : 0u;
    application->sas_compatible = (compatible || compliant) ? 1u : 0u;
    if (application->sas_compatible != 0u)
        application->classification = TITAN_APPLICATION_CLASS_SAS;
}

static int TitanApplications_DirectoryHasSubfolders(const char *directory)
{
    DIR *dir;
    struct dirent *entry;
    char path[TITAN_APPLICATION_PATH_CAPACITY];
    struct stat st;

    if (directory == 0)
        return 0;
    dir = opendir(directory);
    if (dir == 0)
        return 0;
    while ((entry = readdir(dir)) != 0)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (!TitanApplications_PathJoin(path, sizeof(path), directory, entry->d_name))
            continue;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        {
            closedir(dir);
            return 1;
        }
    }
    closedir(dir);
    return 0;
}

static int TitanApplications_IsMemoryCardRootFolder(const char *directory)
{
    const char *colon;
    const char *relative;
    if (directory == 0 ||
        !(strncmp(directory, "mc0:", 4u) == 0 || strncmp(directory, "mc1:", 4u) == 0 ||
          strncmp(directory, "mc?:", 4u) == 0))
        return 0;
    colon = strchr(directory, ':');
    if (colon == 0)
        return 0;
    relative = colon + 1;
    while (*relative == '/')
        relative++;
    return *relative != '\0' && strchr(relative, '/') == 0;
}

static void TitanApplications_ApplySasStructure(
    const char *directory,
    TitanApplication *application
)
{
    const char *folder;
    const char *underscore;
    const TitanSasPrefixRule *rule = 0;
    unsigned int prefix_length = 0u;
    int canonical_title;

    if (directory == 0 || application == 0 || application->sas_compatible == 0u)
        return;

    folder = strrchr(directory, '/');
    folder = folder != 0 ? folder + 1 : directory;
    underscore = strchr(folder, '_');
    if (underscore != 0 && underscore != folder)
    {
        prefix_length = (unsigned int)(underscore - folder);
        rule = TitanSas_FindPrefixRule(folder, prefix_length);
    }
    application->sas_prefix_known = rule != 0 ? 1u : 0u;
    application->sas_has_subfolders = TitanApplications_DirectoryHasSubfolders(directory) ? 1u : 0u;
    application->sas_copyability = application->sas_has_subfolders != 0u
        ? TITAN_SAS_OSDSYS_COPY_UNSAFE
        : TITAN_SAS_OSDSYS_COPY_SAFE;

    canonical_title = application->metadata_source == TITAN_APPLICATION_METADATA_TITLE_CFG &&
        application->sas_title_has_boot != 0u;

    if (rule != 0 &&
        rule->category == TITAN_SAS_PREFIX_CATEGORY_APPLICATION &&
        canonical_title &&
        application->sas_has_subfolders == 0u)
    {
        TitanApplications_SetSasState(application, 1, 1);
        application->sas_profile = TITAN_SAS_PROFILE_CLASSIC;
        application->sas_osdsys_manageable =
            TitanApplications_IsMemoryCardRootFolder(directory) ? 1u : 0u;
    }
    else
    {
        TitanApplications_SetSasState(application, 1, 0);
        application->sas_profile = application->metadata_source == TITAN_APPLICATION_METADATA_APP_CFG
            ? TITAN_SAS_PROFILE_TITAN_FALLBACK
            : TITAN_SAS_PROFILE_EXTENDED_COMPAT;
        application->sas_osdsys_manageable = 0u;
    }
}

static int TitanApplications_FolderPrefixMetadata(
    const char *directory,
    TitanApplication *application
)
{
    const char *folder;
    const char *underscore;
    const char *version_separator;
    const char *cursor;
    char app_name[TITAN_APPLICATION_TITLE_CAPACITY];
    unsigned int app_length;
    int version_valid = 0;

    if (directory == 0 || application == 0)
        return 0;
    folder = strrchr(directory, '/');
    folder = folder != 0 ? folder + 1 : directory;
    underscore = strchr(folder, '_');
    if (underscore == 0 || underscore == folder || underscore[1] == '\0')
        return 0;
    {
        unsigned int prefix_length = (unsigned int)(underscore - folder);
        if (TitanSas_FindPrefixRule(folder, prefix_length) == 0 &&
            !TitanSas_IsThreeLetterCompatibilityPrefix(folder, prefix_length))
            return 0;
    }

    version_separator = strrchr(underscore + 1, '-');
    if (version_separator != 0 && version_separator[1] != '\0')
    {
        version_valid = 1;
        for (cursor = version_separator + 1; *cursor != '\0'; cursor++)
        {
            if ((*cursor < '0' || *cursor > '9') && *cursor != '.')
            {
                version_valid = 0;
                break;
            }
        }
    }
    if (!version_valid)
        version_separator = 0;
    app_length = (unsigned int)((version_separator != 0 ? version_separator : folder + strlen(folder)) - (underscore + 1));
    if (app_length == 0u || app_length >= sizeof(app_name))
        return 0;
    memcpy(app_name, underscore + 1, app_length);
    app_name[app_length] = '\0';
    TitanApplications_BuildTitle(
        application->title, sizeof(application->title), app_name
    );
    if (version_separator != 0)
        TitanApplications_CopyText(
            application->version,
            sizeof(application->version),
            version_separator + 1
        );
    TitanApplications_CopyText(
        application->root_path, sizeof(application->root_path), directory
    );
    TitanApplications_SetSasState(application, 1, 0);
    application->metadata_source = TITAN_APPLICATION_METADATA_FOLDER_PREFIX;
    return 1;
}

static void TitanApplications_ResolveLegacyMetadata(
    const char *path,
    const char *filename,
    TitanApplication *application
)
{
    char directory[TITAN_APPLICATION_PATH_CAPACITY];

    application->classification = TITAN_APPLICATION_CLASS_UNKNOWN_LEGACY;
    application->metadata_source = TITAN_APPLICATION_METADATA_NAME_HEURISTIC;
    if (!TitanApplications_CopyDirectoryPath(directory, sizeof(directory), path))
        return;
    if (TitanApplications_ConfigMetadata(
            directory, "title.cfg", filename, application))
    {
        TitanApplications_SetSasState(application, 1, 0);
        application->metadata_source = TITAN_APPLICATION_METADATA_TITLE_CFG;
        TitanApplications_ApplySasStructure(directory, application);
        return;
    }
    if (TitanApplications_ConfigMetadata(
            directory, "app.cfg", filename, application))
    {
        TitanApplications_SetSasState(application, 1, 0);
        application->metadata_source = TITAN_APPLICATION_METADATA_APP_CFG;
        TitanApplications_ApplySasStructure(directory, application);
        return;
    }
    if (TitanApplications_FolderPrefixMetadata(directory, application))
        TitanApplications_ApplySasStructure(directory, application);
}

static int TitanApplications_ContainsPath(const char *path)
{
    unsigned int index;

    for (index = 0u; index < g_application_count; index++)
    {
        if (TitanApplications_NameEqualsInsensitive(g_applications[index].path, path))
            return 1;
    }
    return 0;
}

static int TitanApplications_PathJoin(
    char *output,
    unsigned int capacity,
    const char *root,
    const char *name
)
{
    int length;

    if (output == 0 || capacity == 0u || root == 0 || name == 0)
        return 0;
    length = snprintf(output, capacity, "%s/%s", root, name);
    return length >= 0 && (unsigned int)length < capacity;
}

static int TitanApplications_IsCatalogEntryValid(const TitanApplication *application)
{
    if (application == 0 ||
        memchr(application->title, '\0', sizeof(application->title)) == 0 ||
        memchr(application->path, '\0', sizeof(application->path)) == 0 ||
        application->title[0] == '\0' || application->path[0] == '\0')
        return 0;

    if (application->type == TITAN_APPLICATION_TITAN_NATIVE ||
        application->type == TITAN_APPLICATION_TITAN_GAME)
    {
        return application->installed &&
            application->api_version == TITAN_APPLICATION_API_VERSION &&
            TitanApplications_HasTmodExtension(application->path);
    }
    if (application->type == TITAN_APPLICATION_SYSTEM_BROWSER)
    {
        return application->installed &&
            application->api_version == 0u &&
            strcmp(application->path, "@system-browser") == 0;
    }
    return TitanApplications_HasElfExtension(application->path) &&
        !TitanApplications_IsInternalElf(application->path);
}

static void TitanApplications_AddExternal(
    const char *path,
    const char *filename,
    const char *title_override
)
{
    TitanApplication *application;

    if (path == 0 || filename == 0 ||
        g_application_count >= TITAN_APPLICATION_CAPACITY ||
        TitanApplications_IsInternalElf(path) ||
        TitanApplications_ContainsPath(path) ||
        TitanElf_ValidateFile(path) != TITAN_ELF_VALID)
        return;

    application = &g_applications[g_application_count];
    memset(application, 0, sizeof(*application));
    application->type = TITAN_APPLICATION_EXTERNAL_ELF;
    application->classification = TITAN_APPLICATION_CLASS_UNKNOWN_LEGACY;
    application->metadata_source = TITAN_APPLICATION_METADATA_NAME_HEURISTIC;
    TitanApplications_CopyText(application->path, sizeof(application->path), path);
    if (title_override != 0 && title_override[0] != '\0')
        TitanApplications_CopyText(application->title, sizeof(application->title), title_override);
    else
        TitanApplications_BuildTitle(application->title, sizeof(application->title), filename);
    TitanApplications_ResolveLegacyMetadata(path, filename, application);
    g_application_count++;
}

static int TitanApplications_ParseUnsigned(const char *text, unsigned int *value_out)
{
    unsigned int value;

    if (text == 0 || text[0] == '\0' || value_out == 0)
        return 0;
    value = 0u;
    while (*text != '\0')
    {
        if (*text < '0' || *text > '9' || value > 1000000u)
            return 0;
        value = value * 10u + (unsigned int)(*text - '0');
        text++;
    }
    *value_out = value;
    return 1;
}

int TitanApplications_ReadInstalledManifest(
    const char *directory_path,
    TitanApplication *application
)
{
    char manifest_path[TITAN_APPLICATION_PATH_CAPACITY];
    char entrypoint[128];
    char title[TITAN_APPLICATION_TITLE_CAPACITY];
    char title_id[TITAN_APPLICATION_TITLE_ID_CAPACITY];
    char version[TITAN_APPLICATION_VERSION_CAPACITY];
    char author[TITAN_APPLICATION_AUTHOR_CAPACITY];
    char description[TITAN_APPLICATION_DESCRIPTION_CAPACITY];
    char icon[128];
    char tile[128];
    char background[128];
    char type_name[32];
    char line[320];
    FILE *manifest;
    unsigned int format;
    unsigned int api_version;
    int format_valid;
    unsigned int directory_length;
    TitanApplicationType type;

    if (application != 0)
        memset(application, 0, sizeof(*application));
    if (directory_path == 0 || application == 0)
        return 0;
    directory_length = (unsigned int)strlen(directory_path);
    if (directory_length + sizeof("/" TITAN_APPLICATION_MANIFEST_NAME) > sizeof(manifest_path))
        return 0;
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s", directory_path, TITAN_APPLICATION_MANIFEST_NAME);
    manifest = fopen(manifest_path, "rb");
    if (manifest == 0)
        return 0;

    memset(entrypoint, 0, sizeof(entrypoint));
    memset(title, 0, sizeof(title));
    memset(title_id, 0, sizeof(title_id));
    memset(version, 0, sizeof(version));
    memset(author, 0, sizeof(author));
    memset(description, 0, sizeof(description));
    memset(icon, 0, sizeof(icon));
    memset(tile, 0, sizeof(tile));
    memset(background, 0, sizeof(background));
    memset(type_name, 0, sizeof(type_name));
    format = 0u;
    api_version = 0u;
    format_valid = 0;

    while (fgets(line, sizeof(line), manifest) != 0)
    {
        char *newline;

        newline = strpbrk(line, "\r\n");
        if (newline != 0) *newline = '\0';
        if (strncmp(line, "format=", 7u) == 0)
        {
            format_valid = TitanApplications_ParseUnsigned(line + 7u, &format) &&
                (format == 1u || format == 2u);
        }
        else if (strncmp(line, "title=", 6u) == 0)
            TitanApplications_CopyText(title, sizeof(title), line + 6u);
        else if (strncmp(line, "title_id=", 9u) == 0)
            TitanApplications_CopyText(title_id, sizeof(title_id), line + 9u);
        else if (strncmp(line, "version=", 8u) == 0)
            TitanApplications_CopyText(version, sizeof(version), line + 8u);
        else if (strncmp(line, "author=", 7u) == 0)
            TitanApplications_CopyText(author, sizeof(author), line + 7u);
        else if (strncmp(line, "description=", 12u) == 0)
            TitanApplications_CopyText(description, sizeof(description), line + 12u);
        else if (strncmp(line, "icon=", 5u) == 0)
            TitanApplications_CopyText(icon, sizeof(icon), line + 5u);
        else if (strncmp(line, "tile=", 5u) == 0)
            TitanApplications_CopyText(tile, sizeof(tile), line + 5u);
        else if (strncmp(line, "background=", 11u) == 0)
            TitanApplications_CopyText(background, sizeof(background), line + 11u);
        else if (strncmp(line, "type=", 5u) == 0)
            TitanApplications_CopyText(type_name, sizeof(type_name), line + 5u);
        else if (strncmp(line, "api=", 4u) == 0)
            (void)TitanApplications_ParseUnsigned(line + 4u, &api_version);
        else if (strncmp(line, "entrypoint=", 11u) == 0)
            TitanApplications_CopyText(entrypoint, sizeof(entrypoint), line + 11u);
        else if (strncmp(line, "executable=", 11u) == 0)
            TitanApplications_CopyText(entrypoint, sizeof(entrypoint), line + 11u);
    }
    fclose(manifest);

    /* 0 means no manifest; -1 means a manifest exists but must be quarantined. */
    if (!format_valid || title[0] == '\0' || entrypoint[0] == '\0' ||
        !TitanApplications_RootEntrySafe(entrypoint))
        return -1;

    if (format == 1u)
    {
        type = TITAN_APPLICATION_EXTERNAL_ELF;
        api_version = 0u;
        if (!TitanApplications_HasElfExtension(entrypoint))
            return -1;
    }
    else
    {
        if (strcmp(type_name, "titan-native") == 0)
            type = TITAN_APPLICATION_TITAN_NATIVE;
        else if (strcmp(type_name, "titan-game") == 0)
            type = TITAN_APPLICATION_TITAN_GAME;
        else if (strcmp(type_name, "system-browser") == 0)
            type = TITAN_APPLICATION_SYSTEM_BROWSER;
        else if (strcmp(type_name, "elf") == 0)
            type = TITAN_APPLICATION_EXTERNAL_ELF;
        else
            return -1;
        if (((type == TITAN_APPLICATION_TITAN_NATIVE ||
              type == TITAN_APPLICATION_TITAN_GAME) &&
             (!TitanApplications_HasTmodExtension(entrypoint) ||
              api_version != TITAN_APPLICATION_API_VERSION)) ||
            (type == TITAN_APPLICATION_SYSTEM_BROWSER &&
             (strcmp(entrypoint, "@system-browser") != 0 || api_version != 0u)) ||
            (type == TITAN_APPLICATION_EXTERNAL_ELF &&
             !TitanApplications_HasElfExtension(entrypoint)))
            return -1;
    }

    application->type = type;
    application->api_version = api_version;
    application->metadata_source = TITAN_APPLICATION_METADATA_TITAN_MANIFEST;
    if (type == TITAN_APPLICATION_TITAN_GAME)
        application->classification = TITAN_APPLICATION_CLASS_NATIVE_GAME;
    else if (type == TITAN_APPLICATION_TITAN_NATIVE ||
             type == TITAN_APPLICATION_SYSTEM_BROWSER)
        application->classification = TITAN_APPLICATION_CLASS_NATIVE;
    else
        application->classification = TITAN_APPLICATION_CLASS_UNKNOWN_LEGACY;
    application->installed = 1;
    TitanApplications_CopyText(application->title, sizeof(application->title), title);
    TitanApplications_CopyText(application->title_id, sizeof(application->title_id), title_id);
    TitanApplications_CopyText(application->version, sizeof(application->version), version);
    TitanApplications_CopyText(application->author, sizeof(application->author), author);
    TitanApplications_CopyText(application->description, sizeof(application->description), description);
    TitanApplications_CopyText(application->root_path, sizeof(application->root_path), directory_path);
    if (type == TITAN_APPLICATION_SYSTEM_BROWSER)
        TitanApplications_CopyText(application->path, sizeof(application->path), "@system-browser");
    else if (!TitanApplications_PathJoin(application->path, sizeof(application->path), directory_path, entrypoint))
        return -1;
    if (icon[0] != '\0' && TitanApplications_RootEntrySafe(icon))
        (void)TitanApplications_PathJoin(application->icon_path, sizeof(application->icon_path), directory_path, icon);
    if (tile[0] != '\0' && TitanApplications_RootEntrySafe(tile))
        (void)TitanApplications_PathJoin(application->tile_path, sizeof(application->tile_path), directory_path, tile);
    else
        (void)TitanApplications_PathJoin(application->tile_path, sizeof(application->tile_path), directory_path, "tile.rgb565");
    if (background[0] != '\0' && TitanApplications_RootEntrySafe(background))
        (void)TitanApplications_PathJoin(application->background_path, sizeof(application->background_path), directory_path, background);

    if ((type == TITAN_APPLICATION_EXTERNAL_ELF &&
         TitanElf_ValidateFile(application->path) != TITAN_ELF_VALID) ||
        (type == TITAN_APPLICATION_TITAN_NATIVE &&
         !TitanAppRuntime_ValidateModuleFile(application->path)) ||
        (type == TITAN_APPLICATION_TITAN_GAME &&
         !TitanAppRuntime_ValidateGameModuleFile(application->path)))
    {
        memset(application, 0, sizeof(*application));
        return -1;
    }
    return 1;
}

static int TitanApplications_AddInstalledManifest(const char *directory_path)
{
    TitanApplication application;
    int result;

    result = TitanApplications_ReadInstalledManifest(directory_path, &application);
    if (result <= 0)
        return result < 0;
    if (g_application_count < TITAN_APPLICATION_CAPACITY &&
        !TitanApplications_ContainsPath(application.path))
        g_applications[g_application_count++] = application;
    return 1;
}

static void TitanApplications_ScanDirectory(const char *directory_path, unsigned int depth)
{
    DIR *directory;

    if (directory_path == 0 || depth > TITAN_APPLICATION_MAX_SCAN_DEPTH ||
        g_application_count >= TITAN_APPLICATION_CAPACITY)
        return;
    if (TitanApplications_AddInstalledManifest(directory_path))
        return;

    directory = opendir(directory_path);
    if (directory == 0)
        return;
    while (g_application_count < TITAN_APPLICATION_CAPACITY)
    {
        struct dirent *entry;
        char path[TITAN_APPLICATION_PATH_CAPACITY];
        struct stat information;
        unsigned int directory_length;
        unsigned int name_length;

        entry = readdir(directory);
        if (entry == 0) break;
        if (entry->d_name[0] == '\0' || strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        directory_length = (unsigned int)strlen(directory_path);
        name_length = (unsigned int)strlen(entry->d_name);
        if (directory_length + name_length + 2u > sizeof(path))
            continue;
        strcpy(path, directory_path);
        if (directory_length > 0u && path[directory_length - 1u] != '/')
            strcat(path, "/");
        strcat(path, entry->d_name);
        if (stat(path, &information) < 0)
            continue;
        if (S_ISDIR(information.st_mode))
        {
            if (depth < TITAN_APPLICATION_MAX_SCAN_DEPTH &&
                !TitanApplications_ShouldSkipDirectory(entry->d_name))
                TitanApplications_ScanDirectory(path, depth + 1u);
            continue;
        }
        if (TitanApplications_HasElfExtension(entry->d_name))
            TitanApplications_AddExternal(path, entry->d_name, 0);
    }
    closedir(directory);
}

static void TitanApplications_Sort(void)
{
    unsigned int left;
    for (left = 1u; left < g_application_count; left++)
    {
        TitanApplication current;
        unsigned int right;

        current = g_applications[left];
        right = left;
        while (right > 0u && strcmp(g_applications[right - 1u].title, current.title) > 0)
        {
            g_applications[right] = g_applications[right - 1u];
            right--;
        }
        g_applications[right] = current;
    }
}

void TitanApplications_Initialize(void)
{
    memset(g_applications, 0, sizeof(g_applications));
    g_application_count = 0u;
    g_catalog_generation = 1u;
}

void TitanApplications_ReplaceCatalog(const TitanApplication *applications, unsigned int count)
{
    unsigned int index;

    if (count > TITAN_APPLICATION_CAPACITY)
        count = TITAN_APPLICATION_CAPACITY;
    memset(g_applications, 0, sizeof(g_applications));
    g_application_count = 0u;
    for (index = 0u; applications != 0 && index < count; index++)
    {
        if (!TitanApplications_IsCatalogEntryValid(&applications[index]) ||
            TitanApplications_ContainsPath(applications[index].path))
            continue;
        g_applications[g_application_count++] = applications[index];
    }
    TitanApplications_Sort();
    g_catalog_generation = g_catalog_generation == 0xffffffffu ? 1u : g_catalog_generation + 1u;
}

int TitanApplications_AddInstalledDirectory(const char *directory_path)
{
    int result;
    if (directory_path == 0 || directory_path[0] == '\0') return 0;
    result = TitanApplications_AddInstalledManifest(directory_path);
    if (result > 0)
    {
        TitanApplications_Sort();
        g_catalog_generation = g_catalog_generation == 0xffffffffu ? 1u : g_catalog_generation + 1u;
    }
    return result > 0;
}

unsigned int TitanApplications_RefreshFromRoot(const char *root_path)
{
    if (root_path == 0 || root_path[0] == '\0')
        return 0u;
    TitanApplications_ScanDirectory(root_path, 0u);
    TitanApplications_Sort();
    return g_application_count;
}

unsigned int TitanApplications_Refresh(void)
{
    g_application_count = 0u;
    memset(g_applications, 0, sizeof(g_applications));
    TitanApplications_RefreshFromRoot("mass:/APPS");
    TitanApplications_RefreshFromRoot("mass:/apps");
    TitanApplications_RefreshFromRoot("mass:/TITAN/apps");
    TitanApplications_RefreshFromRoot("mass:/BOOT");
    TitanApplications_RefreshFromRoot("mass:/boot");
    TitanApplications_RefreshFromRoot("mass:/");
    if (g_application_count == 0u)
    {
        TitanApplications_RefreshFromRoot("mass0:/APPS");
        TitanApplications_RefreshFromRoot("mass0:/apps");
        TitanApplications_RefreshFromRoot("mass0:/TITAN/apps");
        TitanApplications_RefreshFromRoot("mass0:/BOOT");
        TitanApplications_RefreshFromRoot("mass0:/boot");
        TitanApplications_RefreshFromRoot("mass0:/");
    }
    TitanApplications_Sort();
    g_catalog_generation = g_catalog_generation == 0xffffffffu ? 1u : g_catalog_generation + 1u;
    return g_application_count;
}

unsigned int TitanApplications_GetCount(void)
{
    return g_application_count;
}

unsigned int TitanApplications_GetCatalogGeneration(void)
{
    return g_catalog_generation;
}

const TitanApplication *TitanApplications_Get(unsigned int index)
{
    return index < g_application_count ? &g_applications[index] : 0;
}

int TitanApplications_IsSystemBrowser(unsigned int index)
{
    const TitanApplication *application;

    application = TitanApplications_Get(index);
    return application != 0 &&
        application->type == TITAN_APPLICATION_SYSTEM_BROWSER &&
        strcmp(application->path, "@system-browser") == 0;
}

unsigned int TitanApplications_GetHomeTileCount(void)
{
    unsigned int count;
    unsigned int index;

    count = 0u;
    for (index = 0u; index < g_application_count; index++)
    {
        if (g_applications[index].installed)
            count++;
    }
    return count;
}

const TitanApplication *TitanApplications_GetHomeTile(
    unsigned int visible_index,
    unsigned int *application_index
)
{
    unsigned int index;
    unsigned int visible;

    visible = 0u;
    for (index = 0u; index < g_application_count; index++)
    {
        if (!g_applications[index].installed)
            continue;
        if (visible == visible_index)
        {
            if (application_index != 0) *application_index = index;
            return &g_applications[index];
        }
        visible++;
    }
    return 0;
}

int TitanApplications_IsNative(unsigned int index)
{
    const TitanApplication *application = TitanApplications_Get(index);
    return application != 0 && application->type == TITAN_APPLICATION_TITAN_NATIVE;
}

int TitanApplications_IsGame(unsigned int index)
{
    const TitanApplication *application = TitanApplications_Get(index);
    return application != 0 && application->type == TITAN_APPLICATION_TITAN_GAME;
}

const char *TitanApplications_GetClassificationName(
    TitanApplicationClassification classification
)
{
    switch (classification)
    {
        case TITAN_APPLICATION_CLASS_NATIVE: return "Titan Native";
        case TITAN_APPLICATION_CLASS_NATIVE_GAME: return "Titan Native Game";
        case TITAN_APPLICATION_CLASS_SAS: return "Titan SAS";
        case TITAN_APPLICATION_CLASS_UL_ENGINE: return "Titan UL Engine";
        case TITAN_APPLICATION_CLASS_NEUTRINO_ENGINE: return "Titan-Neutrino Engine";
        case TITAN_APPLICATION_CLASS_DISC_ENGINE: return "Titan Disc Engine";
        default: return "Unknown / Legacy";
    }
}

const char *TitanApplications_GetMetadataSourceName(
    TitanApplicationMetadataSource source
)
{
    switch (source)
    {
        case TITAN_APPLICATION_METADATA_TITAN_MANIFEST: return "titan_app.ini";
        case TITAN_APPLICATION_METADATA_APP_CFG: return "app.cfg";
        case TITAN_APPLICATION_METADATA_TITLE_CFG: return "title.cfg";
        case TITAN_APPLICATION_METADATA_FOLDER_PREFIX: return "folder-prefix";
        default: return "name-heuristic";
    }
}

TitanApplicationExecutionModel TitanApplications_GetExecutionModel(unsigned int index)
{
    const TitanApplication *application = TitanApplications_Get(index);
    if (application == 0)
        return TITAN_APPLICATION_EXECUTION_EXTERNAL_HANDOFF;
    if (application->type == TITAN_APPLICATION_TITAN_NATIVE ||
        application->type == TITAN_APPLICATION_TITAN_GAME)
        return TITAN_APPLICATION_EXECUTION_NATIVE_IN_PROCESS;
    if (application->type == TITAN_APPLICATION_SYSTEM_BROWSER)
        return TITAN_APPLICATION_EXECUTION_SYSTEM_SERVICE;
    return TITAN_APPLICATION_EXECUTION_EXTERNAL_HANDOFF;
}

const char *TitanApplications_GetExecutionModelName(TitanApplicationExecutionModel model)
{
    switch (model)
    {
        case TITAN_APPLICATION_EXECUTION_NATIVE_IN_PROCESS: return "Native in-process";
        case TITAN_APPLICATION_EXECUTION_SYSTEM_SERVICE: return "System service";
        default: return "External ELF handoff";
    }
}

int TitanApplications_IsSasStateValid(const TitanApplication *application)
{
    if (application == 0)
        return 0;
    return application->sas_compliant == 0u || application->sas_compatible != 0u;
}

int TitanApplications_IsSasCompatible(unsigned int index)
{
    const TitanApplication *application = TitanApplications_Get(index);
    return application != 0 &&
        application->classification == TITAN_APPLICATION_CLASS_SAS &&
        application->sas_compatible != 0u;
}

int TitanApplications_IsSasCompliant(unsigned int index)
{
    const TitanApplication *application = TitanApplications_Get(index);
    return application != 0 && application->sas_compliant != 0u &&
        application->sas_compatible != 0u;
}

int TitanApplications_IsSas(unsigned int index)
{
    return TitanApplications_IsSasCompatible(index);
}

int TitanApplications_IsInstalled(unsigned int index)
{
    const TitanApplication *application = TitanApplications_Get(index);
    return application != 0 && application->installed;
}

TitanApplicationResult TitanApplications_Launch(unsigned int index)
{
    const TitanApplication *application;

    application = TitanApplications_Get(index);
    if (application == 0)
        return TITAN_APPLICATION_INVALID_INDEX;

    if (application->type == TITAN_APPLICATION_TITAN_NATIVE ||
        application->type == TITAN_APPLICATION_TITAN_GAME)
    {
        if (application->api_version != TITAN_APPLICATION_API_VERSION)
            return TITAN_APPLICATION_API_MISMATCH;
        if (TitanAppRuntime_OwnsShell())
            return TITAN_APPLICATION_RUNTIME_BUSY;
        if (!TitanAppRuntime_Open(
                application->title,
                application->root_path,
                application->path,
                application->background_path,
                application->type == TITAN_APPLICATION_TITAN_GAME))
            return TITAN_APPLICATION_INVALID_TMOD;
        return TITAN_APPLICATION_OK;
    }

    if (TitanElf_ValidateFile(application->path) != TITAN_ELF_VALID)
        return TITAN_APPLICATION_INVALID_ELF;
    {
        char *argv[2];
        TitanExecHandoffResult handoff;

        argv[0] = (char *)application->path;
        argv[1] = 0;
        handoff = TitanExecHandoff_Run(1, argv);
        if (handoff != TITAN_EXEC_HANDOFF_EXEC_RETURNED)
            return TITAN_APPLICATION_HANDOFF_FAILED;
    }
    return TITAN_APPLICATION_EXEC_FAILED;
}

int TitanApplications_GetUninstallTarget(
    unsigned int index,
    char *root_path,
    unsigned int root_capacity,
    char *title_id,
    unsigned int title_id_capacity,
    char *title,
    unsigned int title_capacity
)
{
    const TitanApplication *application;

    application = TitanApplications_Get(index);
    if (application == 0 || !application->installed ||
        !TitanApplications_IsInstalledRoot(application->root_path) ||
        application->title_id[0] == '\0')
        return 0;

    TitanApplications_CopyText(root_path, root_capacity, application->root_path);
    TitanApplications_CopyText(title_id, title_id_capacity, application->title_id);
    TitanApplications_CopyText(title, title_capacity, application->title);
    return 1;
}

int TitanApplications_RemoveInstalledFromCatalog(const char *title_id)
{
    unsigned int index;

    if (title_id == 0 || title_id[0] == '\0')
        return 0;
    for (index = 0u; index < g_application_count; index++)
    {
        unsigned int tail;

        if (!g_applications[index].installed ||
            strcmp(g_applications[index].title_id, title_id) != 0)
            continue;
        for (tail = index + 1u; tail < g_application_count; tail++)
            g_applications[tail - 1u] = g_applications[tail];
        g_application_count--;
        memset(&g_applications[g_application_count], 0, sizeof(g_applications[0]));
        return 1;
    }
    return 0;
}

const char *TitanApplications_GetResultName(TitanApplicationResult result)
{
    switch (result)
    {
        case TITAN_APPLICATION_NOT_FOUND: return TitanLocalization_Compat("Aplicativo nao encontrado");
        case TITAN_APPLICATION_INVALID_INDEX: return TitanLocalization_Compat("Selecao de aplicativo invalida");
        case TITAN_APPLICATION_INVALID_ELF: return TitanLocalization_Compat("Executavel ELF invalido");
        case TITAN_APPLICATION_INVALID_TMOD: return TitanLocalization_Compat("Modulo Titan App invalido ou incompativel");
        case TITAN_APPLICATION_API_MISMATCH: return "Versao da Titan App API incompativel";
        case TITAN_APPLICATION_RUNTIME_BUSY: return "Outro Titan App ja esta em execucao";
        case TITAN_APPLICATION_HANDOFF_FAILED: return TitanLocalization_Compat("Falha no handoff do aplicativo");
        case TITAN_APPLICATION_EXEC_FAILED: return TitanLocalization_Compat("O aplicativo retornou sem iniciar");
        case TITAN_APPLICATION_NOT_INSTALLED: return TitanLocalization_Compat("Aplicativo nao foi instalado pelo Titan");
        case TITAN_APPLICATION_UNINSTALL_DENIED: return TitanLocalization_Compat("Desinstalacao nao permitida");
        case TITAN_APPLICATION_OK:
        default: return TitanLocalization_Compat("Pronto");
    }
}

#include "titan_shell_catalog.h"
#include "titan_version.h"


static const TitanShellItem g_titan_dex_items[] =
{
    {
        "★ APP_HOME",
        "Development application staging area",
        "Developer application launcher",
        "TITN00100",
        TITAN_KERNEL_VERSION_STRING,
        TITAN_ICON_APP_HOME,
        TITAN_SHELL_PAGE_APP_HOME
    },
    {
        "★ FTP_HOME",
        "Transfer activity shortcut (experimental)",
        "Activity view; FTP server not included",
        "TITN00101",
        TITAN_KERNEL_VERSION_STRING,
        TITAN_ICON_FTP_HOME,
        TITAN_SHELL_PAGE_FTP_HOME
    }
};

static const TitanShellItem g_titan_shell_items[] =
{
    {
        "Overview",
        "A modern native shell for PlayStation 2",
        "Independent, modular and console-first",
        "TITN00009",
        TITAN_KERNEL_VERSION_STRING,
        TITAN_ICON_OVERVIEW,
        TITAN_SHELL_PAGE_OVERVIEW
    },
    {
        "Library",
        "Browse all content available on this PlayStation 2",
        "Games, folders and applications beyond the Home limit",
        "TITN00001",
        TITAN_KERNEL_VERSION_STRING,
        TITAN_ICON_LIBRARY,
        TITAN_SHELL_PAGE_LIBRARY
    }
};

static const TitanShellItem g_titan_system_items[] =
{
    {
        "Notifications",
        "System alerts, update notices and activity",
        "Activities, alerts and system history",
        "TITN00004",
        TITAN_KERNEL_VERSION_STRING,
        TITAN_ICON_NOTIFICATIONS,
        TITAN_SHELL_PAGE_NOTIFICATIONS
    },
    {
        "Profile",
        "Select a user and its exclusive VMC",
        "Runtime profile service",
        "TITN00005",
        TITAN_KERNEL_VERSION_STRING,
        TITAN_ICON_USER,
        TITAN_SHELL_PAGE_USER_PROFILE
    },
    {
        "VMC Utility",
        "Manage virtual memory cards and related tools",
        "VMC utility foundation",
        "TITN00006",
        TITAN_KERNEL_VERSION_STRING,
        TITAN_ICON_VMC_UTILITY,
        TITAN_SHELL_PAGE_SAVED_DATA
    },
    {
        "Settings",
        "Users, video, system and About",
        "Project Titan system configuration",
        "TITN00007",
        TITAN_KERNEL_VERSION_STRING,
        TITAN_ICON_SETTINGS,
        TITAN_SHELL_PAGE_SETTINGS
    },
    {
        "Power",
        "Power options for the Project Titan shell",
        "Power off, restart Titan or return to the PS2 Browser",
        "TITN00008",
        TITAN_KERNEL_VERSION_STRING,
        TITAN_ICON_POWER,
        TITAN_SHELL_PAGE_POWER
    }
};

unsigned int TitanShellCatalog_GetCount(void)
{
    return (unsigned int)(
        sizeof(g_titan_shell_items) /
        sizeof(g_titan_shell_items[0])
    );
}

const TitanShellItem *TitanShellCatalog_Get(
    unsigned int index
)
{
    if (index >= TitanShellCatalog_GetCount())
    {
        return 0;
    }

    return &g_titan_shell_items[index];
}


unsigned int TitanShellCatalog_GetDexCount(void)
{
    return (unsigned int)(
        sizeof(g_titan_dex_items) /
        sizeof(g_titan_dex_items[0])
    );
}

const TitanShellItem *TitanShellCatalog_GetDex(unsigned int index)
{
    if (index >= TitanShellCatalog_GetDexCount())
    {
        return 0;
    }

    return &g_titan_dex_items[index];
}

unsigned int TitanShellCatalog_GetSystemCount(void)
{
    return (unsigned int)(
        sizeof(g_titan_system_items) /
        sizeof(g_titan_system_items[0])
    );
}

const TitanShellItem *TitanShellCatalog_GetSystem(
    unsigned int index
)
{
    if (index >= TitanShellCatalog_GetSystemCount())
    {
        return 0;
    }

    return &g_titan_system_items[index];
}

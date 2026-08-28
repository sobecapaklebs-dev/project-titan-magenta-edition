#ifndef PROJECT_TITAN_LIBRARY_CACHE_H
#define PROJECT_TITAN_LIBRARY_CACHE_H

#include "titan_applications.h"
#include "titan_neutrino.h"

int TitanLibraryCache_Load(
    TitanNeutrinoGame *games,
    unsigned int *game_count,
    TitanApplication *applications,
    unsigned int *application_count,
    char *neutrino_elf,
    unsigned int neutrino_capacity
);

int TitanLibraryCache_Save(
    TitanNeutrinoGame *games,
    unsigned int game_count,
    TitanApplication *applications,
    unsigned int application_count,
    const char *neutrino_elf
);

int TitanLibraryCache_Clear(void);

#endif

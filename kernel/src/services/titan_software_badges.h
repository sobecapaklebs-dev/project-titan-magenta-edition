#ifndef PROJECT_TITAN_SOFTWARE_BADGES_H
#define PROJECT_TITAN_SOFTWARE_BADGES_H

#include <stdint.h>

#include "titan_applications.h"
#include "titan_pikavr.h"
#include "titan_vlombiscore.h"

#define TITAN_SOFTWARE_BADGE_LABEL_CAPACITY 32u

typedef struct TitanTechnologyEvidence
{
    int pikavr_path_active;
    TitanPikaVrTelemetry pikavr;
    uint32_t vlombis_capabilities_active;
    uint32_t vlombis_measured_events;
} TitanTechnologyEvidence;

typedef struct TitanSoftwareBadgeState
{
    TitanApplicationClassification classification;
    char classification_label[TITAN_SOFTWARE_BADGE_LABEL_CAPACITY];
    int show_pikavr;
    int show_vlombiscore;
} TitanSoftwareBadgeState;

void TitanSoftwareBadges_EvaluateRuntime(
    const TitanApplication *application,
    TitanSoftwareBadgeState *badges
);

void TitanSoftwareBadges_Evaluate(
    const TitanApplication *application,
    const TitanTechnologyEvidence *evidence,
    TitanSoftwareBadgeState *badges
);

#endif

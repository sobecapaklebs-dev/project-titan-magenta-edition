#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "titan_software_badges.h"

static TitanPikaVrTelemetry g_runtime_pikavr;
static TitanVlombisTelemetry g_runtime_vlombis;
static int g_runtime_pikavr_active;
int TitanPikaVr_IsOptimizedPathActive(void) { return g_runtime_pikavr_active; }
void TitanPikaVr_GetTelemetry(TitanPikaVrTelemetry *t) { if (t) *t = g_runtime_pikavr; }
void TitanVlombisCore_GetTelemetry(TitanVlombisTelemetry *t) { if (t) *t = g_runtime_vlombis; }

int main(void)
{
    TitanApplication application;
    TitanTechnologyEvidence evidence;
    TitanSoftwareBadgeState badges;

    memset(&application, 0, sizeof(application));
    memset(&evidence, 0, sizeof(evidence));
    application.classification = TITAN_APPLICATION_CLASS_NATIVE_GAME;

    TitanSoftwareBadges_Evaluate(&application, &evidence, &badges);
    assert(strcmp(badges.classification_label, "Titan Native Game") == 0);
    assert(!badges.show_pikavr);
    assert(!badges.show_vlombiscore);

    /* Native Game classification must never imply pikaVR. */
    evidence.pikavr_path_active = 1;
    TitanSoftwareBadges_Evaluate(&application, &evidence, &badges);
    assert(!badges.show_pikavr);

    evidence.pikavr.cache_misses = 1u;
    TitanSoftwareBadges_Evaluate(&application, &evidence, &badges);
    assert(badges.show_pikavr);

    evidence.pikavr_path_active = 0;
    TitanSoftwareBadges_Evaluate(&application, &evidence, &badges);
    assert(!badges.show_pikavr);

    /* Vlombis capability declarations without measured participation do not
     * earn a badge. */
    evidence.vlombis_capabilities_active = TITAN_VLOMBIS_CAP_IO_COORDINATION;
    evidence.vlombis_measured_events = 0u;
    TitanSoftwareBadges_Evaluate(&application, &evidence, &badges);
    assert(!badges.show_vlombiscore);
    evidence.vlombis_measured_events = 3u;
    TitanSoftwareBadges_Evaluate(&application, &evidence, &badges);
    assert(badges.show_vlombiscore);

    application.classification = TITAN_APPLICATION_CLASS_SAS;
    memset(&g_runtime_pikavr, 0, sizeof(g_runtime_pikavr));
    memset(&g_runtime_vlombis, 0, sizeof(g_runtime_vlombis));
    g_runtime_pikavr_active = 1;
    g_runtime_pikavr.draw_calls = 2u;
    g_runtime_vlombis.capabilities_active = TITAN_VLOMBIS_CAP_IO_COORDINATION;
    g_runtime_vlombis.measured_events = 3u;
    TitanSoftwareBadges_EvaluateRuntime(&application, &badges);
    assert(badges.show_pikavr);
    assert(badges.show_vlombiscore);

    TitanSoftwareBadges_Evaluate(&application, 0, &badges);
    assert(strcmp(badges.classification_label, "Titan SAS") == 0);
    assert(!badges.show_pikavr);
    assert(!badges.show_vlombiscore);

    puts("titan-software-badges: OK truthful classification and technology evidence");
    return 0;
}

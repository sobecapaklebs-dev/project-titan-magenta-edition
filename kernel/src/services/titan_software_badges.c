#include "titan_software_badges.h"

#include <limits.h>
#include <string.h>

static uint64_t TitanSoftwareBadges_PikaVrActivity(
    const TitanPikaVrTelemetry *telemetry
)
{
    if (telemetry == 0)
        return 0u;
    return (uint64_t)telemetry->cache_hits +
        telemetry->cache_misses +
        telemetry->sampler_state_hits +
        telemetry->sampler_state_misses +
        telemetry->textures_palettized +
        telemetry->textures_deduplicated +
        telemetry->draw_calls +
        telemetry->material_passes +
        telemetry->path3_qwords +
        telemetry->ribbon_path1_qwords;
}

static void TitanSoftwareBadges_CopyLabel(
    char *destination,
    uint32_t capacity,
    const char *source
)
{
    uint32_t index = 0u;

    if (destination == 0 || capacity == 0u)
        return;
    while (source != 0 && source[index] != '\0' && index + 1u < capacity)
    {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

void TitanSoftwareBadges_Evaluate(
    const TitanApplication *application,
    const TitanTechnologyEvidence *evidence,
    TitanSoftwareBadgeState *badges
)
{
    TitanApplicationClassification classification;

    if (badges == 0)
        return;
    memset(badges, 0, sizeof(*badges));
    classification = application != 0
        ? application->classification
        : TITAN_APPLICATION_CLASS_UNKNOWN_LEGACY;
    badges->classification = classification;
    TitanSoftwareBadges_CopyLabel(
        badges->classification_label,
        sizeof(badges->classification_label),
        TitanApplications_GetClassificationName(classification)
    );
    if (evidence == 0)
        return;

    /* Branding never activates a technology badge. pikaVR must be the selected
     * optimized path and must have observed renderer/resource work in this
     * execution. */
    badges->show_pikavr = evidence->pikavr_path_active != 0 &&
        TitanSoftwareBadges_PikaVrActivity(&evidence->pikavr) != 0u;

    /* VlombisCore is an umbrella capability. A capability bit alone is not
     * sufficient: at least one measured event from an active capability must
     * be reported by the participating subsystem. */
    badges->show_vlombiscore =
        evidence->vlombis_capabilities_active != 0u &&
        evidence->vlombis_measured_events != 0u;
}

void TitanSoftwareBadges_EvaluateRuntime(
    const TitanApplication *application,
    TitanSoftwareBadgeState *badges
)
{
    TitanTechnologyEvidence evidence;
    TitanVlombisTelemetry vlombis;

    memset(&evidence, 0, sizeof(evidence));
    memset(&vlombis, 0, sizeof(vlombis));
    evidence.pikavr_path_active = TitanPikaVr_IsOptimizedPathActive();
    TitanPikaVr_GetTelemetry(&evidence.pikavr);
    TitanVlombisCore_GetTelemetry(&vlombis);
    evidence.vlombis_capabilities_active = vlombis.capabilities_active;
    evidence.vlombis_measured_events = vlombis.measured_events;
    TitanSoftwareBadges_Evaluate(application, &evidence, badges);
}

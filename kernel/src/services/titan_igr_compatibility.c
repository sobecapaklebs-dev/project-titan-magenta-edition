#include "titan_igr_compatibility.h"
#include <string.h>

static const TitanIgrProfileRule g_rules[] = {
    { "TITAN-NES", TITAN_SESSION_ENGINE_NATIVE, TITAN_IGR_SUPPORTED, TITAN_OVERLAY_FULL,
      TITAN_SESSION_CAP_SAFE_RETURN|TITAN_SESSION_CAP_OVERLAY_MINIMAL|TITAN_SESSION_CAP_GS_STATE_RESTORE|TITAN_SESSION_CAP_NATIVE_SAFEPOINT, 1u },
    { "TITAN-", TITAN_SESSION_ENGINE_NATIVE, TITAN_IGR_SUPPORTED_WITH_PROFILE, TITAN_OVERLAY_MINIMAL,
      TITAN_SESSION_CAP_SAFE_RETURN|TITAN_SESSION_CAP_OVERLAY_MINIMAL|TITAN_SESSION_CAP_GS_STATE_RESTORE, 2u }
};

static int Prefix(const char *s, const char *p)
{
    size_t n;
    if (!s || !p) return 0;
    n = strlen(p);
    return strncmp(s, p, n) == 0;
}

TitanIgrCompatibility TitanIgrCompatibility_Evaluate(
    const char *title_id, TitanSessionEngine engine, uint32_t caps,
    uint32_t requested_profile, TitanOverlayLevel *overlay_level,
    uint32_t *resolved_profile)
{
    unsigned int i;
    if (overlay_level) *overlay_level = TITAN_OVERLAY_DISABLED;
    if (resolved_profile) *resolved_profile = 0u;
    if (engine == TITAN_SESSION_ENGINE_NONE) return TITAN_IGR_INCOMPATIBLE;

    /* Physical-disc profiles are opt-in compatibility records. Profile 100 is
       RETURN_ONLY. 101/102 require progressively stronger resident-backend
       capabilities; missing capabilities fail closed rather than injecting. */
    if (engine == TITAN_SESSION_ENGINE_DISC && requested_profile >= 100u && requested_profile <= 102u) {
        uint32_t required = TITAN_SESSION_CAP_SAFE_RETURN;
        TitanOverlayLevel level = TITAN_OVERLAY_RETURN_ONLY;
        TitanIgrCompatibility compat = TITAN_IGR_SUPPORTED_WITH_PROFILE;
        if (requested_profile == 101u) {
            required |= TITAN_SESSION_CAP_OVERLAY_MINIMAL | TITAN_SESSION_CAP_GS_STATE_RESTORE;
            level = TITAN_OVERLAY_MINIMAL;
        } else if (requested_profile == 102u) {
            required |= TITAN_SESSION_CAP_OVERLAY_MINIMAL | TITAN_SESSION_CAP_GS_STATE_RESTORE | TITAN_SESSION_CAP_NATIVE_SAFEPOINT;
            level = TITAN_OVERLAY_FULL;
        }
        if ((caps & required) != required) return TITAN_IGR_INCOMPATIBLE;
        if (overlay_level) *overlay_level = level;
        if (resolved_profile) *resolved_profile = requested_profile;
        return compat;
    }

    for (i = 0; i < (unsigned int)(sizeof(g_rules)/sizeof(g_rules[0])); ++i) {
        const TitanIgrProfileRule *r = &g_rules[i];
        if (r->engine != engine || !Prefix(title_id, r->title_prefix)) continue;
        if ((caps & r->required_capabilities) != r->required_capabilities) return TITAN_IGR_INCOMPATIBLE;
        if (requested_profile != 0u && requested_profile != r->profile_id) return TITAN_IGR_INCOMPATIBLE;
        if (overlay_level) *overlay_level = r->max_overlay;
        if (resolved_profile) *resolved_profile = r->profile_id;
        return r->compatibility;
    }

    /* External engines are deliberately conservative until a title/profile is
       proven. UNKNOWN never opts into Titan injection or FULL overlay. */
    if (engine == TITAN_SESSION_ENGINE_NEUTRINO || engine == TITAN_SESSION_ENGINE_UL ||
        engine == TITAN_SESSION_ENGINE_ELF || engine == TITAN_SESSION_ENGINE_DISC)
    {
        if (engine == TITAN_SESSION_ENGINE_DISC && overlay_level)
            *overlay_level = TITAN_OVERLAY_RETURN_ONLY;
        return TITAN_IGR_UNKNOWN;
    }
    return TITAN_IGR_DISABLED;
}

#ifndef PROJECT_TITAN_IGR_COMPATIBILITY_H
#define PROJECT_TITAN_IGR_COMPATIBILITY_H
#include "titan_session_service.h"

typedef struct TitanIgrProfileRule {
    const char *title_prefix;
    TitanSessionEngine engine;
    TitanIgrCompatibility compatibility;
    TitanOverlayLevel max_overlay;
    uint32_t required_capabilities;
    uint32_t profile_id;
} TitanIgrProfileRule;

TitanIgrCompatibility TitanIgrCompatibility_Evaluate(
    const char *title_id,
    TitanSessionEngine engine,
    uint32_t runtime_capabilities,
    uint32_t requested_profile,
    TitanOverlayLevel *overlay_level,
    uint32_t *resolved_profile
);
#endif

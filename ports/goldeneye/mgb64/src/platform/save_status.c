/* save_status.c — see save_status.h. Pure, dependency-light mapping of a
 * config-save outcome to the settings status line + hard-failure predicate. The
 * em-dash is written as its UTF-8 bytes (\xE2\x80\x94) to match the rest of the
 * UI string literals and stay independent of source-charset assumptions. */
#include "save_status.h"

const char *saveStatusMessage(MgbConfigSaveResult r) {
    switch (r) {
        case MGB_CONFIG_SAVE_OK:
            return "Saved to ge007.ini";
        case MGB_CONFIG_SAVE_SUPPRESSED:
            return "Applied to this run only \xE2\x80\x94 faithful/remaster session doesn't persist";
        case MGB_CONFIG_SAVE_FAILED:
            return "Couldn't save \xE2\x80\x94 check permissions";
    }
    /* Defensive default: never NULL for an out-of-enum value. */
    return "";
}

int saveStatusIsFailure(MgbConfigSaveResult r) {
    /* Only a real write failure is a hard failure. The intentional faithful/
     * remaster SUPPRESSED no-write is a normal, non-error outcome. */
    return r == MGB_CONFIG_SAVE_FAILED;
}

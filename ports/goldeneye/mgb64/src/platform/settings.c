/**
 * settings.c -- declarative settings registry for native-port options.
 *
 * The existing INI backend still owns parsing and saving. This layer adds the
 * metadata needed for introspection, future CLI/env overrides, and the settings
 * UI without changing current runtime behavior.
 */

#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "config_pc.h"

static Setting s_settings[CONFIG_MAX_SETTINGS];
static s32 s_numSettings;

static Setting *settingsFindMutable(const char *key)
{
    s32 i;

    for (i = 0; i < s_numSettings; i++) {
        if (strcasecmp(s_settings[i].key, key) == 0) {
            return &s_settings[i];
        }
    }

    return NULL;
}

static Setting *settingsFindOrAdd(const char *key)
{
    Setting *setting = settingsFindMutable(key);

    if (setting != NULL) {
        return setting;
    }

    if (s_numSettings >= CONFIG_MAX_SETTINGS) {
        return NULL;
    }

    setting = &s_settings[s_numSettings++];
    memset(setting, 0, sizeof(*setting));
    setting->key = key;
    return setting;
}

void settingsRegisterInt(const char *key, s32 *var, s32 def, s32 min, s32 max,
                         SettingScope scope, const char *env, const char *cli,
                         const char *label, const char *help)
{
    Setting *setting = settingsFindOrAdd(key);

    if (setting != NULL) {
        setting->type = SETTING_TYPE_INT;
        setting->scope = scope;
        setting->ptr = var;
        setting->def.s32_value = def;
        setting->min.s32_value = min;
        setting->max.s32_value = max;
        setting->env = env;
        setting->cli = cli;
        setting->label = label;
        setting->help = help;
    }

    configRegisterInt(key, var, min, max);
}

void settingsRegisterUInt(const char *key, u32 *var, u32 def, u32 min, u32 max,
                          SettingScope scope, const char *env, const char *cli,
                          const char *label, const char *help)
{
    Setting *setting = settingsFindOrAdd(key);

    if (setting != NULL) {
        setting->type = SETTING_TYPE_UINT;
        setting->scope = scope;
        setting->ptr = var;
        setting->def.u32_value = def;
        setting->min.u32_value = min;
        setting->max.u32_value = max;
        setting->env = env;
        setting->cli = cli;
        setting->label = label;
        setting->help = help;
    }

    configRegisterUInt(key, var, min, max);
}

void settingsRegisterFloat(const char *key, f32 *var, f32 def, f32 min, f32 max,
                           SettingScope scope, const char *env, const char *cli,
                           const char *label, const char *help)
{
    Setting *setting = settingsFindOrAdd(key);

    if (setting != NULL) {
        setting->type = SETTING_TYPE_FLOAT;
        setting->scope = scope;
        setting->ptr = var;
        setting->def.f32_value = def;
        setting->min.f32_value = min;
        setting->max.f32_value = max;
        setting->env = env;
        setting->cli = cli;
        setting->label = label;
        setting->help = help;
    }

    configRegisterFloat(key, var, min, max);
}

void settingsRegisterEnum(const char *key, s32 *var, s32 def,
                          const ConfigEnumOption *options, s32 option_count,
                          SettingScope scope, const char *env, const char *cli,
                          const char *label, const char *help)
{
    Setting *setting = settingsFindOrAdd(key);

    if (setting != NULL) {
        setting->type = SETTING_TYPE_ENUM;
        setting->scope = scope;
        setting->ptr = var;
        setting->def.s32_value = def;
        setting->min.s32_value = 0;
        setting->max.s32_value = option_count > 0 ? option_count - 1 : 0;
        setting->enum_options = options;
        setting->enum_count = option_count;
        setting->env = env;
        setting->cli = cli;
        setting->label = label;
        setting->help = help;
    }

    configRegisterEnum(key, var, options, option_count);
}

void settingsRegisterString(const char *key, char *var, size_t capacity,
                            const char *def,
                            SettingScope scope, const char *env, const char *cli,
                            const char *label, const char *help)
{
    Setting *setting = settingsFindOrAdd(key);

    if (setting != NULL) {
        setting->type = SETTING_TYPE_STRING;
        setting->scope = scope;
        setting->ptr = var;
        setting->def_string = def;
        setting->string_capacity = capacity;
        setting->env = env;
        setting->cli = cli;
        setting->label = label;
        setting->help = help;
    }

    if (var && capacity > 0 && def) {
        snprintf(var, capacity, "%s", def);
    }

    configRegisterString(key, var, capacity);
}

s32 settingsCount(void)
{
    return s_numSettings;
}

const Setting *settingsAt(s32 index)
{
    if (index < 0 || index >= s_numSettings) {
        return NULL;
    }

    return &s_settings[index];
}

const Setting *settingsFind(const char *key)
{
    return settingsFindMutable(key);
}

const char *settingsTypeName(SettingType type)
{
    switch (type) {
        case SETTING_TYPE_INT:   return "int";
        case SETTING_TYPE_UINT:  return "uint";
        case SETTING_TYPE_FLOAT: return "float";
        case SETTING_TYPE_ENUM:  return "enum";
        case SETTING_TYPE_STRING:return "string";
        default:                 return "unknown";
    }
}

const char *settingsScopeName(SettingScope scope)
{
    switch (scope) {
        case SETTING_SCOPE_LIVE:    return "live";
        case SETTING_SCOPE_RESTART: return "restart";
        default:                    return "unknown";
    }
}

const char *settingsOverrideSourceName(SettingOverrideSource source)
{
    switch (source) {
        case SETTING_OVERRIDE_NONE:     return "none";
        case SETTING_OVERRIDE_ENV:      return "env";
        case SETTING_OVERRIDE_CLI:      return "cli";
        case SETTING_OVERRIDE_FAITHFUL: return "faithful";
        case SETTING_OVERRIDE_REMASTER: return "remaster";
        default:                        return "unknown";
    }
}

const char *settingsEnumTokenForValue(const Setting *setting, s32 value)
{
    if (setting != NULL) {
        for (s32 i = 0; i < setting->enum_count; i++) {
            if (setting->enum_options[i].value == value &&
                setting->enum_options[i].token != NULL) {
                return setting->enum_options[i].token;
            }
        }
    }

    return "?";
}

void settingsFormatEnumOptions(const Setting *setting, char *out, size_t out_size)
{
    size_t used = 0;

    if (out_size == 0) {
        return;
    }

    out[0] = '\0';
    if (setting == NULL || setting->enum_count <= 0) {
        snprintf(out, out_size, "?");
        return;
    }

    for (s32 i = 0; i < setting->enum_count; i++) {
        const char *token = setting->enum_options[i].token;
        int written;

        if (token == NULL) {
            token = "?";
        }

        written = snprintf(out + used, out_size - used, "%s%s",
                           i == 0 ? "" : "|", token);
        if (written < 0) {
            break;
        }
        if ((size_t)written >= out_size - used) {
            used = out_size - 1;
            break;
        }
        used += (size_t)written;
    }
}

void settingsMarkCliOverride(const char *key)
{
    Setting *setting = settingsFindMutable(key);

    if (setting != NULL) {
        setting->override_source = SETTING_OVERRIDE_CLI;
    }
}

SettingOverrideSource settingsGetOverrideSource(const char *key)
{
    const Setting *setting = settingsFindMutable(key);

    return setting != NULL ? setting->override_source : SETTING_OVERRIDE_NONE;
}

void settingsSetOverrideSource(const char *key, SettingOverrideSource source)
{
    Setting *setting = settingsFindMutable(key);

    if (setting != NULL) {
        setting->override_source = source;
    }
}

/* A durable edit (in-game Apply / --config-set / a plain UI write) supersedes any
 * transient env override for persistence purposes (AUDIT-0055): the config writer
 * serializes the durable on-disk shadow for a key still flagged
 * SETTING_OVERRIDE_ENV, so an edit must drop that flag or it would be silently
 * discarded in favor of the shadow. configSetValue() -- the single mutation choke
 * point -- calls this after every successful set; the transient appliers (env /
 * faithful / remaster / --config-override) re-stamp their own source immediately
 * afterward, and the staging live-preview snapshots+restores the source around its
 * transient writes, so only genuine durable edits land as SETTING_OVERRIDE_NONE. */
void settingsNoteDurableEdit(const char *key)
{
    settingsSetOverrideSource(key, SETTING_OVERRIDE_NONE);
}

void settingsMarkAdvanced(const char *key)
{
    Setting *setting = settingsFindMutable(key);

    if (setting != NULL) {
        setting->advanced = 1;
    }
}

static s32 settingsApplyPresetValue(const char *key, const char *value,
                                    SettingOverrideSource source)
{
    Setting *setting = settingsFindMutable(key);

    if (setting == NULL || value == NULL) {
        return 0;
    }

    if (!configSetValue(key, value)) {
        return 0;
    }

    setting->override_source = source;
    return 1;
}

s32 settingsApplyFaithfulValue(const char *key, const char *value)
{
    return settingsApplyPresetValue(key, value, SETTING_OVERRIDE_FAITHFUL);
}

s32 settingsApplyRemasterValue(const char *key, const char *value)
{
    return settingsApplyPresetValue(key, value, SETTING_OVERRIDE_REMASTER);
}

void settingsApplyEnvOverrides(void)
{
    s32 i;

    for (i = 0; i < s_numSettings; i++) {
        Setting *setting = &s_settings[i];
        const char *value;

        if (setting->env == NULL) {
            continue;
        }

        value = getenv(setting->env);
        if (value == NULL || value[0] == '\0') {
            continue;
        }

        if (configSetValue(setting->key, value)) {
            setting->override_source = SETTING_OVERRIDE_ENV;
        }
    }
}

void settingsResetAllToDefaults(void)
{
    s32 i;

    for (i = 0; i < s_numSettings; i++) {
        Setting *setting = &s_settings[i];

        /* A reset-to-defaults is a durable action: it must persist the defaults,
         * so clear any transient override marking (AUDIT-0055). Otherwise a key
         * still flagged SETTING_OVERRIDE_ENV would serialize its pre-reset shadow
         * instead of the freshly-reset default. */
        setting->override_source = SETTING_OVERRIDE_NONE;

        switch (setting->type) {
            case SETTING_TYPE_INT:
                *(s32 *)setting->ptr = setting->def.s32_value;
                break;
            case SETTING_TYPE_UINT:
                *(u32 *)setting->ptr = setting->def.u32_value;
                break;
            case SETTING_TYPE_FLOAT:
                *(f32 *)setting->ptr = setting->def.f32_value;
                break;
            case SETTING_TYPE_ENUM:
                *(s32 *)setting->ptr = setting->def.s32_value;
                break;
            case SETTING_TYPE_STRING:
                if (setting->ptr && setting->string_capacity > 0) {
                    snprintf((char *)setting->ptr, setting->string_capacity, "%s",
                             setting->def_string ? setting->def_string : "");
                }
                break;
            default:
                break;
        }
    }
}

static const char *settingsKeyName(const char *key)
{
    const char *dot = strrchr(key, '.');

    return dot != NULL ? dot + 1 : key;
}

static void settingsSectionName(const char *key, char *out, size_t out_size)
{
    const char *dot = strrchr(key, '.');
    size_t len;

    if (out_size == 0) {
        return;
    }

    if (dot == NULL) {
        snprintf(out, out_size, "General");
        return;
    }

    len = (size_t)(dot - key);
    if (len >= out_size) {
        len = out_size - 1;
    }

    memcpy(out, key, len);
    out[len] = '\0';
}

static void settingsFormatCurrentValue(const Setting *setting, char *out, size_t out_size)
{
    switch (setting->type) {
        case SETTING_TYPE_INT:
            snprintf(out, out_size, "%d", *(s32 *)setting->ptr);
            break;
        case SETTING_TYPE_UINT:
            snprintf(out, out_size, "%u", *(u32 *)setting->ptr);
            break;
        case SETTING_TYPE_FLOAT:
            snprintf(out, out_size, "%g", (double)*(f32 *)setting->ptr);
            break;
        case SETTING_TYPE_ENUM:
            snprintf(out, out_size, "%s",
                     settingsEnumTokenForValue(setting, *(s32 *)setting->ptr));
            break;
        case SETTING_TYPE_STRING:
            snprintf(out, out_size, "%s", setting->ptr ? (char *)setting->ptr : "");
            break;
        default:
            snprintf(out, out_size, "?");
            break;
    }
}

static void settingsFormatDefaultValue(const Setting *setting, char *out, size_t out_size)
{
    switch (setting->type) {
        case SETTING_TYPE_INT:
            snprintf(out, out_size, "%d", setting->def.s32_value);
            break;
        case SETTING_TYPE_UINT:
            snprintf(out, out_size, "%u", setting->def.u32_value);
            break;
        case SETTING_TYPE_FLOAT:
            snprintf(out, out_size, "%g", (double)setting->def.f32_value);
            break;
        case SETTING_TYPE_ENUM:
            snprintf(out, out_size, "%s",
                     settingsEnumTokenForValue(setting, setting->def.s32_value));
            break;
        case SETTING_TYPE_STRING:
            snprintf(out, out_size, "%s", setting->def_string ? setting->def_string : "");
            break;
        default:
            snprintf(out, out_size, "?");
            break;
    }
}

static void settingsFormatRange(const Setting *setting, char *out, size_t out_size)
{
    switch (setting->type) {
        case SETTING_TYPE_INT:
            snprintf(out, out_size, "%d..%d", setting->min.s32_value, setting->max.s32_value);
            break;
        case SETTING_TYPE_UINT:
            snprintf(out, out_size, "%u..%u", setting->min.u32_value, setting->max.u32_value);
            break;
        case SETTING_TYPE_FLOAT:
            snprintf(out, out_size, "%g..%g",
                     (double)setting->min.f32_value,
                     (double)setting->max.f32_value);
            break;
        case SETTING_TYPE_ENUM:
            settingsFormatEnumOptions(setting, out, out_size);
            break;
        case SETTING_TYPE_STRING:
            snprintf(out, out_size, "len<%u", (unsigned int)setting->string_capacity);
            break;
        default:
            snprintf(out, out_size, "?");
            break;
    }
}

void settingsPrintList(FILE *f)
{
    s32 i;

    fprintf(f, "Registered settings (%d):\n", s_numSettings);
    for (i = 0; i < s_numSettings; i++) {
        const Setting *setting = &s_settings[i];
        char def[64];
        char range[64];

        settingsFormatDefaultValue(setting, def, sizeof(def));
        settingsFormatRange(setting, range, sizeof(range));

        fprintf(f, "%s\n", setting->key);
        fprintf(f, "  type=%s scope=%s default=%s range=%s\n",
                settingsTypeName(setting->type),
                settingsScopeName(setting->scope),
                def,
                range);
        /* Curation tier: player-facing settings show in the launcher's tabs;
         * advanced ones are hidden behind the per-tab "Advanced (expert)"
         * disclosure. Emitted so the tiering is observable headlessly (env/CLI
         * overrides still apply to advanced settings -- hidden != removed). */
        fprintf(f, "  tier=%s\n", setting->advanced ? "advanced" : "player");
        if (setting->env != NULL || setting->cli != NULL) {
            fprintf(f, "  override env=%s cli=%s\n",
                    setting->env != NULL ? setting->env : "-",
                    setting->cli != NULL ? setting->cli : "-");
        }
        if (setting->override_source != SETTING_OVERRIDE_NONE) {
            fprintf(f, "  active_override=%s\n",
                    settingsOverrideSourceName(setting->override_source));
        }
        if (setting->label != NULL) {
            fprintf(f, "  label=%s\n", setting->label);
        }
        if (setting->help != NULL) {
            fprintf(f, "  help=%s\n", setting->help);
        }
    }
}

void settingsPrintDump(FILE *f)
{
    char current_section[CONFIG_MAX_SECNAME + 1] = {0};
    s32 i;

    fprintf(f, "# MGB64 settings dump\n");
    fprintf(f, "# This shows the current value after loading ge007.ini.\n");
    fprintf(f, "# Runtime/debug flags such as --rom, --level, and --deterministic are not persistent settings.\n");

    for (i = 0; i < s_numSettings; i++) {
        const Setting *setting = &s_settings[i];
        char section[CONFIG_MAX_SECNAME + 1];
        char current[64];
        char def[64];
        char range[64];

        settingsSectionName(setting->key, section, sizeof(section));
        if (strcmp(section, current_section) != 0) {
            fprintf(f, "\n[%s]\n", section);
            snprintf(current_section, sizeof(current_section), "%s", section);
        }

        settingsFormatCurrentValue(setting, current, sizeof(current));
        settingsFormatDefaultValue(setting, def, sizeof(def));
        settingsFormatRange(setting, range, sizeof(range));

        if (setting->label != NULL) {
            fprintf(f, "# %s\n", setting->label);
        }
        if (setting->help != NULL) {
            fprintf(f, "# %s\n", setting->help);
        }
        fprintf(f, "# type=%s scope=%s default=%s range=%s\n",
                settingsTypeName(setting->type),
                settingsScopeName(setting->scope),
                def,
                range);
        if (setting->override_source != SETTING_OVERRIDE_NONE) {
            fprintf(f, "# active_override=%s\n",
                    settingsOverrideSourceName(setting->override_source));
        }
        fprintf(f, "%s=%s\n", settingsKeyName(setting->key), current);
    }
}

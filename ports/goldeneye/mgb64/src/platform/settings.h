#ifndef _PLATFORM_SETTINGS_H_
#define _PLATFORM_SETTINGS_H_

#include <stdio.h>
#include "config_pc.h"

typedef enum SettingType {
    SETTING_TYPE_INT,
    SETTING_TYPE_UINT,
    SETTING_TYPE_FLOAT,
    SETTING_TYPE_ENUM,
    SETTING_TYPE_STRING
} SettingType;

typedef enum SettingScope {
    SETTING_SCOPE_LIVE,
    SETTING_SCOPE_RESTART
} SettingScope;

typedef enum SettingOverrideSource {
    SETTING_OVERRIDE_NONE,
    SETTING_OVERRIDE_ENV,
    SETTING_OVERRIDE_CLI,
    SETTING_OVERRIDE_FAITHFUL,
    SETTING_OVERRIDE_REMASTER
} SettingOverrideSource;

typedef union SettingValue {
    s32 s32_value;
    u32 u32_value;
    f32 f32_value;
} SettingValue;

typedef struct Setting {
    const char *key;
    SettingType type;
    SettingScope scope;
    void *ptr;
    SettingValue def;
    SettingValue min;
    SettingValue max;
    const char *def_string;
    const ConfigEnumOption *enum_options;
    s32 enum_count;
    size_t string_capacity;
    const char *env;
    const char *cli;
    const char *label;
    const char *help;
    SettingOverrideSource override_source;
    /* Curation tier. 0 = player-facing (shown in the launcher UI by default);
     * 1 = advanced/dev-diagnostic (hidden behind the per-tab "Advanced (expert)"
     * disclosure). Opt-in: everything defaults to player-facing so nothing is
     * hidden by accident. Hidden in the UI only -- env/CLI overrides are
     * unaffected (the setting stays in the registry). */
    s32 advanced;
} Setting;

void settingsRegisterInt(const char *key, s32 *var, s32 def, s32 min, s32 max,
                         SettingScope scope, const char *env, const char *cli,
                         const char *label, const char *help);
void settingsRegisterUInt(const char *key, u32 *var, u32 def, u32 min, u32 max,
                          SettingScope scope, const char *env, const char *cli,
                          const char *label, const char *help);
void settingsRegisterFloat(const char *key, f32 *var, f32 def, f32 min, f32 max,
                           SettingScope scope, const char *env, const char *cli,
                           const char *label, const char *help);
void settingsRegisterEnum(const char *key, s32 *var, s32 def,
                          const ConfigEnumOption *options, s32 option_count,
                          SettingScope scope, const char *env, const char *cli,
                          const char *label, const char *help);
void settingsRegisterString(const char *key, char *var, size_t capacity,
                            const char *def,
                            SettingScope scope, const char *env, const char *cli,
                            const char *label, const char *help);

s32 settingsCount(void);
const Setting *settingsAt(s32 index);
const Setting *settingsFind(const char *key);

const char *settingsTypeName(SettingType type);
const char *settingsScopeName(SettingScope scope);
const char *settingsOverrideSourceName(SettingOverrideSource source);
const char *settingsEnumTokenForValue(const Setting *setting, s32 value);
void settingsFormatEnumOptions(const Setting *setting, char *out, size_t out_size);
void settingsMarkCliOverride(const char *key);
/* Read/write a setting's active override source. Used by the config writer to
 * decide whether to serialize the durable on-disk shadow (env-overridden keys)
 * and by the staging live-preview to snapshot+restore the source around its
 * transient writes (AUDIT-0055). Get returns SETTING_OVERRIDE_NONE for an
 * unknown key; Set is a no-op for one. */
SettingOverrideSource settingsGetOverrideSource(const char *key);
void settingsSetOverrideSource(const char *key, SettingOverrideSource source);
/* Mark a key as durably edited (clears its override source to NONE). Called by
 * configSetValue after every successful set so an explicit UI/--config-set edit
 * to an env-overridden key persists rather than being replaced by the shadow. */
void settingsNoteDurableEdit(const char *key);
/* Tag an already-registered setting as advanced (dev/diagnostic). Called right
 * after the settingsRegister* site. Hidden from the launcher's player tabs;
 * env/CLI overrides still apply. No-op if the key is unknown. */
void settingsMarkAdvanced(const char *key);
/* Force one setting to its faithful (pre-remaster) value for this launch only.
 * Applies via configSetValue (validated/clamped) and marks the setting as a
 * transient SETTING_OVERRIDE_FAITHFUL override so configSave never persists it
 * -- a `--faithful` run leaves the user's saved ge007.ini untouched. Returns 1
 * if the value was applied. Used by platformApplyFaithfulPreset(). */
s32 settingsApplyFaithfulValue(const char *key, const char *value);
/* Same transient-override mechanism as settingsApplyFaithfulValue, but stamps
 * SETTING_OVERRIDE_REMASTER so diagnostics label it correctly. Used by
 * platformApplyRemasterPreset() (the --remaster launch preset). */
s32 settingsApplyRemasterValue(const char *key, const char *value);
void settingsApplyEnvOverrides(void);
void settingsResetAllToDefaults(void);
void settingsPrintList(FILE *f);
void settingsPrintDump(FILE *f);

#endif /* _PLATFORM_SETTINGS_H_ */

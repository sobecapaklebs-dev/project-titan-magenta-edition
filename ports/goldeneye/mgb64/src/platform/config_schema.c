// config_schema.c — engine-side implementation of the app config_schema API.
// Translates the Setting registry into plain-type entries for the app UI.
#include "../app/config_schema.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>   /* strtol/atof for staged-value parsing */

#include "settings.h"
#include "config_pc.h"
#include "savedir.h"

extern void platformRegisterConfig(void);
extern void portAudioRegisterConfig(void);

/* ---- Settings staging (host-side UI working copy) ------------------------
 * See src/app/config_schema.h for the contract. A session captures the app UI's
 * mgb_config_set_* writes into s_staged[] instead of the live globals; the
 * getters overlay s_staged[] so the UI reflects the edit while the engine keeps
 * running on the last-applied values. configSetValue (the live/ini mutation) is
 * only reached on Apply and Preview. CONFIG_MAX_SETTINGS (128) bounds how many
 * distinct keys can be staged, so this store never overflows in practice. */
#define CONFIG_STAGING_MAX CONFIG_MAX_SETTINGS
/* AUDIT-0026: the staged value must hold the largest registered string setting
 * (Video.TexturePack / Video.SceneDecorDir are 1024-byte path buffers), not just
 * short numeric/enum text -- otherwise a long folder pick is silently truncated
 * and the damaged path is committed on Apply. */
#define CONFIG_STAGING_VAL_MAX 1024
typedef struct { char key[96]; char val[CONFIG_STAGING_VAL_MAX]; } StagedEntry;
static StagedEntry s_staged[CONFIG_STAGING_MAX];
static s32 s_stagedCount = 0;
static s32 s_stagingActive = 0;

/* One active FOV/sensitivity live-preview at a time (configStagingPreview). */
static char s_previewKey[96] = {0};
static char s_previewBackup[64] = {0};
static s32  s_previewActive = 0;
/* The previewed key's override source, snapshotted on preview-on and restored on
 * preview-off. A preview is a transient live write through configSetValue, which
 * clears the override marking (durable-edit semantics); without restoring it, a
 * previewed-then-reverted env-overridden key would lose its SETTING_OVERRIDE_ENV
 * flag and a later save would persist the reverted env value instead of the
 * durable shadow (AUDIT-0055). */
static SettingOverrideSource s_previewBackupSource = SETTING_OVERRIDE_NONE;

static StagedEntry *stagedFind(const char *key) {
    for (s32 i = 0; i < s_stagedCount; i++) {
        if (strcmp(s_staged[i].key, key) == 0) return &s_staged[i];
    }
    return NULL;
}

/* The staged string for `key`, or NULL when no session is open / key unstaged. */
static const char *stagedLookup(const char *key) {
    StagedEntry *e;
    if (!s_stagingActive) return NULL;
    e = stagedFind(key);
    return e ? e->val : NULL;
}

static void stagedPut(const char *key, const char *val) {
    StagedEntry *e = stagedFind(key);
    if (!e) {
        if (s_stagedCount >= CONFIG_STAGING_MAX) return;  /* cap == setting count; unreachable */
        e = &s_staged[s_stagedCount++];
        snprintf(e->key, sizeof(e->key), "%s", key);
    }
    snprintf(e->val, sizeof(e->val), "%s", val);
}

/* Route a validated string write to the staged copy while a session is open,
 * else straight to the live global (the pre-staging behavior). */
static void stagedOrLive(const char *key, const char *val) {
    if (s_stagingActive) stagedPut(key, val);
    else configSetValue(key, val);
}

/* Render a setting's current live value in the string form configSetValue reads
 * (used to snapshot the live value before a preview so it can be restored). */
static void liveValueToString(const Setting *s, char *buf, size_t n) {
    if (!s || !buf || n == 0) { if (buf && n) buf[0] = '\0'; return; }
    switch (s->type) {
        case SETTING_TYPE_INT:
            snprintf(buf, n, "%d", s->ptr ? *(s32 *)s->ptr : s->def.s32_value); break;
        case SETTING_TYPE_UINT:
            snprintf(buf, n, "%u", s->ptr ? *(u32 *)s->ptr : s->def.u32_value); break;
        case SETTING_TYPE_FLOAT:
            snprintf(buf, n, "%g", (double)(s->ptr ? *(f32 *)s->ptr : s->def.f32_value)); break;
        case SETTING_TYPE_ENUM:
            snprintf(buf, n, "%s", settingsEnumTokenForValue(s, s->ptr ? *(s32 *)s->ptr : s->def.s32_value)); break;
        default:
            snprintf(buf, n, "%s", (s->type == SETTING_TYPE_STRING && s->ptr) ? (const char *)s->ptr : ""); break;
    }
}

int configStagingActive(void) { return (int)s_stagingActive; }

void configStagingBegin(void) {
    s_stagedCount = 0;
    s_stagingActive = 1;
}

/* on=1: snapshot live, push staged -> live. on=0: restore the snapshot. */
void configStagingPreview(const char *key, int on) {
    if (!key) return;
    if (on) {
        const char *sv;
        const Setting *s;
        if (s_previewActive) return;              /* one preview at a time */
        sv = stagedLookup(key);
        if (!sv) return;                          /* nothing staged to feel */
        s = settingsFind(key);
        if (!s) return;
        liveValueToString(s, s_previewBackup, sizeof(s_previewBackup));
        s_previewBackupSource = settingsGetOverrideSource(key);  /* preserve across the transient write */
        snprintf(s_previewKey, sizeof(s_previewKey), "%s", key);
        s_previewActive = 1;
        configSetValue(key, sv);                  /* engine feels the staged value */
    } else {
        if (!s_previewActive || strcmp(s_previewKey, key) != 0) return;
        configSetValue(key, s_previewBackup);     /* restore the live value */
        settingsSetOverrideSource(key, s_previewBackupSource);  /* ...and its override marking */
        s_previewActive = 0;
        s_previewKey[0] = '\0';
    }
}

void configStagingDiscard(void) {
    if (s_previewActive) configStagingPreview(s_previewKey, 0);  /* undo any live preview */
    s_stagedCount = 0;
    s_stagingActive = 0;
}

MgbConfigSaveResult configStagingApply(void) {
    s32 i;
    if (s_previewActive) {                         /* undo the preview so we commit cleanly */
        configSetValue(s_previewKey, s_previewBackup);
        s_previewActive = 0;
        s_previewKey[0] = '\0';
    }
    s_stagingActive = 0;                           /* configSetValue now targets the live globals */
    for (i = 0; i < s_stagedCount; i++) {
        configSetValue(s_staged[i].key, s_staged[i].val);
    }
    s_stagedCount = 0;
    /* Thread the persist outcome up to the app (AUDIT-0036): OK / SUPPRESSED /
     * FAILED, instead of dropping the old void configSave(). */
    return (MgbConfigSaveResult)configSaveResult();
}

/* Resolve a staged enum token to its s32 value; returns fallback if not found. */
static s32 stagedEnumValue(const Setting *s, const char *token, s32 fallback) {
    s32 i;
    for (i = 0; i < s->enum_count; i++) {
        if (s->enum_options[i].token && strcmp(s->enum_options[i].token, token) == 0) {
            return s->enum_options[i].value;
        }
    }
    return fallback;
}

void mgb_config_init(void) {
    savedirInit(NULL);
    platformRegisterConfig();
    portAudioRegisterConfig();
    configInit();
}

int mgb_config_save(void) { return (int)configSave(); }

/* The app-facing MgbConfigSaveResult must stay numerically identical to the
 * engine's ConfigSaveResult so the casts here (and in configStagingApply) are a
 * pure re-typing across the engine-free header boundary (AUDIT-0036). */
_Static_assert((int)MGB_CONFIG_SAVE_OK == (int)CONFIG_SAVE_OK &&
               (int)MGB_CONFIG_SAVE_SUPPRESSED == (int)CONFIG_SAVE_SUPPRESSED &&
               (int)MGB_CONFIG_SAVE_FAILED == (int)CONFIG_SAVE_FAILED,
               "MgbConfigSaveResult must mirror ConfigSaveResult 1:1");

MgbConfigSaveResult mgb_config_save_result(void) {
    return (MgbConfigSaveResult)configSaveResult();
}

const char *mgb_config_path(void) {
    /* savedirPath returns its own static buffer; copy so the caller isn't racing
     * the next savedirPath() user (AUDIT-0047). */
    static char s_cfgPath[1024];
    snprintf(s_cfgPath, sizeof(s_cfgPath), "%s", savedirPath(CONFIG_FILENAME));
    return s_cfgPath;
}

int mgb_config_count(void) { return (int)settingsCount(); }

static MgbCfgKind kindOf(SettingType t) {
    switch (t) {
        case SETTING_TYPE_INT:   return MGB_CFG_INT;
        case SETTING_TYPE_UINT:  return MGB_CFG_UINT;
        case SETTING_TYPE_FLOAT: return MGB_CFG_FLOAT;
        case SETTING_TYPE_ENUM:  return MGB_CFG_ENUM;
        default:                 return MGB_CFG_STRING;
    }
}

int mgb_config_get(int index, MgbCfgEntry *out) {
    const Setting *s = settingsAt((s32)index);
    if (!s || !out) return 0;
    memset(out, 0, sizeof(*out));

    snprintf(out->key, sizeof(out->key), "%s", s->key);
    // Split "Section.name".
    const char *dot = strchr(s->key, '.');
    if (dot) {
        size_t seclen = (size_t)(dot - s->key);
        if (seclen >= sizeof(out->section)) seclen = sizeof(out->section) - 1;
        memcpy(out->section, s->key, seclen);
        out->section[seclen] = '\0';
        snprintf(out->name, sizeof(out->name), "%s", dot + 1);
    } else {
        snprintf(out->section, sizeof(out->section), "%s", "General");
        snprintf(out->name, sizeof(out->name), "%s", s->key);
    }
    snprintf(out->label, sizeof(out->label), "%s", (s->label && s->label[0]) ? s->label : out->name);
    snprintf(out->help, sizeof(out->help), "%s", s->help ? s->help : "");

    out->kind = kindOf(s->type);
    out->is_live = (s->scope == SETTING_SCOPE_LIVE) ? 1 : 0;
    out->enum_count = (int)s->enum_count;
    out->advanced = s->advanced ? 1 : 0;

    switch (s->type) {
        case SETTING_TYPE_INT:
            out->min_val = (float)s->min.s32_value;
            out->max_val = (float)s->max.s32_value;
            out->def_val = (float)s->def.s32_value;
            out->cur_int = s->ptr ? *(s32 *)s->ptr : s->def.s32_value;
            break;
        case SETTING_TYPE_UINT:
            out->min_val = (float)s->min.u32_value;
            out->max_val = (float)s->max.u32_value;
            out->def_val = (float)s->def.u32_value;
            out->cur_int = s->ptr ? (int)*(u32 *)s->ptr : (int)s->def.u32_value;
            break;
        case SETTING_TYPE_FLOAT:
            out->min_val = s->min.f32_value;
            out->max_val = s->max.f32_value;
            out->def_val = s->def.f32_value;
            out->cur_float = s->ptr ? *(f32 *)s->ptr : s->def.f32_value;
            break;
        case SETTING_TYPE_ENUM: {
            out->min_val = 0.0f;
            out->max_val = (float)(s->enum_count > 0 ? s->enum_count - 1 : 0);
            s32 cur = s->ptr ? *(s32 *)s->ptr : s->def.s32_value;
            out->cur_int = cur;
            out->cur_enum_index = 0;
            for (s32 i = 0; i < s->enum_count; i++) {
                if (s->enum_options[i].value == cur) { out->cur_enum_index = (int)i; break; }
            }
            break;
        }
        default:
            break;
    }

    /* Overlay a staged edit so the settings UI reflects the in-progress value
     * while the engine keeps running on the live global. */
    {
        const char *sv = stagedLookup(s->key);
        if (sv) {
            switch (s->type) {
                case SETTING_TYPE_INT:
                case SETTING_TYPE_UINT:
                    out->cur_int = (int)strtol(sv, NULL, 10);
                    break;
                case SETTING_TYPE_FLOAT:
                    out->cur_float = (float)atof(sv);
                    break;
                case SETTING_TYPE_ENUM:
                    out->cur_int = (int)stagedEnumValue(s, sv, out->cur_int);
                    for (s32 i = 0; i < s->enum_count; i++) {
                        if (s->enum_options[i].value == out->cur_int) { out->cur_enum_index = (int)i; break; }
                    }
                    break;
                default:
                    break;
            }
        }
    }
    return 1;
}

int mgb_config_get_int(const char *key, int fallback) {
    const Setting *s = settingsFind(key);
    const char *sv;
    if (!s || !s->ptr) return fallback;
    sv = stagedLookup(key);
    if (sv) {
        if (s->type == SETTING_TYPE_ENUM) return (int)stagedEnumValue(s, sv, fallback);
        return (int)strtol(sv, NULL, 10);
    }
    switch (s->type) {
        case SETTING_TYPE_INT:
        case SETTING_TYPE_ENUM: return (int)*(s32 *)s->ptr;
        case SETTING_TYPE_UINT: return (int)*(u32 *)s->ptr;
        default:                return fallback;
    }
}

float mgb_config_get_float(const char *key, float fallback) {
    const Setting *s = settingsFind(key);
    const char *sv;
    if (!s || !s->ptr || s->type != SETTING_TYPE_FLOAT) return fallback;
    sv = stagedLookup(key);
    if (sv) return (float)atof(sv);
    return *(f32 *)s->ptr;
}

int mgb_config_get_string(const char *key, char *out, int out_size) {
    const Setting *s;
    const char *sv;
    if (!out || out_size <= 0) return 0;
    out[0] = '\0';
    s = settingsFind(key);
    if (!s || s->type != SETTING_TYPE_STRING || !s->ptr) return 0;
    sv = stagedLookup(key);
    snprintf(out, (size_t)out_size, "%s", sv ? sv : (const char *)s->ptr);
    return 1;
}

const char *mgb_config_enum_token(const char *key, int optIndex) {
    const Setting *s = settingsFind(key);
    if (!s || optIndex < 0 || optIndex >= s->enum_count) return "";
    return s->enum_options[optIndex].token ? s->enum_options[optIndex].token : "";
}

void mgb_config_set_int(const char *key, int value) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%d", value);
    stagedOrLive(key, buf);
}

void mgb_config_set_float(const char *key, float value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", (double)value);
    stagedOrLive(key, buf);
}

void mgb_config_set_enum(const char *key, int optIndex) {
    const Setting *s = settingsFind(key);
    if (!s || optIndex < 0 || optIndex >= s->enum_count) return;
    const char *token = s->enum_options[optIndex].token;
    if (token) stagedOrLive(key, token);
}

void mgb_config_set_string(const char *key, const char *value) {
    stagedOrLive(key, value ? value : "");
}

void mgb_config_reset_default(const char *key) {
    const Setting *s = settingsFind(key);
    if (!s) return;
    char buf[32];
    switch (s->type) {
        case SETTING_TYPE_INT:  snprintf(buf, sizeof(buf), "%d", s->def.s32_value); break;
        case SETTING_TYPE_UINT: snprintf(buf, sizeof(buf), "%u", s->def.u32_value); break;
        case SETTING_TYPE_FLOAT: snprintf(buf, sizeof(buf), "%g", (double)s->def.f32_value); break;
        case SETTING_TYPE_ENUM:
            stagedOrLive(key, settingsEnumTokenForValue(s, s->def.s32_value));
            return;
        default:
            if (s->def_string) stagedOrLive(key, s->def_string);
            return;
    }
    stagedOrLive(key, buf);
}

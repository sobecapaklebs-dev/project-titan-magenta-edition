/**
 * config_pc.h — INI config file system for the GE007 PC port.
 *
 * Modeled after the Perfect Dark port's declarative config system.
 * Settings are registered with type + min/max, loaded from ge007.ini,
 * and saved back on shutdown or settings change.
 *
 * Usage:
 *   static s32 g_window_mode = 0;
 *   configRegisterEnum("Video.WindowMode", &g_window_mode, options, option_count);
 *   configInit();  // loads ge007.ini, applies values
 *   configSave();  // writes current values to ge007.ini
 */
#ifndef _PLATFORM_CONFIG_PC_H_
#define _PLATFORM_CONFIG_PC_H_

#include <ultra64.h>
#include <stddef.h>

#define CONFIG_FILENAME  "ge007.ini"
#define CONFIG_MAX_SETTINGS 128
#define CONFIG_MAX_KEYNAME  128
#define CONFIG_MAX_SECNAME   64

typedef struct ConfigEnumOption {
    const char *token;
    s32 value;
} ConfigEnumOption;

/* Register a setting. Call before configInit().
 * key format: "Section.Key" (e.g., "Video.WindowMode").
 * min/max: validation range. Set min >= max to disable clamping. */
void configRegisterInt(const char *key, s32 *var, s32 min, s32 max);
void configRegisterFloat(const char *key, f32 *var, f32 min, f32 max);
void configRegisterUInt(const char *key, u32 *var, u32 min, u32 max);
void configRegisterEnum(const char *key, s32 *var,
                        const ConfigEnumOption *options, s32 option_count);
void configRegisterString(const char *key, char *var, size_t capacity);

/* Load settings from ge007.ini (in savedir). Call after all registrations. */
void configInit(void);

/* Tri-state outcome of a config save (AUDIT-0036), so callers (and the settings
 * UI) can tell an intentional faithful-mode no-op apart from a real write failure
 * — the two were previously indistinguishable at the configSave() 0/1 boundary. */
typedef enum ConfigSaveResult {
    CONFIG_SAVE_OK = 0,       /* values written to ge007.ini */
    CONFIG_SAVE_SUPPRESSED,   /* faithful session: intentionally NOT written */
    CONFIG_SAVE_FAILED        /* fopen / fclose / atomic-replace error */
} ConfigSaveResult;

/* Save current values to ge007.ini, reporting the tri-state outcome above. */
ConfigSaveResult configSaveResult(void);

/* Thin 0/1 wrapper over configSaveResult() preserving the historical contract
 * (shutdown save, in-game menu, --config-set/--reset-config nonzero-on-failure):
 * OK|SUPPRESSED -> 1, FAILED -> 0. Returns 1 on success or intentional no-op. */
s32 configSave(void);

/* When suppressed (set by a `--faithful` launch), configSave() becomes a no-op so
 * a transient faithful session never rewrites the user's saved ge007.ini. */
void configSetSaveSuppressed(s32 suppressed);

/* Set a registered setting from a string value. Returns 1 if the key exists. */
s32 configSetValue(const char *key, const char *value);

/* Find a registered setting by "Section.Key" name.
 * Returns the backing variable pointer, or NULL if not found.
 * type_out receives: 0=int, 1=float, 2=uint, 3=enum, 4=string
 * (or -1 if not found). */
void *configFindEntry(const char *key, int *type_out);

#endif /* _PLATFORM_CONFIG_PC_H_ */

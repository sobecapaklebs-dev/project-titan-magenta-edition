// config_schema.h — clean, engine-free view of the settings registry for the
// app's auto-generated settings UI.
//
// Implemented in src/platform/config_schema.c (compiled into the engine), which
// translates the engine's Setting registry (settings.h) into these plain-type
// entries and wraps configSetValue()/configInit()/configSave(). The app never
// includes engine headers (which carry the math.h shim + ultra64 types).
#ifndef MGB64_CONFIG_SCHEMA_H
#define MGB64_CONFIG_SCHEMA_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MGB_CFG_INT,
    MGB_CFG_UINT,
    MGB_CFG_FLOAT,
    MGB_CFG_ENUM,
    MGB_CFG_STRING
} MgbCfgKind;

typedef struct {
    char section[32];   // "Video" | "Input" | "Game" | "Audio"
    char name[64];      // key within the section
    char key[96];       // full "Section.name" (as configSetValue expects)
    char label[80];     // human label (falls back to name)
    char help[224];     // one-line description
    MgbCfgKind kind;
    int   is_live;      // 1 = applies next frame; 0 = needs restart
    float min_val, max_val, def_val;
    int   cur_int;      // current value for INT/UINT
    float cur_float;    // current value for FLOAT
    int   enum_count;   // options for ENUM
    int   cur_enum_index;
    int   advanced;     // 1 = dev/diagnostic (hide behind "Advanced" disclosure)
} MgbCfgEntry;

// Save outcome (AUDIT-0036), mirroring the engine's ConfigSaveResult 1:1 so the
// settings UI can tell an intentional faithful-mode no-op (SUPPRESSED) apart from
// a real write failure (FAILED). Kept engine-free (plain enum, no ConfigSaveResult
// leak) so app code never pulls the engine headers; config_schema.c static-asserts
// the value mapping.
typedef enum MgbConfigSaveResult {
    MGB_CONFIG_SAVE_OK         = 0,  // written to ge007.ini
    MGB_CONFIG_SAVE_SUPPRESSED = 1,  // faithful session: intentionally not written
    MGB_CONFIG_SAVE_FAILED     = 2   // write failed
} MgbConfigSaveResult;

// Lifecycle. Safe to call once at app startup; the engine boot re-registers
// idempotently (settingsFindOrAdd) and reloads ge007.ini.
void mgb_config_init(void);      // savedir + register + load ge007.ini
int  mgb_config_save(void);      // write ge007.ini; 1 unless the write failed (compat)
MgbConfigSaveResult mgb_config_save_result(void);  // same save, tri-state outcome
// Resolved absolute path of the engine config file (ge007.ini) in the active save
// directory. Returns a pointer to a static buffer (copy before reuse). Use this to
// locate the REAL file instead of guessing a CWD-relative name (AUDIT-0047).
const char *mgb_config_path(void);

// Enumeration.
int  mgb_config_count(void);
int  mgb_config_get(int index, MgbCfgEntry *out);  // 1 on success
const char *mgb_config_enum_token(const char *key, int optIndex);

// Current INT/UINT/ENUM value for a key, or `fallback` if the key is absent or
// not an integer-typed setting. Lightweight lookup for app code that needs one
// value (e.g. the launcher's Game.CheckForUpdates gate) without enumerating.
int  mgb_config_get_int(const char *key, int fallback);

// Current FLOAT value for a key, or `fallback` if the key is absent or not a
// float setting. Lightweight lookup for app code that needs one value (e.g. the
// app shell reading UI.Scale) without enumerating.
float mgb_config_get_float(const char *key, float fallback);

// Current STRING value for a key. Copies into out (always NUL-terminated when
// out_size > 0). Returns 1 if the key exists and is a string setting, else 0
// (and out is set to ""). Lightweight lookup for the settings UI, which needs
// the string value without growing MgbCfgEntry.
int  mgb_config_get_string(const char *key, char *out, int out_size);

// Mutation (validated/clamped by the engine).
void mgb_config_set_int(const char *key, int value);
void mgb_config_set_float(const char *key, float value);
void mgb_config_set_enum(const char *key, int optIndex);
void mgb_config_set_string(const char *key, const char *value);
void mgb_config_reset_default(const char *key);

// --- Settings staging (host-side UI working copy) -------------------------
// The in-game settings UI opens a staging session so a mid-game edit edits a
// copy and never disturbs the live engine globals until the user Applies. While
// a session is open, the mgb_config_get/set_* calls above operate on the staged
// copy; the engine reads its own globals directly, so it keeps running on the
// last-applied values (this is what makes editing settings mid-match safe, incl.
// multiplayer, which never pauses). configSetValue — the only path that mutates
// live globals or the ini — is reached solely on Apply/Preview, so the raw
// engine/CLI/ini-load paths are unaffected. Not engaged under --deterministic
// (the app shell isn't present in headless automation).
void configStagingBegin(void);    // start a session (drops any prior staged copy)
// commit staged -> live globals + persist, then end the session; returns the
// save outcome (AUDIT-0036) so the UI can surface a persist failure. Existing
// callers may discard the value (unchanged 0/1-agnostic call sites keep working).
MgbConfigSaveResult configStagingApply(void);
void configStagingDiscard(void);  // drop the staged copy (live untouched), end the session
int  configStagingActive(void);   // 1 while a session is open

// Temporarily push one staged value to its live global so the owner can feel it
// on the frozen frame (FOV / mouse sensitivity). on=1 previews, on=0 reverts to
// the pre-preview live value. No-op if the key isn't staged, staging is inactive,
// or a different key is already being previewed (one preview at a time).
void configStagingPreview(const char *key, int on);

#ifdef __cplusplus
}
#endif

#endif  // MGB64_CONFIG_SCHEMA_H

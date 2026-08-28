/*
 * port_env.h — registering accessors for GE007_* environment flags.
 *
 * MGB64 gates a large amount of port/diagnostic behavior on GE007_* environment
 * variables read via getenv() at call sites scattered across the tree. Those
 * hatches are the right seam (they keep faithful decompiled code unmodified),
 * but read as string literals they are undiscoverable: you cannot ask the binary
 * "what can I toggle?" and their defaults/help live only in comments.
 *
 * These accessors keep the same seam but make it enumerable and consistent:
 *   - each accessor registers {name, kind, default, help} the first time it is
 *     seen, so the flag surface is enumerable (port_env_dump; the offline
 *     scanner tools/gen_env_reference.py produces the full docs/ENV_FLAGS.md);
 *   - the environment is read once per name and the parsed value cached, so
 *     repeated calls are cheap and a flag can't change mid-run;
 *   - parsing is consistent (bool/int/float) instead of ad-hoc atoi/atof.
 *
 * Bool semantics match the existing ge_env_bool() convention so migrating a
 * call site is behavior-preserving:
 *   unset / empty  -> default_on
 *   "0"            -> off
 *   anything else  -> on
 * (so GE007_FOO=0 always means off, regardless of the default.)
 *
 * NOTE: presence-only sites that use `getenv("X") != NULL` are NOT equivalent to
 * port_env_bool (they treat "0" as on). Only migrate a presence site to
 * port_env_bool if "0 means off" is the intended behavior; otherwise register it
 * with default_on = 0 and keep in mind "GE007_X=0" will now mean off.
 */
#ifndef MGB64_PORT_ENV_H
#define MGB64_PORT_ENV_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Registering, read-once accessors. `help` is a short one-line description used
 * by the generated reference (docs/ENV_FLAGS.md); pass NULL if genuinely none. */
int   port_env_bool(const char *name, int default_on, const char *help);
int   port_env_int(const char *name, int default_val, const char *help);
float port_env_float(const char *name, float default_val, const char *help);

/* Registering *presence* accessor: returns 1 iff the variable is present in the
 * environment (getenv(name) != NULL), 0 otherwise -- exactly the semantics of a
 * raw `getenv("X") != NULL` gate, so a presence site migrates behavior-identically
 * (unlike port_env_bool, this treats "GE007_X=0" and "GE007_X=" as SET). Registers
 * {name, presence, help} the first time it is seen (enumerable in the reference)
 * and reads the environment once, caching the result. `help` may be NULL. */
int   port_env_set(const char *name, const char *help);

/* Dump the flags registered so far.
 *   format == "md"  -> a Markdown table (name | type | default | current | help)
 *   otherwise       -> aligned human-readable text
 * Only flags whose accessor has actually run this session appear (registration
 * is lazy), so run after startup for the fullest catalog. */
void  port_env_dump(FILE *out, const char *format);

/* Number of distinct flags registered so far (for tests / diagnostics). */
int   port_env_registered_count(void);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_PORT_ENV_H */

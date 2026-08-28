/**
 * ge_debug.h -- Centralized diagnostic macros for the NATIVE_PORT.
 *
 * Provides:
 *   GEDBG(fmt, ...)       -- conditional stderr logging (GE007_DEBUG=1)
 *   GEABORT_IF(cond, ...) -- conditional abort (GE007_ASSERT_ON_FAIL=1)
 *   ge_dbg_enabled()      -- cached check for GE007_DEBUG env var
 *
 * Coexists with the existing GE007_VERBOSE printf pattern.
 * Include only inside #ifdef NATIVE_PORT blocks.
 */
#ifndef GE_DEBUG_H
#define GE_DEBUG_H

#ifdef NATIVE_PORT

#include <stdio.h>
#include <stdlib.h>
#include "platform/port_env.h"

static inline int ge_dbg_enabled(void) {
#ifdef PROJECT_TITAN_TGR
    return 0;
#else
    static int cached = -1;
    if (cached < 0) {
        const char *env = getenv("GE007_DEBUG");
        cached = (env != NULL) ? 1 : 0;
    }
    return cached;
#endif
}

static inline int ge_dbg_assert_on_fail(void) {
#ifdef PROJECT_TITAN_TGR
    return 0;
#else
    static int cached = -1;
    if (cached < 0) {
        const char *env = getenv("GE007_ASSERT_ON_FAIL");
        cached = (env != NULL) ? 1 : 0;
    }
    return cached;
#endif
}

/* ge_env_bool -- single truth table for GE007_* behavior gates.
 *   unset / empty -> default_on
 *   "0"           -> off
 *   anything else -> on
 * This avoids the "=0 silently enables" footgun: GE007_FOO=0 always means off,
 * regardless of whether the gate defaults on or off.
 *
 * Forwards to the registering port_env_bool so the flag is enumerable (it lands
 * in the generated docs/ENV_FLAGS.md); the semantics above are identical, and
 * the environment is read once and cached. */
static inline int ge_env_bool(const char *name, int default_on) {
    return port_env_bool(name, default_on, NULL);
}

#ifdef PROJECT_TITAN_TGR
#define GEDBG(fmt, ...) do { (void)sizeof(fmt); } while (0)
#define GEABORT_IF(cond, fmt, ...) do { (void)(cond); (void)sizeof(fmt); } while (0)
#else
#define GEDBG(fmt, ...) do { \
    if (ge_dbg_enabled()) { \
        fprintf(stderr, "[GEDBG] " fmt "\n", ##__VA_ARGS__); \
        fflush(stderr); \
    } \
} while (0)

#define GEABORT_IF(cond, fmt, ...) do { \
    if ((cond)) { \
        fprintf(stderr, "[GEASSERT] " fmt "\n", ##__VA_ARGS__); \
        fflush(stderr); \
        if (ge_dbg_assert_on_fail()) abort(); \
    } \
} while (0)
#endif

#else /* !NATIVE_PORT */

static int ge_dbg_enabled(void) {
    return 0;
}

static int ge_dbg_assert_on_fail(void) {
    return 0;
}

static int ge_env_bool(const char *name, int default_on) {
    (void)name;
    return default_on;
}

static void ge_dbg_noop(const char *fmt, ...) {
    (void)fmt;
}

static void ge_dbg_abort_noop(int cond, const char *fmt, ...) {
    (void)cond;
    (void)fmt;
}

#define GEDBG if (0) ge_dbg_noop
#define GEABORT_IF if (0) ge_dbg_abort_noop

#endif /* NATIVE_PORT */

#endif /* GE_DEBUG_H */

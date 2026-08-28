/*
 * test_port_env.c — ROM-free unit test for the registering GE007_* accessors.
 *
 * Verifies bool/int/float parse semantics, read-once caching, default handling,
 * and registry enumeration. Uses setenv/unsetenv to drive the environment.
 */
#include "port_env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

int main(void) {
    /* --- bool semantics: unset -> default --- */
    unsetenv("GE007_TEST_B");
    CHECK(port_env_bool("GE007_TEST_B", 1, "default-on gate") == 1, "unset bool -> default_on=1");
    unsetenv("GE007_TEST_B2");
    CHECK(port_env_bool("GE007_TEST_B2", 0, "default-off gate") == 0, "unset bool -> default_on=0");

    /* "0" is always off regardless of default; other values on. */
    setenv("GE007_TEST_B3", "0", 1);
    CHECK(port_env_bool("GE007_TEST_B3", 1, NULL) == 0, "\"0\" -> off even when default on");
    setenv("GE007_TEST_B4", "1", 1);
    CHECK(port_env_bool("GE007_TEST_B4", 0, NULL) == 1, "\"1\" -> on");
    setenv("GE007_TEST_B5", "anything", 1);
    CHECK(port_env_bool("GE007_TEST_B5", 0, NULL) == 1, "non-zero string -> on");

    /* Read-once cache: changing the env after first read must NOT change value. */
    setenv("GE007_TEST_CACHE", "1", 1);
    CHECK(port_env_bool("GE007_TEST_CACHE", 0, NULL) == 1, "first read sees 1");
    setenv("GE007_TEST_CACHE", "0", 1);
    CHECK(port_env_bool("GE007_TEST_CACHE", 0, NULL) == 1, "cached: still 1 after env change");

    /* --- int semantics --- */
    unsetenv("GE007_TEST_I");
    CHECK(port_env_int("GE007_TEST_I", 42, "int knob") == 42, "unset int -> default");
    setenv("GE007_TEST_I2", "7", 1);
    CHECK(port_env_int("GE007_TEST_I2", 0, NULL) == 7, "valid int parse");
    setenv("GE007_TEST_I3", "notanumber", 1);
    CHECK(port_env_int("GE007_TEST_I3", 99, NULL) == 99, "invalid int -> default");
    setenv("GE007_TEST_I4", "0x10", 1);
    CHECK(port_env_int("GE007_TEST_I4", 0, NULL) == 16, "hex int (base 0) parse");

    /* --- float semantics --- */
    unsetenv("GE007_TEST_F");
    CHECK(port_env_float("GE007_TEST_F", 1.5f, "float knob") == 1.5f, "unset float -> default");
    setenv("GE007_TEST_F2", "2.25", 1);
    CHECK(port_env_float("GE007_TEST_F2", 0.0f, NULL) == 2.25f, "valid float parse");
    setenv("GE007_TEST_F3", "junk", 1);
    CHECK(port_env_float("GE007_TEST_F3", 3.5f, NULL) == 3.5f, "invalid float -> default");

    /* --- presence semantics: port_env_set == (getenv(name) != NULL) exactly ---
     * This is the behavior contract the raw-getenv presence-site migration relies
     * on: "0" and "" (empty) still count as SET, unlike port_env_bool. */
    unsetenv("GE007_TEST_S");
    CHECK(port_env_set("GE007_TEST_S", "presence gate") == 0, "unset presence -> 0");
    setenv("GE007_TEST_S1", "1", 1);
    CHECK(port_env_set("GE007_TEST_S1", NULL) == 1, "set \"1\" -> 1");
    setenv("GE007_TEST_S0", "0", 1);
    CHECK(port_env_set("GE007_TEST_S0", NULL) == 1, "set \"0\" -> 1 (presence: 0 counts as set)");
    setenv("GE007_TEST_SE", "", 1);
    CHECK(port_env_set("GE007_TEST_SE", NULL) == 1, "set \"\" (empty) -> 1 (present)");
    setenv("GE007_TEST_SX", "anything", 1);
    CHECK(port_env_set("GE007_TEST_SX", NULL) == 1, "set non-empty -> 1");
    /* read-once cache: unsetting after first read must NOT change the value. */
    setenv("GE007_TEST_SCACHE", "x", 1);
    CHECK(port_env_set("GE007_TEST_SCACHE", NULL) == 1, "presence first read sees set");
    unsetenv("GE007_TEST_SCACHE");
    CHECK(port_env_set("GE007_TEST_SCACHE", NULL) == 1, "cached: still set after unset");
    /* each presence read equals a fresh getenv()!=NULL for the same input. */
    unsetenv("GE007_TEST_SEQ_A"); setenv("GE007_TEST_SEQ_B", "0", 1);
    CHECK(port_env_set("GE007_TEST_SEQ_A", NULL) == (getenv("GE007_TEST_SEQ_A") != NULL),
          "presence matches getenv!=NULL (unset)");
    CHECK(port_env_set("GE007_TEST_SEQ_B", NULL) == (getenv("GE007_TEST_SEQ_B") != NULL),
          "presence matches getenv!=NULL (set to 0)");

    /* --- registry: distinct names counted once --- */
    int before = port_env_registered_count();
    (void)port_env_bool("GE007_TEST_B", 1, NULL);   /* already registered */
    CHECK(port_env_registered_count() == before, "re-access does not re-register");

    /* --- dump produces non-empty catalog in both formats --- */
    FILE *devnull = fopen("/dev/null", "w");
    if (devnull) {
        port_env_dump(devnull, "text");
        port_env_dump(devnull, "md");
        fclose(devnull);
    }
    CHECK(port_env_registered_count() > 0, "registry non-empty after accesses");

    if (g_failures == 0) {
        printf("PASS: port_env (%d flags registered)\n", port_env_registered_count());
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}

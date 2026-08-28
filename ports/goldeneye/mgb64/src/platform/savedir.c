/**
 * savedir.c — Smart save directory detection for the GE007 PC port.
 *
 * Priority:
 *   1. --savedir <path> command-line override
 *   2. CWD if ge007.ini or ge007_eeprom.bin already exists there (portable mode)
 *   3. (Windows only) %APPDATA%\ge007\ (created on demand)
 *   4. CWD if writable (first launch in project dir; real write-probe on
 *      Windows, not just the read-only attribute -- see dir_writable())
 *   5. $HOME/.ge007/ or %USERPROFILE%\.ge007\ (created on demand)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>  /* _getpid(), used by the write-probe below */
#ifndef W_OK
#define W_OK 2
#endif
#else
#include <unistd.h>
#endif

#define SAVEDIR_MAX_PATH 1024
#define SAVEDIR_NAME     ".ge007"
#define SAVEDIR_NAME_WIN "ge007" /* %APPDATA%\ge007 -- no leading dot; NTFS/Explorer
                                   * don't treat dot-prefixes as hidden like POSIX does */

static char s_saveDir[SAVEDIR_MAX_PATH];
static char s_pathBuf[SAVEDIR_MAX_PATH];
static int  s_initialized;

static int dir_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static int dir_writable(const char *path) {
#ifdef _WIN32
    /* On msvcrt, access(path, W_OK) applied to a *directory* only checks the
     * read-only DOS attribute -- it ignores ACLs entirely. That means it
     * reports "writable" for e.g. C:\Program Files even when the user has
     * no write permission there, so the CWD priority below would silently
     * "succeed" and saves would then fail (or land somewhere unexpected)
     * with no diagnostic. Probe for real by creating (and deleting) a
     * uniquely-named temp file in the directory. */
    char probe[SAVEDIR_MAX_PATH];
    size_t plen = strlen(path);
    int has_sep = (plen > 0 && (path[plen - 1] == '/' || path[plen - 1] == '\\'));
    int n;
    FILE *f;

    n = snprintf(probe, sizeof(probe), "%s%sge007_wtest_%d.tmp",
        path, has_sep ? "" : "/", (int)_getpid());
    if (n <= 0 || (size_t)n >= sizeof(probe)) return 0;

    f = fopen(probe, "wb");
    if (!f) return 0;
    {
        int ok = (fclose(f) == 0);
        remove(probe);
        return ok;
    }
#else
    return (access(path, W_OK) == 0);
#endif
}

static int ensure_dir(const char *path) {
    if (dir_exists(path)) return 1;
#ifdef _WIN32
    return (_mkdir(path) == 0);
#else
    return (mkdir(path, 0755) == 0);
#endif
}

/* Returns 0 when the save directory is resolved, -1 only when an EXPLICIT
 * override was requested but is unusable (uncreatable / not writable) — a user
 * error the caller must surface instead of silently losing every save
 * (AUDIT-0054). The auto-selection paths (override NULL) always resolve (they
 * have a CWD fallback) and return 0. Idempotent: a second call is a no-op. */
int savedirInit(const char *savedir_override)
{
    if (s_initialized) return 0;

    /* Priority 1: explicit override */
    if (savedir_override && savedir_override[0]) {
        snprintf(s_saveDir, SAVEDIR_MAX_PATH, "%s", savedir_override);
        /* Ensure trailing slash */
        size_t len = strlen(s_saveDir);
        if (len > 0 && s_saveDir[len - 1] != '/') {
            if (len + 1 < SAVEDIR_MAX_PATH) {
                s_saveDir[len] = '/';
                s_saveDir[len + 1] = '\0';
            }
        }
        /* An EXPLICIT override must actually be usable — an unwritable or
         * uncreatable path is a user error, not something to accept silently and
         * then fail every save against (AUDIT-0054). Verify create + write, and
         * fail closed so the caller can exit nonzero rather than "succeed" while
         * printing "Using override" + "save failed". Do NOT set s_initialized on
         * failure, so a corrected retry can re-run. */
        if (!ensure_dir(s_saveDir) || !dir_writable(s_saveDir)) {
            fprintf(stderr,
                    "[SAVEDIR] ERROR: save directory '%s' cannot be created or is not "
                    "writable; refusing to continue (saves would be lost).\n", s_saveDir);
            s_saveDir[0] = '\0';
            return -1;
        }
        s_initialized = 1;
        printf("[SAVEDIR] Using override: %s\n", s_saveDir);
        return 0;
    }

    s_initialized = 1;

    /* Priority 2: CWD if save files already exist (portable mode) */
    if (file_exists("ge007_eeprom.bin") || file_exists("ge007.ini")) {
        s_saveDir[0] = '\0'; /* empty = CWD-relative */
        printf("[SAVEDIR] Using CWD (existing save files found)\n");
        return 0;
    }

#ifdef _WIN32
    /* Priority 3 (Windows only): %APPDATA%\ge007\ -- the platform-conventional
     * per-user writable location. Preferred *ahead of* the CWD-writable probe
     * below: a first launch from an install directory (Program Files or
     * otherwise) should land saves in the expected per-user location rather
     * than next to the executable. */
    {
        const char *appdata = getenv("APPDATA");
        if (appdata) {
            char candidate[SAVEDIR_MAX_PATH];
            snprintf(candidate, SAVEDIR_MAX_PATH, "%s/%s/", appdata, SAVEDIR_NAME_WIN);
            if (ensure_dir(candidate)) {
                snprintf(s_saveDir, SAVEDIR_MAX_PATH, "%s", candidate);
                printf("[SAVEDIR] Using %s\n", s_saveDir);
                return 0;
            }
        }
    }
#endif

    /* Priority 4: CWD if writable (first launch from project directory) */
    if (dir_writable(".")) {
        s_saveDir[0] = '\0'; /* empty = CWD-relative */
        printf("[SAVEDIR] Using CWD\n");
        return 0;
    }

    /* Priority 5: $HOME/.ge007/ (POSIX), or %USERPROFILE%\.ge007\ as a
     * Windows fallback if %APPDATA% wasn't set above) */
    {
        const char *home = getenv("HOME");
        if (!home) home = getenv("USERPROFILE"); /* Windows fallback */
        if (home) {
            snprintf(s_saveDir, SAVEDIR_MAX_PATH, "%s/%s/", home, SAVEDIR_NAME);
            if (ensure_dir(s_saveDir)) {
                printf("[SAVEDIR] Using %s\n", s_saveDir);
                return 0;
            }
        }
    }

    /* Fallback: CWD (may fail on writes, but best we can do) */
    s_saveDir[0] = '\0';
    printf("[SAVEDIR] Falling back to CWD\n");
    return 0;
}

const char *savedirPath(const char *filename)
{
    if (!s_initialized) {
        savedirInit(NULL);
    }

    if (s_saveDir[0]) {
        snprintf(s_pathBuf, SAVEDIR_MAX_PATH, "%s%s", s_saveDir, filename);
    } else {
        snprintf(s_pathBuf, SAVEDIR_MAX_PATH, "%s", filename);
    }
    return s_pathBuf;
}

const char *savedirGet(void)
{
    if (!s_initialized) {
        savedirInit(NULL);
    }
    return s_saveDir;
}

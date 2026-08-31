#include "titan_game_gsm.h"
#include "titan_diagnostics.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITAN_GAME_GSM_DIR TITAN_STORAGE_DEVICE_ROOT "/games"
#define TITAN_GAME_GSM_PATH_CAPACITY 96u
#define TITAN_GAME_GSM_STARTUP_CAPACITY 16u
#define TITAN_UL_GOLDEN_CFG_DIR "mass:/CFG"
#define TITAN_UL_GOLDEN_CFG_PATH_CAPACITY 128u
#define TITAN_UL_GOLDEN_CFG_MAX_BYTES (32u * 1024u)

typedef struct TitanGameGsmCache
{
    int valid;
    char startup[TITAN_GAME_GSM_STARTUP_CAPACITY];
    TitanPerGameGsmMode mode;
} TitanGameGsmCache;

static TitanGameGsmCache g_game_gsm_cache;

static size_t TitanGameGsm_BoundedLength(const char *text, size_t capacity)
{
    size_t length;

    if (text == 0)
        return 0u;

    for (length = 0u; length < capacity && text[length] != '\0'; ++length)
    {
    }
    return length;
}

int TitanGameGsm_IsValidStartup(const char *startup)
{
    size_t length;
    size_t index;

    if (startup == 0)
        return 0;

    length = TitanGameGsm_BoundedLength(startup, TITAN_GAME_GSM_STARTUP_CAPACITY);
    if (length == 0u || length >= TITAN_GAME_GSM_STARTUP_CAPACITY)
        return 0;

    for (index = 0u; index < length; ++index)
    {
        unsigned char ch = (unsigned char)startup[index];
        if (!(isalnum(ch) || ch == '_' || ch == '-' || ch == '.'))
            return 0;
    }
    return 1;
}

static int TitanGameGsm_BuildPath(
    const char *startup,
    const char *suffix,
    char *destination,
    size_t capacity
)
{
    int written;

    if (!TitanGameGsm_IsValidStartup(startup) || destination == 0 ||
        capacity == 0u || suffix == 0)
        return 0;

    written = snprintf(
        destination,
        capacity,
        TITAN_GAME_GSM_DIR "/%s.gsm%s",
        startup,
        suffix
    );
    return written >= 0 && (size_t)written < capacity;
}

static int TitanGameGsm_EnsureDirectoryPath(const char *path)
{
    int is_directory;

    if (TitanStorageIo_PathInfo64(path, &is_directory, 0))
        return is_directory;

    if (TitanStorageIo_CreateDirectory(path) == 0)
        return 1;

    /*
     * FileXio does not reliably mirror IOP errno into EE errno.  In
     * particular, mkdir() on an already-existing FAT directory may return
     * failure while EE errno is not EEXIST.  Re-probe the path instead of
     * making persistence depend on errno propagation.
     */
    return TitanStorageIo_PathInfo64(path, &is_directory, 0) && is_directory;
}

static int TitanGameGsm_EnsureDirectory(void)
{
    return
        TitanGameGsm_EnsureDirectoryPath(TITAN_STORAGE_DEVICE_ROOT) &&
        TitanGameGsm_EnsureDirectoryPath(TITAN_GAME_GSM_DIR);
}

static void TitanGameGsm_UpdateCache(
    const char *startup,
    TitanPerGameGsmMode mode
)
{
    memset(&g_game_gsm_cache, 0, sizeof(g_game_gsm_cache));
    if (!TitanGameGsm_IsValidStartup(startup))
        return;

    strncpy(
        g_game_gsm_cache.startup,
        startup,
        sizeof(g_game_gsm_cache.startup) - 1u
    );
    g_game_gsm_cache.mode = mode;
    g_game_gsm_cache.valid = 1;
}

void TitanGameGsm_InvalidateCache(void)
{
    memset(&g_game_gsm_cache, 0, sizeof(g_game_gsm_cache));
}

TitanPerGameGsmMode TitanGameGsm_GetMode(const char *startup)
{
    char path[TITAN_GAME_GSM_PATH_CAPACITY];
    char encoded;
    int descriptor;
    TitanPerGameGsmMode mode;

    if (!TitanGameGsm_IsValidStartup(startup))
        return TITAN_PER_GAME_GSM_DEFAULT;

    if (g_game_gsm_cache.valid &&
        strcmp(g_game_gsm_cache.startup, startup) == 0)
        return g_game_gsm_cache.mode;

    mode = TITAN_PER_GAME_GSM_DEFAULT;
    if (!TitanGameGsm_BuildPath(startup, "", path, sizeof(path)))
        return mode;

    descriptor = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (descriptor >= 0)
    {
        if (TitanStorageIo_ReadSome(descriptor, &encoded, 1u) == 1 &&
            encoded >= '1' && encoded <= '3')
            mode = (TitanPerGameGsmMode)(encoded - '0');
        TitanStorageIo_Close(descriptor);
    }

    TitanGameGsm_UpdateCache(startup, mode);
    return mode;
}

enum
{
    TITAN_GAME_GSM_SAVE_STAGE_VALIDATE = 1,
    TITAN_GAME_GSM_SAVE_STAGE_PATHS,
    TITAN_GAME_GSM_SAVE_STAGE_DIRECTORY,
    TITAN_GAME_GSM_SAVE_STAGE_OPEN,
    TITAN_GAME_GSM_SAVE_STAGE_WRITE,
    TITAN_GAME_GSM_SAVE_STAGE_REPLACE,
    TITAN_GAME_GSM_SAVE_STAGE_COMPLETE
};

static void TitanGameGsm_LogSave(
    int success,
    unsigned int stage,
    TitanPerGameGsmMode mode,
    int detail
)
{
#if defined(__EE__) || defined(_EE)
    TitanDiagnostics_Event(
        success ? TITAN_DIAG_INFO : TITAN_DIAG_WARN,
        TITAN_DIAG_STORAGE,
        0x0341u,
        success ? 0 : -1,
        stage,
        (uint32_t)mode,
        (uint32_t)detail,
        0u
    );
#else
    (void)success;
    (void)stage;
    (void)mode;
    (void)detail;
#endif
}

int TitanGameGsm_SetMode(const char *startup, TitanPerGameGsmMode mode)
{
    char target[TITAN_GAME_GSM_PATH_CAPACITY];
    char temporary[TITAN_GAME_GSM_PATH_CAPACITY];
    char backup[TITAN_GAME_GSM_PATH_CAPACITY];
    char encoded;
    int descriptor;
    int ok;
    int detail;

    if (!TitanGameGsm_IsValidStartup(startup) ||
        mode < TITAN_PER_GAME_GSM_DEFAULT ||
        mode >= TITAN_PER_GAME_GSM_MODE_COUNT)
    {
        TitanGameGsm_LogSave(
            0,
            TITAN_GAME_GSM_SAVE_STAGE_VALIDATE,
            mode,
            errno
        );
        return 0;
    }

    if (!TitanGameGsm_BuildPath(startup, "", target, sizeof(target)) ||
        !TitanGameGsm_BuildPath(startup, ".tmp", temporary, sizeof(temporary)) ||
        !TitanGameGsm_BuildPath(startup, ".bak", backup, sizeof(backup)))
    {
        TitanGameGsm_LogSave(
            0,
            TITAN_GAME_GSM_SAVE_STAGE_PATHS,
            mode,
            errno
        );
        return 0;
    }

    if (!TitanGameGsm_EnsureDirectory())
    {
        TitanGameGsm_LogSave(
            0,
            TITAN_GAME_GSM_SAVE_STAGE_DIRECTORY,
            mode,
            errno
        );
        return 0;
    }

    /* Persist Default explicitly as '0' so the per-title choice remains
     * deterministic across reboots. Engine-specific launch preparation stays
     * outside the Golden runtime binaries. */
    encoded = (char)('0' + (int)mode);
    descriptor = TitanStorageIo_Open(
        temporary,
        TITAN_STORAGE_IO_WRITE |
        TITAN_STORAGE_IO_CREATE |
        TITAN_STORAGE_IO_TRUNCATE
    );
    if (descriptor < 0)
    {
        TitanGameGsm_LogSave(
            0,
            TITAN_GAME_GSM_SAVE_STAGE_OPEN,
            mode,
            errno
        );
        return 0;
    }

    ok = TitanStorageIo_WriteExact(descriptor, &encoded, 1u);
    if (ok && TitanStorageIo_Flush(descriptor) < 0)
        ok = 0;
    detail = errno;
    if (TitanStorageIo_Close(descriptor) < 0)
    {
        detail = errno;
        ok = 0;
    }
    if (!ok)
    {
        (void)TitanStorageIo_Remove(temporary);
        TitanGameGsm_LogSave(
            0,
            TITAN_GAME_GSM_SAVE_STAGE_WRITE,
            mode,
            detail
        );
        return 0;
    }

    if (!TitanStorageIo_AtomicReplace(temporary, target, backup))
    {
        detail = errno;
        (void)TitanStorageIo_Remove(temporary);
        TitanGameGsm_LogSave(
            0,
            TITAN_GAME_GSM_SAVE_STAGE_REPLACE,
            mode,
            detail
        );
        return 0;
    }
    (void)TitanStorageIo_Remove(backup);

    TitanGameGsm_UpdateCache(startup, mode);
    TitanGameGsm_LogSave(
        1,
        TITAN_GAME_GSM_SAVE_STAGE_COMPLETE,
        mode,
        0
    );
    return 1;
}

TitanPerGameGsmMode TitanGameGsm_CycleMode(
    const char *startup,
    int direction
)
{
    int next;
    TitanPerGameGsmMode current;

    current = TitanGameGsm_GetMode(startup);
    next = (int)current + (direction < 0 ? -1 : 1);
    if (next < 0)
        next = (int)TITAN_PER_GAME_GSM_MODE_COUNT - 1;
    if (next >= (int)TITAN_PER_GAME_GSM_MODE_COUNT)
        next = 0;

    if (!TitanGameGsm_SetMode(startup, (TitanPerGameGsmMode)next))
        return current;
    return (TitanPerGameGsmMode)next;
}

const char *TitanGameGsm_GetModeName(TitanPerGameGsmMode mode)
{
    switch (mode)
    {
        case TITAN_PER_GAME_GSM_480P:
            return "480p";
        case TITAN_PER_GAME_GSM_720P:
            return "720p";
        case TITAN_PER_GAME_GSM_1080I:
            return "1080i";
        case TITAN_PER_GAME_GSM_DEFAULT:
        default:
            return "Default";
    }
}

const char *TitanGameGsm_GetNeutrinoArgument(TitanPerGameGsmMode mode)
{
    /*
     * Hardware Golden Neutrino is immutable. Feed only switches already
     * understood by its validated parser. 720p is intentionally unsupported
     * for Neutrino and therefore falls back to native/default.
     */
    switch (mode)
    {
        case TITAN_PER_GAME_GSM_480P: return "-gsm=fp2";
        case TITAN_PER_GAME_GSM_1080I: return "-gsm=1080ix2";
        case TITAN_PER_GAME_GSM_720P:
        case TITAN_PER_GAME_GSM_DEFAULT:
        default:
            return 0;
    }
}

static int TitanGameGsm_IsOplGsmLine(const char *line, size_t length)
{
    static const char *keys[] = {
        "$GSMSource=", "$EnableGSM=", "$GSMVMode=",
        "$GSMXOffset=", "$GSMYOffset=", "$GSMFIELDFix="
    };
    unsigned int index;

    while (length > 0u && (*line == ' ' || *line == '\t'))
    {
        ++line;
        --length;
    }

    for (index = 0u; index < sizeof(keys) / sizeof(keys[0]); ++index)
    {
        size_t key_length = strlen(keys[index]);
        if (length >= key_length && strncmp(line, keys[index], key_length) == 0)
            return 1;
    }
    return 0;
}

static int TitanGameGsm_AppendText(
    char *output,
    size_t capacity,
    size_t *used,
    const char *text
)
{
    size_t length;

    if (output == 0 || used == 0 || text == 0)
        return 0;

    length = strlen(text);
    if (*used + length >= capacity)
        return 0;

    memcpy(output + *used, text, length);
    *used += length;
    output[*used] = '\0';
    return 1;
}

int TitanGameGsm_PrepareUlGoldenConfig(
    const char *startup,
    TitanPerGameGsmMode mode
)
{
    char target[TITAN_UL_GOLDEN_CFG_PATH_CAPACITY];
    char temporary[TITAN_UL_GOLDEN_CFG_PATH_CAPACITY];
    char backup[TITAN_UL_GOLDEN_CFG_PATH_CAPACITY];
    char *input = 0;
    char *output = 0;
    TitanStorageIoSize existing_size = 0u;
    size_t used = 0u;
    size_t cursor = 0u;
    int descriptor = -1;
    int is_directory = 0;
    int ok = 0;
    int vmode = 0;
    int written;

    if (!TitanGameGsm_IsValidStartup(startup) ||
        mode < TITAN_PER_GAME_GSM_DEFAULT ||
        mode >= TITAN_PER_GAME_GSM_MODE_COUNT)
        return 0;

    if (!TitanGameGsm_EnsureDirectoryPath(TITAN_UL_GOLDEN_CFG_DIR))
        return 0;

    written = snprintf(
        target,
        sizeof(target),
        TITAN_UL_GOLDEN_CFG_DIR "/%s.cfg",
        startup
    );
    if (written < 0 || (size_t)written >= sizeof(target))
        return 0;

    written = snprintf(
        temporary,
        sizeof(temporary),
        TITAN_UL_GOLDEN_CFG_DIR "/%s.cfg.titan-gsm.tmp",
        startup
    );
    if (written < 0 || (size_t)written >= sizeof(temporary))
        return 0;

    written = snprintf(
        backup,
        sizeof(backup),
        TITAN_UL_GOLDEN_CFG_DIR "/%s.cfg.titan-gsm.bak",
        startup
    );
    if (written < 0 || (size_t)written >= sizeof(backup))
        return 0;

    if (!TitanStorageIo_PathInfo64(target, &is_directory, &existing_size))
        existing_size = 0u;
    if (is_directory || existing_size > TITAN_UL_GOLDEN_CFG_MAX_BYTES)
        return 0;

    input = (char *)malloc((size_t)existing_size + 1u);
    output = (char *)malloc((size_t)existing_size + 256u);
    if (input == 0 || output == 0)
        goto cleanup;
    input[0] = '\0';
    output[0] = '\0';

    if (existing_size > 0u)
    {
        descriptor = TitanStorageIo_Open(target, TITAN_STORAGE_IO_READ);
        if (descriptor < 0 ||
            !TitanStorageIo_ReadExact(descriptor, input, (size_t)existing_size))
            goto cleanup;
        if (TitanStorageIo_Close(descriptor) < 0)
        {
            descriptor = -1;
            goto cleanup;
        }
        descriptor = -1;
        input[(size_t)existing_size] = '\0';
    }

    while (cursor < (size_t)existing_size)
    {
        size_t start = cursor;
        size_t length;

        while (cursor < (size_t)existing_size && input[cursor] != '\n')
            ++cursor;
        if (cursor < (size_t)existing_size)
            ++cursor;
        length = cursor - start;

        if (!TitanGameGsm_IsOplGsmLine(input + start, length))
        {
            if (used + length + 1u >= (size_t)existing_size + 256u)
                goto cleanup;
            memcpy(output + used, input + start, length);
            used += length;
            output[used] = '\0';
        }
    }

    if (used > 0u && output[used - 1u] != '\n' &&
        !TitanGameGsm_AppendText(
            output,
            (size_t)existing_size + 256u,
            &used,
            "\r\n"
        ))
        goto cleanup;

    if (!TitanGameGsm_AppendText(
        output,
        (size_t)existing_size + 256u,
        &used,
        "$GSMSource=1\r\n"
    ))
        goto cleanup;

    if (!TitanGameGsm_AppendText(
        output,
        (size_t)existing_size + 256u,
        &used,
        mode == TITAN_PER_GAME_GSM_DEFAULT
            ? "$EnableGSM=0\r\n"
            : "$EnableGSM=1\r\n"
    ))
        goto cleanup;

    switch (mode)
    {
        case TITAN_PER_GAME_GSM_480P: vmode = 8; break;
        case TITAN_PER_GAME_GSM_720P: vmode = 10; break;
        case TITAN_PER_GAME_GSM_1080I: vmode = 11; break;
        case TITAN_PER_GAME_GSM_DEFAULT:
        default:
            vmode = 0;
            break;
    }

    if (vmode != 0)
    {
        char line[32];
        written = snprintf(line, sizeof(line), "$GSMVMode=%d\r\n", vmode);
        if (written < 0 || (size_t)written >= sizeof(line) ||
            !TitanGameGsm_AppendText(
                output,
                (size_t)existing_size + 256u,
                &used,
                line
            ))
            goto cleanup;
    }

    descriptor = TitanStorageIo_Open(
        temporary,
        TITAN_STORAGE_IO_WRITE |
        TITAN_STORAGE_IO_CREATE |
        TITAN_STORAGE_IO_TRUNCATE
    );
    if (descriptor < 0)
        goto cleanup;

    if (!TitanStorageIo_WriteExact(descriptor, output, used) ||
        TitanStorageIo_Flush(descriptor) < 0)
        goto cleanup;

    if (TitanStorageIo_Close(descriptor) < 0)
    {
        descriptor = -1;
        goto cleanup;
    }
    descriptor = -1;

    if (!TitanStorageIo_AtomicReplace(temporary, target, backup))
        goto cleanup;

    (void)TitanStorageIo_Remove(backup);
    ok = 1;

cleanup:
    if (descriptor >= 0)
        (void)TitanStorageIo_Close(descriptor);
    if (!ok)
        (void)TitanStorageIo_Remove(temporary);
    free(input);
    free(output);
    return ok;
}

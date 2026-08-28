#include "titan_storage_io.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__EE__) || defined(_EE)

/*
 * The PS2SDK newlib port requires file access through the POSIX API. The
 * FileXio client still has to be initialized after the kernel reloads the
 * IOMANX/FileXio IRX pair, but no legacy fileXio* I/O call is used below.
 */
#ifndef NEWLIB_PORT_AWARE
#define NEWLIB_PORT_AWARE
#define TITAN_STORAGE_IO_DEFINED_NEWLIB_PORT_AWARE
#endif
#include <fileXio_rpc.h>
#ifdef TITAN_STORAGE_IO_DEFINED_NEWLIB_PORT_AWARE
#undef TITAN_STORAGE_IO_DEFINED_NEWLIB_PORT_AWARE
#undef NEWLIB_PORT_AWARE
#endif

#endif

#define TITAN_STORAGE_IO_MAX_STALLS 4u
#define TITAN_STORAGE_IO_PRIMARY_PREFIX "mass:/"
#define TITAN_STORAGE_IO_ALIAS_COUNT 10u
#define TITAN_STORAGE_IO_PATH_CAPACITY 512u

/* Recent USB stacks may expose the first device as mass0: while older stacks
 * also publish the mass: alias. Project Titan's persistent on-disk contract
 * deliberately remains mass:/TITAN. The I/O boundary translates only after a
 * primary operation fails, keeping existing paths and snapshots compatible. */
static int TitanStorageIo_BuildAliasPath(
    const char *path,
    unsigned int alias_index,
    char *fallback,
    size_t capacity
)
{
    const size_t primary_length = sizeof(TITAN_STORAGE_IO_PRIMARY_PREFIX) - 1u;
    const char *suffix;
    int written;

    if (path == 0 || fallback == 0 || capacity == 0u ||
        alias_index >= TITAN_STORAGE_IO_ALIAS_COUNT ||
        strncmp(path, TITAN_STORAGE_IO_PRIMARY_PREFIX, primary_length) != 0)
    {
        return 0;
    }

    suffix = path + primary_length;
    written = snprintf(fallback, capacity, "mass%u:/%s", alias_index, suffix);
    if (written < 0 || (size_t)written >= capacity)
    {
        errno = ENAMETOOLONG;
        return 0;
    }
    return 1;
}

#if defined(TITAN_STORAGE_IO_TESTING)
static size_t g_test_max_transfer;

void TitanStorageIo_TestSetMaxTransfer(size_t max_transfer)
{
    g_test_max_transfer = max_transfer;
}

static size_t TitanStorageIo_LimitTransferForTest(size_t size)
{
    if (g_test_max_transfer != 0u && size > g_test_max_transfer)
    {
        return g_test_max_transfer;
    }
    return size;
}
#else
static size_t TitanStorageIo_LimitTransferForTest(size_t size)
{
    return size;
}
#endif

static int TitanStorageIo_TranslateFlags(int flags)
{
    int translated;

    if (
        (flags & TITAN_STORAGE_IO_READ) != 0 &&
        (flags & TITAN_STORAGE_IO_WRITE) != 0
    )
    {
        translated = O_RDWR;
    }
    else if ((flags & TITAN_STORAGE_IO_WRITE) != 0)
    {
        translated = O_WRONLY;
    }
    else
    {
        translated = O_RDONLY;
    }

    if ((flags & TITAN_STORAGE_IO_CREATE) != 0)
    {
        translated |= O_CREAT;
    }

    if ((flags & TITAN_STORAGE_IO_TRUNCATE) != 0)
    {
        translated |= O_TRUNC;
    }

    if ((flags & TITAN_STORAGE_IO_EXCLUSIVE) != 0)
    {
        translated |= O_EXCL;
    }

    return translated;
}

int TitanStorageIo_InitializeClient(void)
{
#if defined(__EE__) || defined(_EE)
    return fileXioInit() >= 0;
#else
    return 1;
#endif
}

int TitanStorageIo_Open(
    const char *path,
    int flags
)
{
    char fallback[TITAN_STORAGE_IO_PATH_CAPACITY];
    unsigned int alias_index;
    int descriptor;
    int translated_flags;

    if (path == 0 || path[0] == '\0')
    {
        errno = EINVAL;
        return -1;
    }

    translated_flags = TitanStorageIo_TranslateFlags(flags);
    descriptor = open(path, translated_flags, 0666);
    for (alias_index = 0u;
         descriptor < 0 && alias_index < TITAN_STORAGE_IO_ALIAS_COUNT;
         ++alias_index)
    {
        if (TitanStorageIo_BuildAliasPath(path, alias_index, fallback, sizeof(fallback)))
        {
            descriptor = open(fallback, translated_flags, 0666);
        }
    }
    return descriptor;
}

int TitanStorageIo_Close(int descriptor)
{
    int result;

    do
    {
        result = close(descriptor);
    }
    while (result < 0 && errno == EINTR);

    return result;
}

TitanStorageIoResult TitanStorageIo_ReadSome(
    int descriptor,
    void *buffer,
    size_t size
)
{
    ssize_t result;

    if (buffer == 0 && size != 0u)
    {
        errno = EINVAL;
        return -1;
    }

    size = TitanStorageIo_LimitTransferForTest(size);

    do
    {
        result = read(descriptor, buffer, size);
    }
    while (result < 0 && errno == EINTR);

    return (TitanStorageIoResult)result;
}

TitanStorageIoResult TitanStorageIo_WriteSome(
    int descriptor,
    const void *buffer,
    size_t size
)
{
    ssize_t result;

    if (buffer == 0 && size != 0u)
    {
        errno = EINVAL;
        return -1;
    }

    size = TitanStorageIo_LimitTransferForTest(size);

    do
    {
        result = write(descriptor, buffer, size);
    }
    while (result < 0 && errno == EINTR);

    return (TitanStorageIoResult)result;
}

int TitanStorageIo_ReadExact(
    int descriptor,
    void *buffer,
    size_t size
)
{
    unsigned char *cursor;
    size_t remaining;
    unsigned int stalls;

    if (buffer == 0 && size != 0u)
    {
        errno = EINVAL;
        return 0;
    }

    cursor = (unsigned char *)buffer;
    remaining = size;
    stalls = 0u;

    while (remaining > 0u)
    {
        TitanStorageIoResult result;

        result = TitanStorageIo_ReadSome(descriptor, cursor, remaining);
        if (result < 0)
        {
            return 0;
        }
        if (result == 0)
        {
            stalls++;
            if (stalls >= TITAN_STORAGE_IO_MAX_STALLS)
            {
                errno = EIO;
                return 0;
            }
            continue;
        }

        if ((uint64_t)result > (uint64_t)remaining)
        {
            errno = EIO;
            return 0;
        }

        cursor += (size_t)result;
        remaining -= (size_t)result;
        stalls = 0u;
    }

    return 1;
}

int TitanStorageIo_WriteExact(
    int descriptor,
    const void *buffer,
    size_t size
)
{
    const unsigned char *cursor;
    size_t remaining;
    unsigned int stalls;

    if (buffer == 0 && size != 0u)
    {
        errno = EINVAL;
        return 0;
    }

    cursor = (const unsigned char *)buffer;
    remaining = size;
    stalls = 0u;

    while (remaining > 0u)
    {
        TitanStorageIoResult result;

        result = TitanStorageIo_WriteSome(descriptor, cursor, remaining);
        if (result < 0)
        {
            return 0;
        }
        if (result == 0)
        {
            stalls++;
            if (stalls >= TITAN_STORAGE_IO_MAX_STALLS)
            {
                errno = EIO;
                return 0;
            }
            continue;
        }

        if ((uint64_t)result > (uint64_t)remaining)
        {
            errno = EIO;
            return 0;
        }

        cursor += (size_t)result;
        remaining -= (size_t)result;
        stalls = 0u;
    }

    return 1;
}

TitanStorageIoOffset TitanStorageIo_Seek64(
    int descriptor,
    TitanStorageIoOffset offset,
    int origin
)
{
    off_t native_offset;
    off_t result;

    native_offset = (off_t)offset;
    if ((TitanStorageIoOffset)native_offset != offset)
    {
        errno = EOVERFLOW;
        return -1;
    }

    do
    {
        result = lseek(descriptor, native_offset, origin);
    }
    while (result < 0 && errno == EINTR);

    return (TitanStorageIoOffset)result;
}

int TitanStorageIo_Flush(int descriptor)
{
    int result;

    do
    {
        result = fsync(descriptor);
    }
    while (result < 0 && errno == EINTR);

#if defined(__EE__) || defined(_EE)
    /* Some PS2SDK/newlib combinations report ENOSYS even though close() still
     * pushes the FileXio buffer. Treat that single case as best-effort flush;
     * all other errors remain visible to the caller. */
    if (result < 0 && errno == ENOSYS)
    {
        return 0;
    }
#endif

    return result;
}

int TitanStorageIo_PathInfo64(
    const char *path,
    int *is_directory,
    TitanStorageIoSize *size
)
{
    char fallback[TITAN_STORAGE_IO_PATH_CAPACITY];
    struct stat info;
    unsigned int alias_index;
    int found;

    if (path == 0)
    {
        errno = EINVAL;
        return 0;
    }

    found = stat(path, &info) == 0;
    for (alias_index = 0u; !found && alias_index < TITAN_STORAGE_IO_ALIAS_COUNT; ++alias_index)
    {
        if (TitanStorageIo_BuildAliasPath(path, alias_index, fallback, sizeof(fallback)))
        {
            found = stat(fallback, &info) == 0;
        }
    }
    if (!found) return 0;

    if (is_directory != 0) *is_directory = S_ISDIR(info.st_mode) != 0;
    if (size != 0)
    {
        if (info.st_size < 0)
        {
            errno = EOVERFLOW;
            return 0;
        }
        *size = (TitanStorageIoSize)info.st_size;
    }
    return 1;
}

int TitanStorageIo_CreateDirectory(
    const char *path
)
{
    char fallback[TITAN_STORAGE_IO_PATH_CAPACITY];
    unsigned int alias_index;
    int result;

    if (path == 0 || path[0] == '\0')
    {
        errno = EINVAL;
        return -1;
    }
    result = mkdir(path, 0777);
    for (alias_index = 0u;
         result < 0 && errno != EEXIST && alias_index < TITAN_STORAGE_IO_ALIAS_COUNT;
         ++alias_index)
    {
        if (TitanStorageIo_BuildAliasPath(path, alias_index, fallback, sizeof(fallback)))
            result = mkdir(fallback, 0777);
    }
    return result;
}

int TitanStorageIo_Remove(
    const char *path
)
{
    char fallback[TITAN_STORAGE_IO_PATH_CAPACITY];
    unsigned int alias_index;
    int result;

    if (path == 0 || path[0] == '\0')
    {
        errno = EINVAL;
        return -1;
    }
    result = unlink(path);
    for (alias_index = 0u; result < 0 && alias_index < TITAN_STORAGE_IO_ALIAS_COUNT; ++alias_index)
    {
        if (TitanStorageIo_BuildAliasPath(path, alias_index, fallback, sizeof(fallback)))
            result = unlink(fallback);
    }
    return result;
}

int TitanStorageIo_Rename(
    const char *source,
    const char *destination
)
{
    char alias_source[TITAN_STORAGE_IO_PATH_CAPACITY];
    char alias_destination[TITAN_STORAGE_IO_PATH_CAPACITY];
    unsigned int alias_index;
    int result;

    if (source == 0 || source[0] == '\0' || destination == 0 || destination[0] == '\0')
    {
        errno = EINVAL;
        return -1;
    }
    result = rename(source, destination);
    for (alias_index = 0u; result < 0 && alias_index < TITAN_STORAGE_IO_ALIAS_COUNT; ++alias_index)
    {
        if (TitanStorageIo_BuildAliasPath(source, alias_index, alias_source, sizeof(alias_source)) &&
            TitanStorageIo_BuildAliasPath(destination, alias_index, alias_destination, sizeof(alias_destination)))
        {
            result = rename(alias_source, alias_destination);
        }
    }
    return result;
}

static void TitanStorageIo_CopyEntryName(
    char *destination,
    size_t capacity,
    const char *source
)
{
    size_t index = 0u;
    if (destination == 0 || capacity == 0u) return;
    while (source != 0 && source[index] != '\0' && index + 1u < capacity)
    {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int TitanStorageIo_JoinDirectoryEntry(
    char *out,
    size_t capacity,
    const char *directory,
    const char *name
)
{
    size_t directory_length;
    const char *separator;
    int written;

    if (out == 0 || capacity == 0u || directory == 0 || name == 0) return 0;
    directory_length = strlen(directory);
    separator = "";
    if (directory_length != 0u &&
        directory[directory_length - 1u] != '/' &&
        directory[directory_length - 1u] != ':')
    {
        separator = "/";
    }
    written = snprintf(out, capacity, "%s%s%s", directory, separator, name);
    return written >= 0 && (size_t)written < capacity;
}

int TitanStorageIo_ListDirectory(
    const char *path,
    unsigned int start_index,
    TitanStorageIoEntry *entries,
    unsigned int capacity
)
{
    char fallback[TITAN_STORAGE_IO_PATH_CAPACITY];
    const char *opened_path;
    unsigned int alias_index;
    DIR *directory;
    struct dirent *item;
    unsigned int logical_index;
    unsigned int produced;

    if (path == 0 || path[0] == '\0' || (entries == 0 && capacity != 0u))
    {
        errno = EINVAL;
        return -1;
    }

    opened_path = path;
    directory = opendir(path);
    for (alias_index = 0u; directory == 0 && alias_index < TITAN_STORAGE_IO_ALIAS_COUNT; ++alias_index)
    {
        if (TitanStorageIo_BuildAliasPath(path, alias_index, fallback, sizeof(fallback)))
        {
            directory = opendir(fallback);
            if (directory != 0) opened_path = fallback;
        }
    }
    if (directory == 0) return -1;

    logical_index = 0u;
    produced = 0u;
    while ((item = readdir(directory)) != 0)
    {
        char child[TITAN_STORAGE_IO_PATH_CAPACITY];
        struct stat info;
        TitanStorageIoEntry *entry;

        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
            continue;
        if (logical_index++ < start_index) continue;
        if (produced >= capacity) break;
        if (!TitanStorageIo_JoinDirectoryEntry(
                child, sizeof(child), opened_path, item->d_name))
            continue;
        if (stat(child, &info) < 0) continue;

        entry = &entries[produced++];
        memset(entry, 0, sizeof(*entry));
        TitanStorageIo_CopyEntryName(
            entry->name, sizeof(entry->name), item->d_name);
        entry->flags = S_ISDIR(info.st_mode) ? TITAN_STORAGE_IO_ENTRY_DIRECTORY : 0u;
        if (info.st_size >= 0) entry->size = (TitanStorageIoSize)info.st_size;
    }
    closedir(directory);
    return (int)produced;
}

int TitanStorageIo_RemoveAny(const char *path)
{
    char fallback[TITAN_STORAGE_IO_PATH_CAPACITY];
    struct stat info;
    unsigned int alias_index;

    if (path == 0 || path[0] == '\0')
    {
        errno = EINVAL;
        return -1;
    }

    if (stat(path, &info) == 0)
        return S_ISDIR(info.st_mode) ? rmdir(path) : unlink(path);

    for (alias_index = 0u; alias_index < TITAN_STORAGE_IO_ALIAS_COUNT; ++alias_index)
    {
        if (TitanStorageIo_BuildAliasPath(path, alias_index, fallback, sizeof(fallback)) &&
            stat(fallback, &info) == 0)
        {
            return S_ISDIR(info.st_mode) ? rmdir(fallback) : unlink(fallback);
        }
    }
    return -1;
}

int TitanStorageIo_AtomicReplace(
    const char *temporary_path,
    const char *target_path,
    const char *backup_path
)
{
    int target_exists;

    if (
        temporary_path == 0 || temporary_path[0] == '\0' ||
        target_path == 0 || target_path[0] == '\0'
    )
    {
        errno = EINVAL;
        return 0;
    }

    target_exists = TitanStorageIo_PathInfo64(target_path, 0, 0);

    if (backup_path != 0 && backup_path[0] != '\0')
    {
        TitanStorageIo_Remove(backup_path);

        if (
            target_exists &&
            TitanStorageIo_Rename(target_path, backup_path) < 0
        )
        {
            return 0;
        }
    }
    else if (target_exists)
    {
        /* FAT rename cannot portably replace an existing destination. A
         * backup-less replace is intentionally rejected so callers cannot
         * destroy the only known-good file before publishing the temporary. */
        errno = EEXIST;
        return 0;
    }

    if (TitanStorageIo_Rename(temporary_path, target_path) < 0)
    {
        if (
            target_exists &&
            backup_path != 0 && backup_path[0] != '\0'
        )
        {
            TitanStorageIo_Rename(backup_path, target_path);
        }
        return 0;
    }

    return 1;
}

int TitanStorageIo_Read(
    int descriptor,
    void *buffer,
    unsigned int size
)
{
    TitanStorageIoResult result;

    result = TitanStorageIo_ReadSome(descriptor, buffer, (size_t)size);
    if (result > INT_MAX)
    {
        errno = EOVERFLOW;
        return -1;
    }
    return (int)result;
}

int TitanStorageIo_Write(
    int descriptor,
    const void *buffer,
    unsigned int size
)
{
    TitanStorageIoResult result;

    result = TitanStorageIo_WriteSome(descriptor, buffer, (size_t)size);
    if (result > INT_MAX)
    {
        errno = EOVERFLOW;
        return -1;
    }
    return (int)result;
}

int TitanStorageIo_Seek(
    int descriptor,
    int offset,
    int origin
)
{
    TitanStorageIoOffset result;

    result = TitanStorageIo_Seek64(
        descriptor,
        (TitanStorageIoOffset)offset,
        origin
    );
    if (result > INT_MAX || result < INT_MIN)
    {
        errno = EOVERFLOW;
        return -1;
    }
    return (int)result;
}

int TitanStorageIo_PathInfo(
    const char *path,
    int *is_directory,
    unsigned int *size
)
{
    TitanStorageIoSize wide_size;

    if (!TitanStorageIo_PathInfo64(path, is_directory, size != 0 ? &wide_size : 0))
    {
        return 0;
    }

    if (size != 0)
    {
        if (wide_size > UINT_MAX)
        {
            errno = EOVERFLOW;
            return 0;
        }
        *size = (unsigned int)wide_size;
    }

    return 1;
}

#ifndef PROJECT_TITAN_STORAGE_IO_H
#define PROJECT_TITAN_STORAGE_IO_H

#include <stddef.h>
#include <stdint.h>

#define TITAN_STORAGE_IO_READ 0x01
#define TITAN_STORAGE_IO_WRITE 0x02
#define TITAN_STORAGE_IO_CREATE 0x04
#define TITAN_STORAGE_IO_TRUNCATE 0x08
#define TITAN_STORAGE_IO_EXCLUSIVE 0x10

#define TITAN_STORAGE_IO_SEEK_SET 0
#define TITAN_STORAGE_IO_SEEK_CURRENT 1
#define TITAN_STORAGE_IO_SEEK_END 2

/* Public I/O values are deliberately wider than the EE/newlib implementation.
 * This prevents silent truncation at subsystem boundaries and keeps all range
 * checks in one place. */
typedef int64_t TitanStorageIoOffset;
typedef uint64_t TitanStorageIoSize;
typedef int64_t TitanStorageIoResult;

int TitanStorageIo_InitializeClient(void);

int TitanStorageIo_Open(
    const char *path,
    int flags
);

int TitanStorageIo_Close(int descriptor);

TitanStorageIoResult TitanStorageIo_ReadSome(
    int descriptor,
    void *buffer,
    size_t size
);

TitanStorageIoResult TitanStorageIo_WriteSome(
    int descriptor,
    const void *buffer,
    size_t size
);

/* Exact helpers retry interrupted and partial transfers. They never spin on
 * zero progress: after a bounded number of stalls they fail. */
int TitanStorageIo_ReadExact(
    int descriptor,
    void *buffer,
    size_t size
);

int TitanStorageIo_WriteExact(
    int descriptor,
    const void *buffer,
    size_t size
);

TitanStorageIoOffset TitanStorageIo_Seek64(
    int descriptor,
    TitanStorageIoOffset offset,
    int origin
);

int TitanStorageIo_Flush(int descriptor);

int TitanStorageIo_PathInfo64(
    const char *path,
    int *is_directory,
    TitanStorageIoSize *size
);

int TitanStorageIo_CreateDirectory(
    const char *path
);

int TitanStorageIo_Remove(
    const char *path
);

int TitanStorageIo_Rename(
    const char *source,
    const char *destination
);

#define TITAN_STORAGE_IO_ENTRY_NAME_CAPACITY 128u
#define TITAN_STORAGE_IO_ENTRY_DIRECTORY 0x00000001u

typedef struct TitanStorageIoEntry
{
    char name[TITAN_STORAGE_IO_ENTRY_NAME_CAPACITY];
    TitanStorageIoSize size;
    unsigned int flags;
} TitanStorageIoEntry;

/* Paged directory enumeration. start_index counts logical entries after . and
 * ... Return the number of entries produced, 0 at end, or -1 on error. */
int TitanStorageIo_ListDirectory(
    const char *path,
    unsigned int start_index,
    TitanStorageIoEntry *entries,
    unsigned int capacity
);

/* Removes either a regular file or an empty directory. */
int TitanStorageIo_RemoveAny(const char *path);

/* Publish an already-written temporary file without destroying the last
 * known-good target first. When backup_path is non-null, a previous target is
 * retained until the caller has validated the new target and removes it. */
int TitanStorageIo_AtomicReplace(
    const char *temporary_path,
    const char *target_path,
    const char *backup_path
);

/* Compatibility wrappers for old call sites. New code should use the wide
 * and exact APIs above. They reject values that cannot be represented. */
int TitanStorageIo_Read(
    int descriptor,
    void *buffer,
    unsigned int size
);

int TitanStorageIo_Write(
    int descriptor,
    const void *buffer,
    unsigned int size
);

int TitanStorageIo_Seek(
    int descriptor,
    int offset,
    int origin
);

int TitanStorageIo_PathInfo(
    const char *path,
    int *is_directory,
    unsigned int *size
);

#if defined(TITAN_STORAGE_IO_TESTING)
void TitanStorageIo_TestSetMaxTransfer(size_t max_transfer);
#endif

#endif

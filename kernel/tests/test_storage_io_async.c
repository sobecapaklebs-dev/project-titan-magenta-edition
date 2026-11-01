#include "titan_storage_io.h"

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#undef NEWLIB_PORT_AWARE

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MOCK_CAPACITY 128u
#define MOCK_FD 7

typedef enum MockOperation { MOCK_NONE, MOCK_OPEN, MOCK_READ, MOCK_WRITE, MOCK_CLOSE, MOCK_RENAME } MockOperation;
static unsigned char g_bytes[MOCK_CAPACITY];
static size_t g_length;
static size_t g_cursor;
static int g_block_mode;
static MockOperation g_operation;
static void *g_buffer;
static const void *g_const_buffer;
static int g_size;
static int g_pending_polls;
static int g_result;

int fileXioInit(void) { return 0; }
int fileXioExit(void) { return 0; }
void fileXioSetBlockMode(int mode) { g_block_mode = mode; }
int fileXioWaitAsync(int mode, int *result)
{
    assert(mode == FXIO_NOWAIT);
    assert(g_operation != MOCK_NONE);
    if (g_pending_polls++ == 0) return FXIO_INCOMPLETE;
    switch (g_operation) {
        case MOCK_OPEN: g_result = MOCK_FD; break;
        case MOCK_READ: {
            size_t amount = (size_t)g_size;
            if (amount > g_length - g_cursor) amount = g_length - g_cursor;
            memcpy(g_buffer, g_bytes + g_cursor, amount);
            g_cursor += amount;
            g_result = (int)amount;
            break;
        }
        case MOCK_WRITE:
            assert(g_cursor + (size_t)g_size <= MOCK_CAPACITY);
            memcpy(g_bytes + g_cursor, g_const_buffer, (size_t)g_size);
            g_cursor += (size_t)g_size;
            if (g_cursor > g_length) g_length = g_cursor;
            g_result = g_size;
            break;
        case MOCK_CLOSE: g_result = 0; break;
        case MOCK_RENAME: g_result = 0; break;
        default: assert(0); break;
    }
    g_operation = MOCK_NONE;
    *result = g_result;
    return FXIO_COMPLETE;
}
int fileXioOpen(const char *name, int flags, ...)
{
    assert(name != NULL);
    assert(g_block_mode == FXIO_NOWAIT);
    if ((flags & FIO_O_TRUNC) != 0) { g_length = 0u; g_cursor = 0u; }
    g_operation = MOCK_OPEN; g_pending_polls = 0; return 0;
}
int fileXioClose(int fd)
{
    assert(fd == MOCK_FD && g_block_mode == FXIO_NOWAIT);
    g_operation = MOCK_CLOSE; g_pending_polls = 0; return 0;
}
int fileXioRead(int fd, void *buffer, int size)
{
    assert(fd == MOCK_FD && g_block_mode == FXIO_NOWAIT);
    g_operation = MOCK_READ; g_buffer = buffer; g_size = size; g_pending_polls = 0; return 0;
}
int fileXioWrite(int fd, const void *buffer, int size)
{
    assert(fd == MOCK_FD && g_block_mode == FXIO_NOWAIT);
    g_operation = MOCK_WRITE; g_const_buffer = buffer; g_size = size; g_pending_polls = 0; return 0;
}
int fileXioRemove(const char *name) { (void)name; return -1; }
int fileXioRename(const char *source, const char *destination)
{
    assert(source != NULL && destination != NULL && g_block_mode == FXIO_NOWAIT);
    g_operation = MOCK_RENAME; g_pending_polls = 0; return 0;
}

static int finish_write(TitanStorageIoAsyncFile *file, const char *path,
                        const void *buffer, size_t size, int flags)
{
    int result;
    do { result = TitanStorageIo_AsyncWrite(file, path, flags, buffer, size); }
    while (result == TITAN_STORAGE_IO_ASYNC_PENDING);
    return result;
}
static int finish_read(TitanStorageIoAsyncFile *file, const char *path,
                       void *buffer, size_t size)
{
    int result;
    do { result = TitanStorageIo_AsyncRead(file, path, buffer, size); }
    while (result == TITAN_STORAGE_IO_ASYNC_PENDING);
    return result;
}
static int finish_close(TitanStorageIoAsyncFile *file)
{
    int result;
    do { result = TitanStorageIo_AsyncClose(file); }
    while (result == TITAN_STORAGE_IO_ASYNC_PENDING);
    return result;
}
static int finish_replace(TitanStorageIoAsyncFile *file, const char *source, const char *target)
{
    int result;
    do { result = TitanStorageIo_AsyncAtomicReplace(file, source, target); }
    while (result == TITAN_STORAGE_IO_ASYNC_PENDING);
    return result;
}

int main(void)
{
    TitanStorageIoAsyncFile file = {0};
    const char a[] = "abc";
    const char b[] = "defg";
    char out_a[sizeof(a)] = {0};
    char out_b[sizeof(b)] = {0};
    int state;

    assert(TitanStorageIo_InitializeClient());
    state = TitanStorageIo_AsyncWrite(&file, "mass:/test.bin",
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE,
        a, sizeof(a) - 1u);
    assert(state == TITAN_STORAGE_IO_ASYNC_PENDING);
    assert(finish_write(&file, "mass:/test.bin", a, sizeof(a) - 1u,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE)
        == TITAN_STORAGE_IO_ASYNC_DONE);
    assert(finish_write(&file, "mass:/test.bin", b, sizeof(b) - 1u,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE) == TITAN_STORAGE_IO_ASYNC_DONE);
    assert(finish_close(&file) == TITAN_STORAGE_IO_ASYNC_DONE);
    state = TitanStorageIo_AsyncAtomicReplace(&file, "mass:/temp.bin", "mass:/test.bin");
    assert(state == TITAN_STORAGE_IO_ASYNC_PENDING);
    assert(TitanStorageIo_AsyncPending(&file));
    assert(finish_replace(&file, "mass:/temp.bin", "mass:/test.bin")
        == TITAN_STORAGE_IO_ASYNC_DONE);

    g_cursor = 0u;
    state = TitanStorageIo_AsyncRead(&file, "mass:/test.bin", out_a, sizeof(a) - 1u);
    assert(state == TITAN_STORAGE_IO_ASYNC_PENDING);
    assert(finish_read(&file, "mass:/test.bin", out_a, sizeof(a) - 1u)
        == TITAN_STORAGE_IO_ASYNC_DONE);
    assert(finish_read(&file, "mass:/test.bin", out_b, sizeof(b) - 1u)
        == TITAN_STORAGE_IO_ASYNC_DONE);
    assert(finish_close(&file) == TITAN_STORAGE_IO_ASYNC_DONE);
    assert(memcmp(a, out_a, sizeof(a) - 1u) == 0);
    assert(memcmp(b, out_b, sizeof(b) - 1u) == 0);
    assert(!TitanStorageIo_AsyncPending(&file));
    puts("FileXio NOWAIT async storage tests passed");
    return 0;
}

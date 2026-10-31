#include "titan_storage_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char temporary_root[] = "/tmp/project_titan_storage_alias_XXXXXX";
    char value;
    int descriptor;
    int is_directory;
    TitanStorageIoSize size;

    assert(mkdtemp(temporary_root) != 0);
    assert(chdir(temporary_root) == 0);
    assert(mkdir("mass0:", 0700) == 0);

    /* No mass: directory exists. Every operation must transparently reach
     * the first USB device exposed only as mass0:. */
    assert(TitanStorageIo_CreateDirectory("mass:/TITAN") == 0);
    assert(
        TitanStorageIo_PathInfo64(
            "mass:/TITAN",
            &is_directory,
            &size
        )
    );
    assert(is_directory != 0);

    descriptor = TitanStorageIo_Open(
        "mass:/TITAN/value.tmp",
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE |
            TITAN_STORAGE_IO_TRUNCATE
    );
    assert(descriptor >= 0);
    value = '1';
    assert(TitanStorageIo_WriteExact(descriptor, &value, 1u));
    assert(TitanStorageIo_Close(descriptor) == 0);
    assert(
        TitanStorageIo_AtomicReplace(
            "mass:/TITAN/value.tmp",
            "mass:/TITAN/value.cfg",
            "mass:/TITAN/value.bak"
        )
    );

    descriptor = TitanStorageIo_Open(
        "mass:/TITAN/value.cfg",
        TITAN_STORAGE_IO_READ
    );
    assert(descriptor >= 0);
    value = 0;
    assert(TitanStorageIo_ReadExact(descriptor, &value, 1u));
    assert(TitanStorageIo_Close(descriptor) == 0);
    assert(value == '1');
    assert(
        TitanStorageIo_PathInfo64(
            "mass:/TITAN/value.cfg",
            &is_directory,
            &size
        )
    );
    assert(!is_directory);
    assert(size == 1u);
    assert(access("mass0:/TITAN/value.cfg", F_OK) == 0);

    /* The async facade falls back synchronously on host and preserves one
     * sequential descriptor across separately submitted transfer quanta. */
    {
        TitanStorageIoAsyncFile async_file = {0};
        const char first[] = "abc";
        const char second[] = "defg";
        char read_first[sizeof(first)] = {0};
        char read_second[sizeof(second)] = {0};
        int state;
        state = TitanStorageIo_AsyncWrite(&async_file, "mass:/TITAN/async.tmp",
            TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE,
            first, sizeof(first) - 1u);
        assert(state == TITAN_STORAGE_IO_ASYNC_DONE);
        state = TitanStorageIo_AsyncWrite(&async_file, "mass:/TITAN/async.tmp",
            TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE,
            second, sizeof(second) - 1u);
        assert(state == TITAN_STORAGE_IO_ASYNC_DONE);
        assert(TitanStorageIo_AsyncClose(&async_file) == TITAN_STORAGE_IO_ASYNC_DONE);
        state = TitanStorageIo_AsyncRead(&async_file, "mass:/TITAN/async.tmp",
            read_first, sizeof(first) - 1u);
        assert(state == TITAN_STORAGE_IO_ASYNC_DONE);
        state = TitanStorageIo_AsyncRead(&async_file, "mass:/TITAN/async.tmp",
            read_second, sizeof(second) - 1u);
        assert(state == TITAN_STORAGE_IO_ASYNC_DONE);
        assert(TitanStorageIo_AsyncClose(&async_file) == TITAN_STORAGE_IO_ASYNC_DONE);
        assert(memcmp(first, read_first, sizeof(first) - 1u) == 0);
        assert(memcmp(second, read_second, sizeof(second) - 1u) == 0);
        assert(TitanStorageIo_Remove("mass:/TITAN/async.tmp") == 0);
    }

    assert(TitanStorageIo_Remove("mass:/TITAN/value.cfg") == 0);

    puts("storage mass:/mass0:/ fallback tests passed");
    return 0;
}

// libc/newlib
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
_off64_t lseek64(int __filedes, _off64_t __offset, int __whence); // should be in unistd.h

// PS2SDK
#include <delaythread.h> // DelayThread
#include <ps2sdkapi.h>   // ps2sdk_get_iop_fd

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

// IOP settings structs
#include "../../../common/include/fhi_bd_config.h"    // fhi_bd, fhi_bd_file — also pulls usbhdfsd-common.h
#include "../../../common/include/fhi_fileid_config.h"

#include "fhi_config.h"
#include "modlist.h"

// ---------------------------------------------------------------------------
// Settings pointers — exactly one is non-NULL after fhi_config_init
// ---------------------------------------------------------------------------
static struct fhi_bd     *g_bd   = NULL;
static struct fhi_fileid *g_fileid = NULL;

// ---------------------------------------------------------------------------
// Vtable
// ---------------------------------------------------------------------------
struct fhi_backend_ops {
    int (*add_file_fd)(int fhi_fid, int fd, const char *path);
    int keep_open;  // 1 = leave fd open for IOP post-reboot
};

// Forward declarations of backend implementations
static int backend_bd_add_file_fd(int fhi_fid, int fd, const char *path);
static int backend_bd_add_file_fd_ex(
    int fhi_fid,
    int fd,
    const char *path,
    int append
);
static int backend_fileid_add_file_fd(int fhi_fid, int fd, const char *path);

static const struct fhi_backend_ops ops_bd   = { backend_bd_add_file_fd,   0 };
static const struct fhi_backend_ops ops_fileid = { backend_fileid_add_file_fd, 1 };

static const struct fhi_backend_ops *g_ops = NULL;

// ---------------------------------------------------------------------------
// fhi_config_init — filename-based backend detection
// ---------------------------------------------------------------------------
int fhi_config_init(struct SModList *ml)
{
    static const struct {
        const char                     *filename;
        const struct fhi_backend_ops   *ops;
        unsigned int                    settings_size;
        void                          **settings_out;
    } candidates[] = {
        { "fhi_bd.irx",    &ops_bd,     sizeof(struct fhi_bd),             (void **)&g_bd   },
        { "mmcefhi.irx",   &ops_fileid, sizeof(struct fhi_fileid),         (void **)&g_fileid },
        { "udpfs_fhi.irx", &ops_fileid, sizeof(struct fhi_fileid),         (void **)&g_fileid },
    };
    unsigned int i;

    g_bd = NULL;
    g_fileid = NULL;
    g_ops = NULL;

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        void *s = module_get_settings(modlist_get_by_name(ml, candidates[i].filename));
        if (!s)
            continue;
        memset(s, 0, candidates[i].settings_size);
        *candidates[i].settings_out = s;
        g_ops = candidates[i].ops;
        return 0;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Bounded open retry. USB mount transitions are asynchronous, but an empty
// spin loop can starve the rest of the loader and still provides no real time
// for the device to settle.
// ---------------------------------------------------------------------------
static int fhi_open_with_retry(const char *path, int flags)
{
    unsigned int attempt;

    for (attempt = 0u; attempt < 50u; attempt++) {
        int fd = open(path, flags);
        if (fd >= 0)
            return fd;
        DelayThread(10 * 1000);
    }

    return -1;
}

static int fhi_bd_find_fragment_end(
    int replacing_fid,
    unsigned int *fragment_end
)
{
    unsigned int end = 0u;
    int index;

    if (g_bd == NULL || fragment_end == NULL)
        return -1;

    for (index = 0; index < FHI_MAX_FILES; index++) {
        const struct fhi_bd_file *current = &g_bd->file[index];
        unsigned int current_end;

        if (index == replacing_fid || current->frag_count == 0u)
            continue;

        current_end = (unsigned int)current->frag_start +
            (unsigned int)current->frag_count;
        if (current_end < (unsigned int)current->frag_start ||
            current_end > BDM_MAX_FRAGS) {
            printf("Existing FHI fragment map is invalid for file %d\n", index);
            return -1;
        }

        if (current_end > end)
            end = current_end;
    }

    *fragment_end = end;
    return 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
int fhi_add_file_fd(int fhi_fid, int fd, const char *path)
{
    int rv;

    if (g_ops == NULL) {
        close(fd);
        return -1;
    }

    rv = g_ops->add_file_fd(fhi_fid, fd, path);
    if (!g_ops->keep_open)
        close(fd);
    return rv;
}

int fhi_add_file(int fhi_fid, const char *path, int flags)
{
    int fd, rv;

    if (g_ops == NULL)
        return -1;

    printf("Loading %s...\n", path);
    fd = fhi_open_with_retry(path, flags);
    if (fd < 0) {
        printf("Unable to open %s\n", path);
        return -1;
    }

    rv = g_ops->add_file_fd(fhi_fid, fd, path);
    if (!g_ops->keep_open)
        close(fd);
    return rv;
}

int fhi_supports_file_chain(void)
{
    return g_bd != NULL;
}

int fhi_add_file_part(
    int fhi_fid,
    const char *path,
    int flags,
    int first_part
)
{
    int fd;
    int rv;

    if (g_bd == NULL) {
        printf("The active FHI backend cannot concatenate USBExtreme parts\n");
        return -1;
    }

    printf("Loading %s...\n", path);
    fd = fhi_open_with_retry(path, flags);
    if (fd < 0) {
        printf("Unable to open %s\n", path);
        return -1;
    }

    rv = backend_bd_add_file_fd_ex(
        fhi_fid,
        fd,
        path,
        first_part ? 0 : 1
    );
    close(fd);
    return rv;
}

// ---------------------------------------------------------------------------
// Backend: BD (block device with defragmentation)
// ---------------------------------------------------------------------------
static int backend_bd_add_file_fd(int fhi_fid, int fd, const char *path)
{
    return backend_bd_add_file_fd_ex(fhi_fid, fd, path, 0);
}

static int backend_bd_add_file_fd_ex(
    int fhi_fid,
    int fd,
    const char *path,
    int append
)
{
    static bd_fragment_t staged_fragments[BDM_MAX_FRAGS];
    int i;
    int iop_fd;
    int fragment_count;
    int device_number;
    int driver_result;
    uint32_t driver_name;
    _off64_t size;
    unsigned int fragment_write_start;
    unsigned int staged_fragment_end;
    uint64_t new_size;
    struct fhi_bd_file previous_file;
    struct fhi_bd_file staged_file;
    struct fhi_bd_file *file;

    (void)path;

    if (g_bd == NULL || fhi_fid < 0 || fhi_fid >= FHI_MAX_FILES)
        return -1;

    iop_fd = ps2sdk_get_iop_fd(fd);
    if (iop_fd < 0) {
        printf("Unable to obtain IOP file descriptor\n");
        return -1;
    }

    size = lseek64(fd, 0, SEEK_END);
    if (size <= 0) {
        printf("Invalid backing file size\n");
        return -1;
    }

    driver_result = fileXioIoctl2(
        iop_fd,
        USBMASS_IOCTL_GET_DRIVERNAME,
        NULL,
        0,
        NULL,
        0
    );
    if (driver_result < 0) {
        printf("Unable to identify backing driver\n");
        return -1;
    }
    driver_name = (uint32_t)driver_result;

    device_number = -1;
    if (fileXioIoctl2(
            iop_fd,
            USBMASS_IOCTL_GET_DEVICE_NUMBER,
            NULL,
            0,
            &device_number,
            sizeof(device_number)) < 0 || device_number < 0) {
        printf("Unable to identify backing device\n");
        return -1;
    }

    file = &g_bd->file[fhi_fid];
    previous_file = *file;
    staged_file = previous_file;

    if (append) {
        if (previous_file.frag_count == 0u) {
            printf("Cannot append a fragment list before its first part\n");
            return -1;
        }
        if (driver_name != g_bd->drvName ||
            (uint32_t)device_number != g_bd->devNr) {
            printf("USBExtreme parts are not on the same backing device\n");
            return -1;
        }

        fragment_write_start = (unsigned int)previous_file.frag_start +
            (unsigned int)previous_file.frag_count;
        if (fragment_write_start < (unsigned int)previous_file.frag_start) {
            printf("FHI fragment index overflow\n");
            return -1;
        }
    } else {
        if (fhi_bd_find_fragment_end(fhi_fid, &fragment_write_start) < 0)
            return -1;
        staged_file.frag_start = (uint8_t)fragment_write_start;
        staged_file.frag_count = 0u;
        staged_file.size = 0u;
    }

    if (fragment_write_start >= BDM_MAX_FRAGS) {
        printf("Too many fragments (%u)\n", fragment_write_start);
        return -1;
    }

    memset(staged_fragments, 0, sizeof(staged_fragments));
    fragment_count = fileXioIoctl2(
        iop_fd,
        USBMASS_IOCTL_GET_FRAGLIST,
        NULL,
        0,
        staged_fragments,
        sizeof(bd_fragment_t) * (BDM_MAX_FRAGS - fragment_write_start)
    );
    if (fragment_count <= 0) {
        printf("Unable to map backing file fragments (%d)\n", fragment_count);
        return -1;
    }

    staged_fragment_end = fragment_write_start + (unsigned int)fragment_count;
    if (staged_fragment_end < fragment_write_start ||
        staged_fragment_end > BDM_MAX_FRAGS ||
        (unsigned int)staged_file.frag_count +
            (unsigned int)fragment_count > UINT8_MAX) {
        printf("Invalid backing file fragment count (%d)\n", fragment_count);
        return -1;
    }

    for (i = 0; i < fragment_count; i++) {
        if (staged_fragments[i].count == 0u) {
            printf("Backing file contains an empty fragment\n");
            return -1;
        }
    }

    if ((uint64_t)size > UINT64_MAX - staged_file.size) {
        printf("Backing file size overflow\n");
        return -1;
    }
    new_size = staged_file.size + (uint64_t)size;

    /* Commit only after all I/O and identity checks have succeeded. */
    memcpy(
        &g_bd->frags[fragment_write_start],
        staged_fragments,
        sizeof(bd_fragment_t) * (unsigned int)fragment_count
    );
    staged_file.frag_count = (uint8_t)(
        (unsigned int)staged_file.frag_count +
        (unsigned int)fragment_count
    );
    staged_file.size = new_size;
    *file = staged_file;

    if (!append) {
        g_bd->drvName = driver_name;
        g_bd->devNr = (uint32_t)device_number;
    }

    printf(
        "file[%d] part fragments: start=%u, added=%d, total=%u\n",
        fhi_fid,
        fragment_write_start,
        fragment_count,
        file->frag_count
    );
    for (i = 0; i < fragment_count; i++)
        printf("- frag[%u] start=%u, count=%u\n",
               fragment_write_start + (unsigned int)i,
               (unsigned int)staged_fragments[i].sector,
               staged_fragments[i].count);

    printf("Using BDM device: %s%d\n", (char *)&g_bd->drvName, (int)g_bd->devNr);

    return 0;
}

// ---------------------------------------------------------------------------
// Backend: FILEID (pre-opened file handles)
// Shared by both mmcefhi.irx and udpfs_fhi.irx
// ---------------------------------------------------------------------------
static int backend_fileid_add_file_fd(int fhi_fid, int fd, const char *path)
{
    int iop_fd;
    int file_id;
    int device_number;
    _off64_t size;

    if (g_fileid == NULL || fhi_fid < 0 || fhi_fid >= FHI_MAX_FILES)
        return -1;

    iop_fd = ps2sdk_get_iop_fd(fd);
    if (iop_fd < 0)
        return -1;

    device_number = 0;
    if (path != NULL) {
        const char *separator = strchr(path, ':');
        const char *cursor;

        if (separator == NULL)
            return -1;
        for (cursor = path; cursor < separator; cursor++) {
            if (*cursor >= '0' && *cursor <= '9')
                device_number = *cursor - '0';
        }
    }

    file_id = fileXioIoctl2(iop_fd, 0x80, NULL, 0, NULL, 0);
    size = lseek64(fd, 0, SEEK_END);
    if (file_id < 0 || size <= 0)
        return -1;

    /* Publish only after every query has completed successfully. */
    g_fileid->devNr = device_number;
    g_fileid->file[fhi_fid].id = file_id;
    g_fileid->file[fhi_fid].size = (uint64_t)size;
    return 0;
}

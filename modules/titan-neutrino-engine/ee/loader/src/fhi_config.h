#ifndef FHI_CONFIG_H
#define FHI_CONFIG_H

#include "../../../common/include/fhi.h"  // FHI_FID_*, FHI_MAX_FILES

struct SModList;

// Scan mods for an active FHI backend, zero its settings struct.
// Returns 0 if found, -1 if none.
int fhi_config_init(struct SModList *ml);

// Add a file to the active backend using an already-open fd.
// 'path' provides context (mmce/udpfs backend: extracts devNr from it; others: ignored).
// fd lifetime is transferred: non-keep-open backends (bd) get it closed here;
// keep-open backends (mmce, udpfs) leave it open for IOP post-reboot.
// Returns 0 on success, -1 on error.
int fhi_add_file_fd(int fhi_fid, int fd, const char *path);

// Open path with retry (up to 1000 x nopdelay), then call fhi_add_file_fd.
// Caller is responsible for passing appropriate flags for the backend.
// Returns 0 on success, -1 on error.
int fhi_add_file(int fhi_fid, const char *path, int flags);

// Returns nonzero only for block-device backends, where several physical
// files can be represented as one logical FHI file by appending fragment maps.
int fhi_supports_file_chain(void);

// Register one part of a chained logical file. first_part must be nonzero for
// the first slice and zero for every following slice.
int fhi_add_file_part(
    int fhi_fid,
    const char *path,
    int flags,
    int first_part
);

#endif

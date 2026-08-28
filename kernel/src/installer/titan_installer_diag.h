#ifndef PROJECT_TITAN_INSTALLER_DIAG_H
#define PROJECT_TITAN_INSTALLER_DIAG_H

#include <stdint.h>

/*
 * Titan Installer diagnostics intentionally buffer events in EE RAM while an
 * installation is active.  Persisting every event to the same USB device
 * would perturb the timing being measured and could itself create wave
 * stutter.  The buffered log is flushed on success/failure and can fall back
 * to the selected Memory Card if USB persistence fails.
 */

void TitanInstallerDiag_Start(const char *installer_version, const char *executable_path);
void TitanInstallerDiag_BindStorage(const char *usb_prefix, int memory_card);
void TitanInstallerDiag_SetMemoryCard(int memory_card);

void TitanInstallerDiag_Logf(
    const char *level,
    const char *event,
    const char *format,
    ...
);

void TitanInstallerDiag_SetContext(
    unsigned int entry_index,
    unsigned int entry_count,
    const char *phase,
    const char *path,
    unsigned int progress
);

void TitanInstallerDiag_BeginFrame(unsigned int frame_number);

void TitanInstallerDiag_RecordIo(
    const char *operation,
    const char *path,
    unsigned int requested_bytes,
    int result,
    int error_number,
    uint64_t elapsed_ms
);

void TitanInstallerDiag_RecordCpuWork(
    const char *operation,
    unsigned int processed_bytes,
    uint64_t elapsed_ms
);

void TitanInstallerDiag_EndFrame(
    uint64_t frame_elapsed_ms,
    uint64_t input_elapsed_ms,
    uint64_t install_elapsed_ms,
    uint64_t render_elapsed_ms,
    unsigned int target_frame_ms,
    unsigned int progress,
    const char *phase,
    const char *path
);

/* Writes the complete RAM trace. Returns non-zero when either USB or the
 * Memory Card fallback accepted the log. */
int TitanInstallerDiag_FlushFinal(const char *result_label);

/* A small pre-install checkpoint is safe because it happens before the timed
 * transaction begins. It is never called from the per-frame install pump. */
int TitanInstallerDiag_FlushCheckpoint(const char *label);

#endif

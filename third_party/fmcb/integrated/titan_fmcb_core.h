#ifndef TITAN_FMCB_CORE_H
#define TITAN_FMCB_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TitanFmcbProgressCallback)(void *opaque, const char *stage, float progress, int detail);

/* Runs the upstream FMCB installation engine inside the caller's EE process.
 * There is deliberately no LoadExecPS2/FMCBInstaller.elf handoff here. */
int TitanFmcbCore_Install(const char *fmcb_root, int mc_port,
                         TitanFmcbProgressCallback progress, void *opaque);
int TitanFmcbCore_Cleanup(const char *fmcb_root, int mc_port,
                         TitanFmcbProgressCallback progress, void *opaque);

#ifdef __cplusplus
}
#endif
#endif

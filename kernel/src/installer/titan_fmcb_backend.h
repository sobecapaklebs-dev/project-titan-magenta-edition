#ifndef TITAN_FMCB_BACKEND_H
#define TITAN_FMCB_BACKEND_H

typedef void (*TitanFmcbBackendProgress)(void *opaque, const char *stage, float progress, int detail);

int TitanFmcbBackend_IsIntegrated(void);
int TitanFmcbBackend_Install(const char *fmcb_root, int mc_port,
                             TitanFmcbBackendProgress progress, void *opaque);
int TitanFmcbBackend_Cleanup(const char *fmcb_root, int mc_port,
                             TitanFmcbBackendProgress progress, void *opaque);

#endif

#include "titan_fmcb_backend.h"
#include "titan_fmcb_core.h"

int TitanFmcbBackend_IsIntegrated(void)
{
    return 1;
}

int TitanFmcbBackend_Install(const char *fmcb_root, int mc_port,
                             TitanFmcbBackendProgress progress, void *opaque)
{
    return TitanFmcbCore_Install(fmcb_root, mc_port,
                                 (TitanFmcbProgressCallback)progress, opaque);
}

int TitanFmcbBackend_Cleanup(const char *fmcb_root, int mc_port,
                             TitanFmcbBackendProgress progress, void *opaque)
{
    return TitanFmcbCore_Cleanup(fmcb_root, mc_port,
                                 (TitanFmcbProgressCallback)progress, opaque);
}

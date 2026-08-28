#include "titan_storage_pfs_iop.h"

int TitanStoragePfsIop_Ensure(
    const TitanStoragePfsIopBackend *backend,
    void *context,
    const TitanStoragePfsIopImage images[4]
)
{
    uint32_t i;
    if (backend == 0 || images == 0 || backend->module_resident == 0 ||
        backend->exec_module == 0 || backend->devices_ready == 0) return 0;
    if (backend->devices_ready(context)) return 1;
    for (i = 0u; i < 4u; ++i)
    {
        if (images[i].module_name == 0 || images[i].module_name[0] == '\0') return 0;
        if (backend->module_resident(context, images[i].module_name)) continue;
        if (images[i].data == 0 || images[i].size == 0u) return 0;
        if (!backend->exec_module(context, images[i].data, images[i].size)) return 0;
    }
    return backend->devices_ready(context) != 0;
}

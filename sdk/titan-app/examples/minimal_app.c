#include "titan_app_sdk.h"
#include <stddef.h>

static const TitanAppApiV1 *g_api;

int titan_app_init(const TitanAppApiV1 *api, const char *app_root)
{
    (void)app_root;
    if (api == 0 || api->abi_version != TITAN_APP_API_VERSION ||
        api->struct_size < offsetof(TitanAppApiV1, notify) + sizeof(api->notify))
        return -1;
    g_api = api;
    return 0;
}

void titan_app_update(const TitanAppInput *input, uint32_t delta_ms)
{
    (void)delta_ms;
    if (input != 0 && (input->pressed & TITAN_APP_BUTTON_CIRCLE) != 0u)
        g_api->request_exit();
}

void titan_app_draw(void)
{
    g_api->draw_text_shadow(80.0f, 80.0f, 0.8f, "Hello Titan", 255,255,255,255);
}

void titan_app_shutdown(void) { g_api = 0; }

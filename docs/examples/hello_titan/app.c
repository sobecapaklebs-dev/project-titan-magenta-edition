#include "titan_app_sdk.h"

static const TitanAppApiV1 *g_titan;
static unsigned int g_press_count;

int titan_app_init(const TitanAppApiV1 *api, const char *app_root)
{
    (void)app_root;
    if (
        api == 0 ||
        api->abi_version != TITAN_APP_API_VERSION ||
        api->struct_size < sizeof(TitanAppApiV1)
    )
    {
        return -1;
    }

    g_titan = api;
    g_press_count = 0u;
    g_titan->notify(
        TITAN_APP_NOTIFICATION_SUCCESS,
        "Hello Titan",
        "Titan App API v1 carregada"
    );
    return 0;
}

void titan_app_update(const TitanAppInput *input, uint32_t delta_ms)
{
    (void)delta_ms;
    if (input == 0)
        return;

    if ((input->pressed & TITAN_APP_BUTTON_CROSS) != 0u)
        g_press_count++;
    if ((input->pressed & TITAN_APP_BUTTON_CIRCLE) != 0u)
        g_titan->request_exit();
}

void titan_app_draw(void)
{
    char counter[32];
    unsigned int value;
    unsigned int digit_count;
    unsigned int index;

    g_titan->draw_panel(118.0f, 86.0f, 618.0f, 276.0f, 235u);
    g_titan->draw_text_shadow(
        156.0f, 126.0f, 0.95f,
        "Hello Titan",
        246u, 248u, 252u, 255u
    );
    g_titan->draw_text(
        156.0f, 174.0f, 0.58f,
        "Este aplicativo esta rodando dentro do Project Titan.",
        210u, 216u, 230u, 255u
    );
    g_titan->draw_text(
        156.0f, 207.0f, 0.58f,
        "X incrementa o contador. Circulo fecha o aplicativo.",
        210u, 216u, 230u, 255u
    );

    counter[0] = 'X';
    counter[1] = ':';
    counter[2] = ' ';
    value = g_press_count;
    digit_count = 1u;
    while (value >= 10u && digit_count < 9u)
    {
        value /= 10u;
        digit_count++;
    }
    value = g_press_count;
    for (index = 0u; index < digit_count; index++)
    {
        counter[2u + digit_count - index] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    counter[3u + digit_count] = '\0';

    g_titan->draw_text_shadow(
        156.0f, 266.0f, 0.74f,
        counter,
        222u, 43u, 82u, 255u
    );
    g_titan->draw_progress(
        156.0f, 311.0f, 420.0f, 7.0f,
        g_press_count % 101u,
        255u
    );
}

void titan_app_shutdown(void)
{
    g_titan = 0;
}

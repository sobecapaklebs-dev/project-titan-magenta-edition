#include "titan_app_sdk.h"
#include "titan_game_sdk.h"

#include <stddef.h>
#include <stdint.h>

#define REQUIRED_APP_SIZE \
    (offsetof(TitanAppApiV1, game_end) + sizeof(((TitanAppApiV1 *)0)->game_end))
#define REQUIRED_GAME_SIZE \
    (offsetof(TitanGameApiV1, video_draw_clip_triangles) + \
     sizeof(((TitanGameApiV1 *)0)->video_draw_clip_triangles))

static const TitanAppApiV1 *s_app;
static const TitanGameApiV1 *s_game;
static int s_session_active;
static int s_finished;

static void end_session(void)
{
    if (s_session_active && s_app != 0 && s_app->game_end != 0)
        s_app->game_end();
    s_session_active = 0;
    s_game = 0;
}

int titan_app_init(const TitanAppApiV1 *api, const char *app_root)
{
    if (api == 0 || app_root == 0 || app_root[0] == '\0' ||
        api->abi_version != TITAN_APP_API_VERSION ||
        api->struct_size < REQUIRED_APP_SIZE ||
        api->request_exit == 0 || api->game_begin == 0 || api->game_end == 0)
        return -1;
    s_app = api;
    return 0;
}

void titan_app_update(const TitanAppInput *input, uint32_t delta_ms)
{
    TitanGameSessionConfig config;
    TitanGamePadState pad;
    TitanGameClipVertex triangle[3];
    uint32_t i;

    (void)input;
    (void)delta_ms;
    if (s_finished) return;

    if (!s_session_active) {
        for (i = 0; i < sizeof(config) / sizeof(uint32_t); i++)
            ((uint32_t *)&config)[i] = 0u;
        config.struct_size = sizeof(config);
        config.width = TITAN_GAME_DEFAULT_WIDTH;
        config.height = TITAN_GAME_DEFAULT_HEIGHT;
        config.heap_limit_bytes = 0u;
        if (!s_app->game_begin(&config, &s_game) || s_game == 0) {
            s_finished = 1;
            s_app->request_exit();
            return;
        }
        s_session_active = 1;
        if (s_game->abi_version != TITAN_GAME_API_VERSION ||
            s_game->struct_size < REQUIRED_GAME_SIZE ||
            (s_game->feature_flags & TITAN_GAME_FEATURE_CLIPSPACE_TRIANGLES) == 0u) {
            end_session();
            s_finished = 1;
            s_app->request_exit();
            return;
        }
    }

    s_game->poll_input();
    if (s_game->get_pad(0u, &pad) &&
        (pad.pressed & TITAN_GAME_BUTTON_CIRCLE) != 0u) {
        end_session();
        s_finished = 1;
        s_app->request_exit();
        return;
    }

    triangle[0] = (TitanGameClipVertex){-0.65f, -0.55f, 0.5f, 1.0f, 0.0f, 0.0f, 0xff3050ffu};
    triangle[1] = (TitanGameClipVertex){ 0.65f, -0.55f, 0.5f, 1.0f, 0.0f, 0.0f, 0xff50d070u};
    triangle[2] = (TitanGameClipVertex){ 0.00f,  0.65f, 0.5f, 1.0f, 0.0f, 0.0f, 0xffffc040u};

    if (s_game->video_begin_frame(8u, 10u, 18u)) {
        (void)s_game->video_draw_clip_triangles(triangle, 3u, 0u);
        (void)s_game->video_end_frame();
    }
}

void titan_app_draw(void)
{
    /* TGR owns presentation while the session is active. */
}

void titan_app_shutdown(void)
{
    end_session();
    s_finished = 1;
}

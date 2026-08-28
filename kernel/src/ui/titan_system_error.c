#include "titan_system_error.h"

#include "titan_audio.h"
#include "titan_font.h"
#include "titan_ui_assets.h"
#include "titan_video.h"

#include <string.h>

#define TITAN_SYSTEM_ERROR_QUEUE_CAPACITY 3u
#define TITAN_SYSTEM_ERROR_COMPONENT_CAPACITY 48u
#define TITAN_SYSTEM_ERROR_MESSAGE_CAPACITY 160u

typedef struct TitanSystemErrorEntry
{
    char component[TITAN_SYSTEM_ERROR_COMPONENT_CAPACITY];
    char message[TITAN_SYSTEM_ERROR_MESSAGE_CAPACITY];
    unsigned int repeat_count;
} TitanSystemErrorEntry;

typedef struct TitanSystemErrorState
{
    int initialized;
    int open;
    TitanSystemErrorEntry current;
    TitanSystemErrorEntry queue[TITAN_SYSTEM_ERROR_QUEUE_CAPACITY];
    unsigned int queue_count;
    unsigned int dropped_count;
} TitanSystemErrorState;

static TitanSystemErrorState g_system_error;

static void TitanSystemError_Copy(
    char *destination,
    unsigned int capacity,
    const char *source
)
{
    unsigned int index = 0u;
    if (destination == 0 || capacity == 0u) return;
    while (source != 0 && source[index] != '\0' && index + 1u < capacity)
    {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int TitanSystemError_Matches(
    const TitanSystemErrorEntry *entry,
    const char *component,
    const char *message
)
{
    return entry != 0 && component != 0 && message != 0 &&
        strcmp(entry->component, component) == 0 &&
        strcmp(entry->message, message) == 0;
}

static void TitanSystemError_StartNext(void)
{
    unsigned int index;
    if (g_system_error.open || g_system_error.queue_count == 0u) return;
    g_system_error.current = g_system_error.queue[0];
    for (index = 1u; index < g_system_error.queue_count; index++)
        g_system_error.queue[index - 1u] = g_system_error.queue[index];
    g_system_error.queue_count--;
    g_system_error.open = 1;
    (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_NOTIFICATION);
}

void TitanSystemError_Initialize(void)
{
    memset(&g_system_error, 0, sizeof(g_system_error));
    g_system_error.initialized = 1;
}

int TitanSystemError_Raise(const char *component, const char *message)
{
    TitanSystemErrorEntry *entry;
    unsigned int index;

    if (!g_system_error.initialized) TitanSystemError_Initialize();
    if (component == 0 || message == 0 || component[0] == '\0' || message[0] == '\0')
        return 0;

    if (g_system_error.open &&
        TitanSystemError_Matches(&g_system_error.current, component, message))
    {
        g_system_error.current.repeat_count++;
        return 1;
    }

    for (index = 0u; index < g_system_error.queue_count; index++)
    {
        if (TitanSystemError_Matches(&g_system_error.queue[index], component, message))
        {
            g_system_error.queue[index].repeat_count++;
            return 1;
        }
    }

    if (!g_system_error.open && g_system_error.queue_count == 0u)
    {
        entry = &g_system_error.current;
        memset(entry, 0, sizeof(*entry));
        TitanSystemError_Copy(entry->component, sizeof(entry->component), component);
        TitanSystemError_Copy(entry->message, sizeof(entry->message), message);
        entry->repeat_count = 1u;
        g_system_error.open = 1;
        (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_NOTIFICATION);
        return 1;
    }

    if (g_system_error.queue_count >= TITAN_SYSTEM_ERROR_QUEUE_CAPACITY)
    {
        g_system_error.dropped_count++;
        return 0;
    }

    entry = &g_system_error.queue[g_system_error.queue_count++];
    memset(entry, 0, sizeof(*entry));
    TitanSystemError_Copy(entry->component, sizeof(entry->component), component);
    TitanSystemError_Copy(entry->message, sizeof(entry->message), message);
    entry->repeat_count = 1u;
    return 1;
}

int TitanSystemError_Update(const TitanInputState *input)
{
    int was_open;
    if (!g_system_error.initialized) return 0;
    was_open = g_system_error.open;
    if (g_system_error.open && input != 0 && input->connected &&
        (input->pressed & (TITAN_INPUT_BUTTON_CROSS | TITAN_INPUT_BUTTON_CIRCLE)) != 0u)
    {
        memset(&g_system_error.current, 0, sizeof(g_system_error.current));
        g_system_error.open = 0;
        TitanSystemError_StartNext();
    }
    return was_open;
}

void TitanSystemError_Draw(unsigned int frame)
{
    const float panel_x = 134.0f;
    const float panel_y = 126.0f;
    const float panel_w = 586.0f;
    const float panel_h = 196.0f;
    float component_width;
    (void)frame;

    if (!g_system_error.open) return;

    TitanVideo_ResetTransform();
    TitanVideo_DrawRectAlpha(0.0f, 0.0f, 854.0f, 448.0f, 0u, 0u, 0u, 116u);
    TitanVideo_DrawRoundedRectAlpha(
        panel_x + 6.0f, panel_y + 7.0f, panel_w, panel_h, 10.0f,
        0u, 0u, 0u, 92u
    );
    TitanVideo_DrawRoundedRectAlpha(
        panel_x, panel_y, panel_w, panel_h, 10.0f,
        18u, 20u, 27u, 252u
    );
    TitanVideo_DrawRectAlpha(panel_x, panel_y, 5.0f, panel_h, 223u, 46u, 58u, 255u);
    TitanUiAssets_DrawAlert(
        panel_x + 50.0f, panel_y + 55.0f,
        46.0f, 46.0f,
        223u, 46u, 58u, 255u
    );
    TitanFont_DrawTextShadow(
        panel_x + 88.0f, panel_y + 28.0f, 0.72f,
        "Erro do sistema", 248u, 249u, 252u, 255u
    );
    component_width = TitanFont_MeasureText(0.48f, g_system_error.current.component);
    (void)component_width;
    TitanFont_DrawTextAlpha(
        panel_x + 88.0f, panel_y + 61.0f, 0.48f,
        g_system_error.current.component, 223u, 46u, 58u, 255u
    );
    TitanFont_DrawTextAlpha(
        panel_x + 34.0f, panel_y + 105.0f, 0.48f,
        g_system_error.current.message, 208u, 213u, 224u, 255u
    );
    if (g_system_error.current.repeat_count > 1u)
    {
        TitanFont_DrawTextAlpha(
            panel_x + 34.0f, panel_y + 133.0f, 0.40f,
            "O mesmo erro ocorreu novamente.", 150u, 158u, 174u, 255u
        );
    }
    TitanFont_DrawTextAlpha(
        panel_x + panel_w - 118.0f, panel_y + panel_h - 31.0f,
        0.46f, "X  OK", 245u, 246u, 250u, 255u
    );
}

int TitanSystemError_IsOpen(void)
{
    return g_system_error.open;
}

unsigned int TitanSystemError_GetDroppedCount(void)
{
    return g_system_error.dropped_count;
}

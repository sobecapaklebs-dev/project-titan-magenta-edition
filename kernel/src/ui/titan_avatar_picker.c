#include "titan_avatar_picker.h"

#include "titan_audio.h"

#include "titan_avatar_ids.h"
#include "titan_animation.h"
#include "titan_avatars.h"
#include "titan_font.h"
#include "titan_video.h"
#include "titan_time.h"

#include <string.h>

#define TITAN_AVATAR_PICKER_TITLE_CAPACITY 64
#define TITAN_AVATAR_PICKER_NAME_CAPACITY 64
#define TITAN_AVATAR_PICKER_COLUMNS 8u
#define TITAN_AVATAR_PICKER_ROWS 4u
#define TITAN_AVATAR_PICKER_PAGE_SIZE \
    (TITAN_AVATAR_PICKER_COLUMNS * TITAN_AVATAR_PICKER_ROWS)
#define TITAN_AVATAR_PICKER_TILE_SIZE 56.0f
#define TITAN_AVATAR_PICKER_TILE_GAP 10.0f
#define TITAN_AVATAR_PICKER_GRID_X 168.0f
#define TITAN_AVATAR_PICKER_GRID_Y 90.0f
#define TITAN_AVATAR_PICKER_OPEN_MS 134u
#define TITAN_AVATAR_PICKER_CLOSE_MS 134u

typedef enum TitanAvatarPickerState
{
    TITAN_AVATAR_PICKER_CLOSED = 0,
    TITAN_AVATAR_PICKER_OPENING,
    TITAN_AVATAR_PICKER_ACTIVE,
    TITAN_AVATAR_PICKER_CLOSING
} TitanAvatarPickerState;

typedef struct TitanAvatarPickerContext
{
    int initialized;
    TitanAvatarPickerState state;
    unsigned int selected_avatar;
    unsigned long long transition_started_ms;
    char title[TITAN_AVATAR_PICKER_TITLE_CAPACITY];
    char profile_name[TITAN_AVATAR_PICKER_NAME_CAPACITY];
    TitanAvatarPickerConfirmCallback confirm_callback;
    TitanAvatarPickerCancelCallback cancel_callback;
    void *user_data;
} TitanAvatarPickerContext;

static TitanAvatarPickerContext g_avatar_picker;

static void TitanAvatarPicker_CopyString(
    char *destination,
    unsigned int capacity,
    const char *source
)
{
    unsigned int index;

    if (destination == 0 || capacity == 0u)
    {
        return;
    }

    index = 0u;

    while (
        source != 0 &&
        source[index] != '\0' &&
        index + 1u < capacity
    )
    {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

static unsigned char TitanAvatarPicker_ModulateAlpha(
    unsigned char alpha,
    float visibility
)
{
    unsigned int value;

    if (visibility < 0.0f)
    {
        visibility = 0.0f;
    }
    else if (visibility > 1.0f)
    {
        visibility = 1.0f;
    }

    value = (unsigned int)((float)alpha * visibility + 0.5f);

    if (value > 255u)
    {
        value = 255u;
    }

    return (unsigned char)value;
}

static void TitanAvatarPicker_DrawOutline(
    float x,
    float y,
    float width,
    float height,
    float thickness,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    TitanVideo_DrawRectAlpha(x, y, width, thickness, red, green, blue, alpha);
    TitanVideo_DrawRectAlpha(
        x,
        y + height - thickness,
        width,
        thickness,
        red,
        green,
        blue,
        alpha
    );
    TitanVideo_DrawRectAlpha(x, y, thickness, height, red, green, blue, alpha);
    TitanVideo_DrawRectAlpha(
        x + width - thickness,
        y,
        thickness,
        height,
        red,
        green,
        blue,
        alpha
    );
}

static void TitanAvatarPicker_FormatPage(
    char output[16],
    unsigned int page,
    unsigned int page_count
)
{
    unsigned int offset;

    offset = 0u;
    output[offset++] = (char)('0' + ((page + 1u) / 10u));

    if (output[0] == '0')
    {
        offset = 0u;
    }

    output[offset++] = (char)('0' + ((page + 1u) % 10u));
    output[offset++] = ' ';
    output[offset++] = '/';
    output[offset++] = ' ';
    output[offset++] = (char)('0' + (page_count / 10u));

    if (output[offset - 1u] == '0')
    {
        offset--;
    }

    output[offset++] = (char)('0' + (page_count % 10u));
    output[offset] = '\0';
}

static void TitanAvatarPicker_SelectPage(
    int direction
)
{
    unsigned int page_count;
    unsigned int page;
    unsigned int local_index;
    unsigned int selected;

    page_count =
        (TITAN_AVATAR_COUNT + TITAN_AVATAR_PICKER_PAGE_SIZE - 1u) /
        TITAN_AVATAR_PICKER_PAGE_SIZE;
    page = g_avatar_picker.selected_avatar / TITAN_AVATAR_PICKER_PAGE_SIZE;
    local_index = g_avatar_picker.selected_avatar % TITAN_AVATAR_PICKER_PAGE_SIZE;

    if (direction < 0)
    {
        page = page == 0u ? page_count - 1u : page - 1u;
    }
    else
    {
        page++;

        if (page >= page_count)
        {
            page = 0u;
        }
    }

    selected = (page * TITAN_AVATAR_PICKER_PAGE_SIZE) + local_index;

    if (selected >= TITAN_AVATAR_COUNT)
    {
        selected = TITAN_AVATAR_COUNT - 1u;
    }

    g_avatar_picker.selected_avatar = selected;
}

static void TitanAvatarPicker_MoveVertical(
    int direction
)
{
    unsigned int page;
    unsigned int page_start;
    unsigned int page_end;
    unsigned int column;
    unsigned int selected;

    page = g_avatar_picker.selected_avatar / TITAN_AVATAR_PICKER_PAGE_SIZE;
    page_start = page * TITAN_AVATAR_PICKER_PAGE_SIZE;
    page_end = page_start + TITAN_AVATAR_PICKER_PAGE_SIZE;

    if (page_end > TITAN_AVATAR_COUNT)
    {
        page_end = TITAN_AVATAR_COUNT;
    }

    selected = g_avatar_picker.selected_avatar;
    column = (selected - page_start) % TITAN_AVATAR_PICKER_COLUMNS;

    if (direction < 0)
    {
        if (selected >= page_start + TITAN_AVATAR_PICKER_COLUMNS)
        {
            selected -= TITAN_AVATAR_PICKER_COLUMNS;
        }
        else
        {
            selected = page_start +
                ((TITAN_AVATAR_PICKER_ROWS - 1u) * TITAN_AVATAR_PICKER_COLUMNS) +
                column;

            while (selected >= page_end)
            {
                selected -= TITAN_AVATAR_PICKER_COLUMNS;
            }
        }
    }
    else
    {
        if (selected + TITAN_AVATAR_PICKER_COLUMNS < page_end)
        {
            selected += TITAN_AVATAR_PICKER_COLUMNS;
        }
        else
        {
            selected = page_start + column;

            if (selected >= page_end)
            {
                selected = page_end - 1u;
            }
        }
    }

    g_avatar_picker.selected_avatar = selected;
}

void TitanAvatarPicker_Initialize(void)
{
    memset(&g_avatar_picker, 0, sizeof(g_avatar_picker));
    g_avatar_picker.initialized = 1;
    g_avatar_picker.state = TITAN_AVATAR_PICKER_CLOSED;
}

int TitanAvatarPicker_Open(
    const TitanAvatarPickerRequest *request
)
{
    if (!g_avatar_picker.initialized)
    {
        TitanAvatarPicker_Initialize();
    }

    if (
        request == 0 ||
        g_avatar_picker.state != TITAN_AVATAR_PICKER_CLOSED
    )
    {
        return 0;
    }

    TitanAvatarPicker_CopyString(
        g_avatar_picker.title,
        sizeof(g_avatar_picker.title),
        request->title
    );
    TitanAvatarPicker_CopyString(
        g_avatar_picker.profile_name,
        sizeof(g_avatar_picker.profile_name),
        request->profile_name
    );

    g_avatar_picker.selected_avatar =
        request->initial_avatar_id < TITAN_AVATAR_COUNT
            ? request->initial_avatar_id
            : 0u;
    g_avatar_picker.confirm_callback = request->confirm_callback;
    g_avatar_picker.cancel_callback = request->cancel_callback;
    g_avatar_picker.user_data = request->user_data;
    g_avatar_picker.transition_started_ms = TitanTime_NowMilliseconds();
    g_avatar_picker.state = TITAN_AVATAR_PICKER_OPENING;

    return 1;
}

void TitanAvatarPicker_Update(
    const TitanInputState *input
)
{
    if (g_avatar_picker.state == TITAN_AVATAR_PICKER_CLOSED)
    {
        return;
    }

    if (g_avatar_picker.state == TITAN_AVATAR_PICKER_OPENING)
    {
        if (TitanTime_NowMilliseconds() - g_avatar_picker.transition_started_ms >=
            TITAN_AVATAR_PICKER_OPEN_MS)
        {
            g_avatar_picker.state = TITAN_AVATAR_PICKER_ACTIVE;
        }
        return;
    }

    if (g_avatar_picker.state == TITAN_AVATAR_PICKER_CLOSING)
    {
        if (TitanTime_NowMilliseconds() - g_avatar_picker.transition_started_ms >=
            TITAN_AVATAR_PICKER_CLOSE_MS)
        {
            g_avatar_picker.state = TITAN_AVATAR_PICKER_CLOSED;
        }
        return;
    }

    if (input == 0 || !input->connected)
    {
        return;
    }

    if (
        input->pressed &
        (
            TITAN_INPUT_BUTTON_LEFT |
            TITAN_INPUT_BUTTON_RIGHT |
            TITAN_INPUT_BUTTON_UP |
            TITAN_INPUT_BUTTON_DOWN |
            TITAN_INPUT_BUTTON_L1 |
            TITAN_INPUT_BUTTON_R1
        )
    )
    {
        (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_INTERFACE_MENU_NAV);
    }

    if (input->pressed & TITAN_INPUT_BUTTON_LEFT)
    {
        g_avatar_picker.selected_avatar =
            g_avatar_picker.selected_avatar == 0u
                ? TITAN_AVATAR_COUNT - 1u
                : g_avatar_picker.selected_avatar - 1u;
    }

    if (input->pressed & TITAN_INPUT_BUTTON_RIGHT)
    {
        g_avatar_picker.selected_avatar++;

        if (g_avatar_picker.selected_avatar >= TITAN_AVATAR_COUNT)
        {
            g_avatar_picker.selected_avatar = 0u;
        }
    }

    if (input->pressed & TITAN_INPUT_BUTTON_UP)
    {
        TitanAvatarPicker_MoveVertical(-1);
    }

    if (input->pressed & TITAN_INPUT_BUTTON_DOWN)
    {
        TitanAvatarPicker_MoveVertical(1);
    }

    if (input->pressed & TITAN_INPUT_BUTTON_L1)
    {
        TitanAvatarPicker_SelectPage(-1);
    }

    if (input->pressed & TITAN_INPUT_BUTTON_R1)
    {
        TitanAvatarPicker_SelectPage(1);
    }

    if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
    {
        if (g_avatar_picker.confirm_callback != 0)
        {
            g_avatar_picker.confirm_callback(
                g_avatar_picker.selected_avatar,
                g_avatar_picker.user_data
            );
        }

        g_avatar_picker.state = TITAN_AVATAR_PICKER_CLOSING;
        g_avatar_picker.transition_started_ms = TitanTime_NowMilliseconds();
    }
    else if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
    {
        if (g_avatar_picker.cancel_callback != 0)
        {
            g_avatar_picker.cancel_callback(g_avatar_picker.user_data);
        }

        g_avatar_picker.state = TITAN_AVATAR_PICKER_CLOSING;
        g_avatar_picker.transition_started_ms = TitanTime_NowMilliseconds();
    }
}

void TitanAvatarPicker_Draw(
    unsigned int frame
)
{
    unsigned int page;
    unsigned int page_count;
    unsigned int page_start;
    unsigned int page_end;
    unsigned int avatar_id;
    unsigned int local_index;
    unsigned int column;
    unsigned int row;
    float tile_x;
    float tile_y;
    float visibility;
    float title_width;
    float name_width;
    char page_text[16];
    unsigned char alpha;

    (void)frame;

    if (g_avatar_picker.state == TITAN_AVATAR_PICKER_CLOSED)
    {
        return;
    }

    if (g_avatar_picker.state == TITAN_AVATAR_PICKER_OPENING)
    {
        visibility = (float)(
            TitanTime_NowMilliseconds() - g_avatar_picker.transition_started_ms
        ) / (float)TITAN_AVATAR_PICKER_OPEN_MS;
    }
    else if (g_avatar_picker.state == TITAN_AVATAR_PICKER_CLOSING)
    {
        visibility = 1.0f - (float)(
            TitanTime_NowMilliseconds() - g_avatar_picker.transition_started_ms
        ) / (float)TITAN_AVATAR_PICKER_CLOSE_MS;
    }
    else
    {
        visibility = 1.0f;
    }

    visibility = TitanAnimation_SmoothStep(visibility);
    alpha = TitanAvatarPicker_ModulateAlpha(255u, visibility);
    page = g_avatar_picker.selected_avatar / TITAN_AVATAR_PICKER_PAGE_SIZE;
    page_count =
        (TITAN_AVATAR_COUNT + TITAN_AVATAR_PICKER_PAGE_SIZE - 1u) /
        TITAN_AVATAR_PICKER_PAGE_SIZE;
    page_start = page * TITAN_AVATAR_PICKER_PAGE_SIZE;
    page_end = page_start + TITAN_AVATAR_PICKER_PAGE_SIZE;

    if (page_end > TITAN_AVATAR_COUNT)
    {
        page_end = TITAN_AVATAR_COUNT;
    }

    TitanVideo_DrawRectAlpha(
        0.0f,
        0.0f,
        (float)TITAN_UI_WIDTH,
        (float)TITAN_UI_HEIGHT,
        5,
        7,
        11,
        TitanAvatarPicker_ModulateAlpha(156u, visibility)
    );
    TitanVideo_DrawRectAlpha(
        145.0f,
        24.0f,
        564.0f,
        358.0f,
        15,
        18,
        24,
        TitanAvatarPicker_ModulateAlpha(224u, visibility)
    );

    title_width = TitanFont_MeasureText(0.92f, g_avatar_picker.title);
    TitanFont_DrawTextShadow(
        ((float)TITAN_UI_WIDTH - title_width) * 0.5f,
        38.0f,
        0.92f,
        g_avatar_picker.title,
        246,
        248,
        252,
        alpha
    );

    name_width = TitanFont_MeasureText(0.58f, g_avatar_picker.profile_name);
    TitanFont_DrawTextAlpha(
        ((float)TITAN_UI_WIDTH - name_width) * 0.5f,
        66.0f,
        0.58f,
        g_avatar_picker.profile_name,
        188,
        195,
        207,
        alpha
    );

    TitanAvatarPicker_FormatPage(page_text, page, page_count);
    TitanFont_DrawTextAlpha(
        651.0f,
        66.0f,
        0.52f,
        page_text,
        167,
        174,
        187,
        alpha
    );

    for (avatar_id = page_start; avatar_id < page_end; avatar_id++)
    {
        local_index = avatar_id - page_start;
        column = local_index % TITAN_AVATAR_PICKER_COLUMNS;
        row = local_index / TITAN_AVATAR_PICKER_COLUMNS;
        tile_x = TITAN_AVATAR_PICKER_GRID_X +
            ((float)column *
             (TITAN_AVATAR_PICKER_TILE_SIZE + TITAN_AVATAR_PICKER_TILE_GAP));
        tile_y = TITAN_AVATAR_PICKER_GRID_Y +
            ((float)row *
             (TITAN_AVATAR_PICKER_TILE_SIZE + TITAN_AVATAR_PICKER_TILE_GAP));

        TitanAvatars_Draw(
            avatar_id,
            tile_x,
            tile_y,
            TITAN_AVATAR_PICKER_TILE_SIZE,
            alpha
        );

        if (avatar_id == g_avatar_picker.selected_avatar)
        {
            TitanVideo_DrawRectAlpha(
                tile_x - 5.0f,
                tile_y - 5.0f,
                TITAN_AVATAR_PICKER_TILE_SIZE + 10.0f,
                TITAN_AVATAR_PICKER_TILE_SIZE + 10.0f,
                226,
                35,
                76,
                TitanAvatarPicker_ModulateAlpha(38u, visibility)
            );
            TitanAvatarPicker_DrawOutline(
                tile_x - 2.0f,
                tile_y - 2.0f,
                TITAN_AVATAR_PICKER_TILE_SIZE + 4.0f,
                TITAN_AVATAR_PICKER_TILE_SIZE + 4.0f,
                2.0f,
                238,
                43,
                86,
                alpha
            );
        }
    }

    TitanFont_DrawTextAlpha(
        171.0f,
        360.0f,
        0.50f,
        "D-Pad Navegar    L1/R1 Pagina    X Confirmar    O Voltar",
        205,
        211,
        222,
        alpha
    );
}

int TitanAvatarPicker_IsOpen(void)
{
    return g_avatar_picker.state != TITAN_AVATAR_PICKER_CLOSED;
}

unsigned int TitanAvatarPicker_GetSelected(void)
{
    return g_avatar_picker.selected_avatar;
}

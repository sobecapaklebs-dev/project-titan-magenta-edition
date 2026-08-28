#include "titan_text_input.h"

#include "titan_utf8.h"
#include "titan_time.h"

#include "titan_audio.h"

#include "titan_animation.h"
#include "titan_font.h"
#include "titan_video.h"

#include <string.h>

#define TITAN_TEXT_INPUT_TITLE_CAPACITY 64
#define TITAN_TEXT_INPUT_PLACEHOLDER_CAPACITY 64

#define TITAN_TEXT_INPUT_PANEL_WIDTH 512.0f
#define TITAN_TEXT_INPUT_PANEL_HEIGHT 236.0f
#define TITAN_TEXT_INPUT_DEFAULT_X 171.0f
#define TITAN_TEXT_INPUT_DEFAULT_Y 128.0f
#define TITAN_TEXT_INPUT_SAFE_MARGIN_X 24.0f
#define TITAN_TEXT_INPUT_SAFE_MARGIN_Y 18.0f
#define TITAN_TEXT_INPUT_TOP_EXTENT 66.0f

#define TITAN_TEXT_INPUT_CACHE_WIDTH \
    TITAN_VIDEO_CACHED_LAYER_WIDTH
#define TITAN_TEXT_INPUT_CACHE_HEIGHT \
    TITAN_VIDEO_CACHED_LAYER_HEIGHT
#define TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH 384u
#define TITAN_TEXT_INPUT_CACHE_VISIBLE_HEIGHT 236u

#define TITAN_TEXT_INPUT_KEY_WIDTH 42.0f
#define TITAN_TEXT_INPUT_KEY_HEIGHT 24.0f
#define TITAN_TEXT_INPUT_KEY_GAP 5.0f
#define TITAN_TEXT_INPUT_CHARACTER_Y 34.0f
#define TITAN_TEXT_INPUT_CHARACTER_STEP 28.0f
#define TITAN_TEXT_INPUT_FUNCTION_Y 176.0f

#define TITAN_TEXT_INPUT_OPEN_MS 134u
#define TITAN_TEXT_INPUT_CLOSE_MS 134u
#define TITAN_TEXT_INPUT_ANALOG_DEADZONE 24
#define TITAN_TEXT_INPUT_NAV_INITIAL_DELAY_MS 300u
#define TITAN_TEXT_INPUT_NAV_REPEAT_MS 83u

#define TITAN_TEXT_INPUT_CHARACTER_COLUMNS 10
#define TITAN_TEXT_INPUT_FUNCTION_KEYS 4

typedef enum TitanTextInputLayout
{
    TITAN_TEXT_INPUT_LAYOUT_ALPHA = 0,
    TITAN_TEXT_INPUT_LAYOUT_NUMBER,
    TITAN_TEXT_INPUT_LAYOUT_SYMBOL
} TitanTextInputLayout;

typedef struct TitanTextInputContext
{
    int initialized;
    TitanTextInputState state;
    TitanTextInputFieldType field_type;
    TitanTextInputLayout layout;
    int uppercase;
    int reveal_password;

    char title[TITAN_TEXT_INPUT_TITLE_CAPACITY];
    char placeholder[TITAN_TEXT_INPUT_PLACEHOLDER_CAPACITY];
    char text[TITAN_TEXT_INPUT_TEXT_CAPACITY];
    char message[TITAN_TEXT_INPUT_MESSAGE_CAPACITY];

    unsigned int maximum_codepoints;
    unsigned int cursor_codepoint;
    unsigned long long transition_started_ms;

    int selected_row;
    int selected_column;
    int navigation_direction;
    unsigned long long navigation_next_repeat_ms;

    float panel_x;
    float panel_y;
    float panel_target_x;
    float panel_target_y;

    unsigned short cache_pixels[
        TITAN_TEXT_INPUT_CACHE_WIDTH *
        TITAN_TEXT_INPUT_CACHE_HEIGHT
    ] __attribute__((aligned(64)));
    int cache_ready;

    TitanTextInputValidationCallback validation_callback;
    TitanTextInputConfirmCallback confirm_callback;
    TitanTextInputCancelCallback cancel_callback;
    void *user_data;
} TitanTextInputContext;

static TitanTextInputContext g_text_input;

static void TitanTextInput_RebuildCache(void);

static const unsigned int g_alpha_lower[4][TITAN_TEXT_INPUT_CHARACTER_COLUMNS] =
{
    { 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p' },
    { 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 0x00E7u },
    { 'z', 'x', 'c', 'v', 'b', 'n', 'm', 0x00E1u, 0x00E9u, 0x00EDu },
    { 0x00F3u, 0x00FAu, 0x00E3u, 0x00F5u, 0x00E2u, 0x00EAu, 0x00F4u, 0x00E0u, '.', '-' }
};

static const unsigned int g_alpha_upper[4][TITAN_TEXT_INPUT_CHARACTER_COLUMNS] =
{
    { 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P' },
    { 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 0x00C7u },
    { 'Z', 'X', 'C', 'V', 'B', 'N', 'M', 0x00C1u, 0x00C9u, 0x00CDu },
    { 0x00D3u, 0x00DAu, 0x00C3u, 0x00D5u, 0x00C2u, 0x00CAu, 0x00D4u, 0x00C0u, '.', '-' }
};

static const unsigned int g_number[1][TITAN_TEXT_INPUT_CHARACTER_COLUMNS] =
{
    { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0' }
};

static const unsigned int g_symbol[3][TITAN_TEXT_INPUT_CHARACTER_COLUMNS] =
{
    { '@', '#', '$', '%', '&', '*', '(', ')', '[', ']' },
    { '+', '-', '=', '/', ':', ';', '!', '?', '"', '\'' },
    { '\\', '_', ',', '.', '<', '>', '|', '~', '^', '`' }
};

static const unsigned int g_path_lower[4][TITAN_TEXT_INPUT_CHARACTER_COLUMNS] =
{
    { 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p' },
    { 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '/' },
    { 'z', 'x', 'c', 'v', 'b', 'n', 'm', '.', '_', '-' },
    { '\\', ':', 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u }
};

static const unsigned int g_path_upper[4][TITAN_TEXT_INPUT_CHARACTER_COLUMNS] =
{
    { 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P' },
    { 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', '/' },
    { 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '.', '_', '-' },
    { '\\', ':', 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u }
};

static const unsigned int g_path_symbol[6] =
{
    '/', '\\', ':', '_', '-', '.'
};

static void TitanTextInput_CopyString(
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

    destination[0] = '\0';

    if (source == 0)
    {
        return;
    }

    index = 0u;

    while (index + 1u < capacity && source[index] != '\0')
    {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

static float TitanTextInput_Clamp(
    float value,
    float minimum,
    float maximum
)
{
    if (value < minimum)
    {
        return minimum;
    }

    if (value > maximum)
    {
        return maximum;
    }

    return value;
}

static unsigned char TitanTextInput_Alpha(
    float value
)
{
    value = TitanTextInput_Clamp(value, 0.0f, 255.0f);
    return (unsigned char)(value + 0.5f);
}

static unsigned int TitanTextInput_DecodeCodepoint(
    const char *text,
    unsigned int *byte_index
)
{
    return TitanUtf8_DecodeOne(text, byte_index);
}

static unsigned int TitanTextInput_EncodeCodepoint(
    unsigned int codepoint,
    char output[5]
)
{
    return TitanUtf8_EncodeOne(codepoint, output);
}

static unsigned int TitanTextInput_CountCodepoints(
    const char *text
)
{
    unsigned int count;
    unsigned int index;

    if (text == 0)
    {
        return 0u;
    }

    count = 0u;
    index = 0u;

    while (text[index] != '\0')
    {
        TitanTextInput_DecodeCodepoint(text, &index);
        count++;
    }

    return count;
}

static unsigned int TitanTextInput_GetByteOffset(
    const char *text,
    unsigned int codepoint_index
)
{
    unsigned int current;
    unsigned int byte_index;

    if (text == 0)
    {
        return 0u;
    }

    current = 0u;
    byte_index = 0u;

    while (text[byte_index] != '\0' && current < codepoint_index)
    {
        TitanTextInput_DecodeCodepoint(text, &byte_index);
        current++;
    }

    return byte_index;
}

static int TitanTextInput_IsCodepointAllowed(
    unsigned int codepoint
)
{
    if (g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_IP_ADDRESS)
    {
        return
            (codepoint >= (unsigned int)'0' && codepoint <= (unsigned int)'9') ||
            codepoint == (unsigned int)'.';
    }

    if (g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_PORT)
    {
        return codepoint >= (unsigned int)'0' && codepoint <= (unsigned int)'9';
    }

    if (g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_PATH)
    {
        if (
            (codepoint >= (unsigned int)'a' && codepoint <= (unsigned int)'z') ||
            (codepoint >= (unsigned int)'A' && codepoint <= (unsigned int)'Z') ||
            (codepoint >= (unsigned int)'0' && codepoint <= (unsigned int)'9')
        )
        {
            return 1;
        }

        return
            codepoint == (unsigned int)' ' ||
            codepoint == (unsigned int)'/' ||
            codepoint == (unsigned int)'\\' ||
            codepoint == (unsigned int)':' ||
            codepoint == (unsigned int)'_' ||
            codepoint == (unsigned int)'-' ||
            codepoint == (unsigned int)'.';
    }

    return codepoint >= 0x20u;
}

static int TitanTextInput_InsertCodepoint(
    unsigned int codepoint
)
{
    char encoded[5];
    unsigned int encoded_length;
    unsigned int byte_offset;
    unsigned int byte_length;
    unsigned int codepoint_count;

    if (!TitanTextInput_IsCodepointAllowed(codepoint))
    {
        return 0;
    }

    codepoint_count = TitanTextInput_CountCodepoints(g_text_input.text);

    if (codepoint_count >= g_text_input.maximum_codepoints)
    {
        TitanTextInput_CopyString(
            g_text_input.message,
            sizeof(g_text_input.message),
            "Limite de caracteres atingido."
        );
        return 0;
    }

    encoded_length = TitanTextInput_EncodeCodepoint(codepoint, encoded);
    byte_length = (unsigned int)strlen(g_text_input.text);

    if (
        encoded_length == 0u ||
        byte_length + encoded_length >= sizeof(g_text_input.text)
    )
    {
        return 0;
    }

    byte_offset = TitanTextInput_GetByteOffset(
        g_text_input.text,
        g_text_input.cursor_codepoint
    );

    memmove(
        g_text_input.text + byte_offset + encoded_length,
        g_text_input.text + byte_offset,
        byte_length - byte_offset + 1u
    );

    memcpy(
        g_text_input.text + byte_offset,
        encoded,
        encoded_length
    );

    g_text_input.cursor_codepoint++;
    g_text_input.message[0] = '\0';

    return 1;
}

static void TitanTextInput_DeletePrevious(void)
{
    unsigned int start;
    unsigned int end;
    unsigned int byte_length;

    if (g_text_input.cursor_codepoint == 0u)
    {
        return;
    }

    start = TitanTextInput_GetByteOffset(
        g_text_input.text,
        g_text_input.cursor_codepoint - 1u
    );

    end = TitanTextInput_GetByteOffset(
        g_text_input.text,
        g_text_input.cursor_codepoint
    );

    byte_length = (unsigned int)strlen(g_text_input.text);

    memmove(
        g_text_input.text + start,
        g_text_input.text + end,
        byte_length - end + 1u
    );

    g_text_input.cursor_codepoint--;
    g_text_input.message[0] = '\0';
}

static int TitanTextInput_GetCharacterRowCount(void)
{
    if (g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_IP_ADDRESS)
    {
        return 2;
    }

    if (g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_PORT)
    {
        return 1;
    }

    if (g_text_input.layout == TITAN_TEXT_INPUT_LAYOUT_NUMBER)
    {
        return 1;
    }

    if (g_text_input.layout == TITAN_TEXT_INPUT_LAYOUT_SYMBOL)
    {
        if (g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_PATH)
        {
            return 1;
        }

        return 3;
    }

    return 5;
}

static int TitanTextInput_GetKeyCount(
    int row
)
{
    int character_rows;

    character_rows = TitanTextInput_GetCharacterRowCount();

    if (row < 0 || row > character_rows)
    {
        return 0;
    }

    if (row == character_rows)
    {
        if (
            g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_IP_ADDRESS ||
            g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_PORT
        )
        {
            return 1;
        }

        return TITAN_TEXT_INPUT_FUNCTION_KEYS;
    }

    if (
        g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_IP_ADDRESS &&
        row == 1
    )
    {
        return 1;
    }

    if (
        g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_PATH &&
        g_text_input.layout == TITAN_TEXT_INPUT_LAYOUT_ALPHA &&
        row == 4
    )
    {
        return 2;
    }

    if (
        g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_PATH &&
        g_text_input.layout == TITAN_TEXT_INPUT_LAYOUT_SYMBOL &&
        row == 0
    )
    {
        return 6;
    }

    return TITAN_TEXT_INPUT_CHARACTER_COLUMNS;
}

static unsigned int TitanTextInput_GetCharacter(
    int row,
    int column
)
{
    if (row < 0 || column < 0 || column >= TITAN_TEXT_INPUT_CHARACTER_COLUMNS)
    {
        return 0u;
    }

    if (g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_IP_ADDRESS)
    {
        if (row == 0)
        {
            return g_number[0][column];
        }

        return column == 0 ? (unsigned int)'.' : 0u;
    }

    if (g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_PORT)
    {
        return row == 0 ? g_number[0][column] : 0u;
    }

    if (g_text_input.layout == TITAN_TEXT_INPUT_LAYOUT_NUMBER)
    {
        return row == 0 ? g_number[0][column] : 0u;
    }

    if (g_text_input.layout == TITAN_TEXT_INPUT_LAYOUT_SYMBOL)
    {
        if (
            g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_PATH &&
            row == 0 &&
            column < 6
        )
        {
            return g_path_symbol[column];
        }

        if (row >= 0 && row < 3)
        {
            return g_symbol[row][column];
        }

        return 0u;
    }

    if (row == 0)
    {
        return g_number[0][column];
    }

    if (row > 0 && row <= 4)
    {
        if (g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_PATH)
        {
            return g_text_input.uppercase
                ? g_path_upper[row - 1][column]
                : g_path_lower[row - 1][column];
        }

        return g_text_input.uppercase
            ? g_alpha_upper[row - 1][column]
            : g_alpha_lower[row - 1][column];
    }

    return 0u;
}

static void TitanTextInput_ClampSelection(void)
{
    int row_count;
    int key_count;

    row_count = TitanTextInput_GetCharacterRowCount() + 1;

    if (g_text_input.selected_row < 0)
    {
        g_text_input.selected_row = row_count - 1;
    }
    else if (g_text_input.selected_row >= row_count)
    {
        g_text_input.selected_row = 0;
    }

    key_count = TitanTextInput_GetKeyCount(g_text_input.selected_row);

    if (key_count <= 0)
    {
        g_text_input.selected_column = 0;
    }
    else if (g_text_input.selected_column < 0)
    {
        g_text_input.selected_column = key_count - 1;
    }
    else if (g_text_input.selected_column >= key_count)
    {
        g_text_input.selected_column = key_count - 1;
    }
}

static void TitanTextInput_MoveSelection(
    int direction
)
{
    int previous_count;
    int next_count;
    int previous_column;

    if (direction == -1 || direction == 1)
    {
        g_text_input.selected_column += direction;

        next_count = TitanTextInput_GetKeyCount(g_text_input.selected_row);

        if (g_text_input.selected_column < 0)
        {
            g_text_input.selected_column = next_count - 1;
        }
        else if (g_text_input.selected_column >= next_count)
        {
            g_text_input.selected_column = 0;
        }

        return;
    }

    previous_count = TitanTextInput_GetKeyCount(g_text_input.selected_row);
    previous_column = g_text_input.selected_column;

    if (direction == -2)
    {
        g_text_input.selected_row--;
    }
    else if (direction == 2)
    {
        g_text_input.selected_row++;
    }

    TitanTextInput_ClampSelection();
    next_count = TitanTextInput_GetKeyCount(g_text_input.selected_row);

    if (previous_count > 1 && next_count > 1)
    {
        g_text_input.selected_column =
            (previous_column * (next_count - 1)) /
            (previous_count - 1);
    }

    TitanTextInput_ClampSelection();
}

static int TitanTextInput_GetNavigationDirection(
    const TitanInputState *input
)
{
    int horizontal;
    int vertical;
    int direction;

    if (input == 0 || !input->connected)
    {
        g_text_input.navigation_direction = 0;
        g_text_input.navigation_next_repeat_ms = 0u;
        return 0;
    }

    horizontal = (int)input->left_stick_x - 128;
    vertical = (int)input->left_stick_y - 128;
    direction = 0;

    if (input->buttons & TITAN_INPUT_BUTTON_LEFT)
    {
        direction = -1;
    }
    else if (input->buttons & TITAN_INPUT_BUTTON_RIGHT)
    {
        direction = 1;
    }
    else if (input->buttons & TITAN_INPUT_BUTTON_UP)
    {
        direction = -2;
    }
    else if (input->buttons & TITAN_INPUT_BUTTON_DOWN)
    {
        direction = 2;
    }
    else if (
        (horizontal < -TITAN_TEXT_INPUT_ANALOG_DEADZONE ||
         horizontal > TITAN_TEXT_INPUT_ANALOG_DEADZONE) &&
        (horizontal < 0 ? -horizontal : horizontal) >=
            (vertical < 0 ? -vertical : vertical)
    )
    {
        direction = horizontal < 0 ? -1 : 1;
    }
    else if (
        vertical < -TITAN_TEXT_INPUT_ANALOG_DEADZONE ||
        vertical > TITAN_TEXT_INPUT_ANALOG_DEADZONE
    )
    {
        direction = vertical < 0 ? -2 : 2;
    }

    if (direction == 0)
    {
        g_text_input.navigation_direction = 0;
        g_text_input.navigation_next_repeat_ms = 0u;
        return 0;
    }

    if (direction != g_text_input.navigation_direction)
    {
        g_text_input.navigation_direction = direction;
        g_text_input.navigation_next_repeat_ms =
            TitanTime_NowMilliseconds() + TITAN_TEXT_INPUT_NAV_INITIAL_DELAY_MS;
        return direction;
    }

    if (
        g_text_input.navigation_next_repeat_ms != 0u &&
        TitanTime_NowMilliseconds() >= g_text_input.navigation_next_repeat_ms
    )
    {
        g_text_input.navigation_next_repeat_ms += TITAN_TEXT_INPUT_NAV_REPEAT_MS;
        return direction;
    }

    return 0;
}

static float TitanTextInput_GetAnalogVelocity(
    int value
)
{
    int magnitude;
    float normalized;
    float velocity;

    magnitude = value < 0 ? -value : value;

    if (magnitude <= TITAN_TEXT_INPUT_ANALOG_DEADZONE)
    {
        return 0.0f;
    }

    normalized =
        (float)(magnitude - TITAN_TEXT_INPUT_ANALOG_DEADZONE) /
        (float)(127 - TITAN_TEXT_INPUT_ANALOG_DEADZONE);

    normalized = TitanTextInput_Clamp(normalized, 0.0f, 1.0f);
    velocity = 0.65f + (3.35f * normalized);

    return value < 0 ? -velocity : velocity;
}

static void TitanTextInput_UpdatePanelPosition(
    const TitanInputState *input
)
{
    float maximum_x;
    float maximum_y;

    maximum_x =
        (float)TITAN_UI_WIDTH -
        TITAN_TEXT_INPUT_SAFE_MARGIN_X -
        TITAN_TEXT_INPUT_PANEL_WIDTH;

    maximum_y =
        (float)TITAN_UI_HEIGHT -
        TITAN_TEXT_INPUT_SAFE_MARGIN_Y -
        TITAN_TEXT_INPUT_PANEL_HEIGHT;

    if (input != 0 && input->connected)
    {
        if (input->pressed & TITAN_INPUT_BUTTON_R3)
        {
            g_text_input.panel_target_x = TITAN_TEXT_INPUT_DEFAULT_X;
            g_text_input.panel_target_y = TITAN_TEXT_INPUT_DEFAULT_Y;
        }
        else
        {
            g_text_input.panel_target_x += TitanTextInput_GetAnalogVelocity(
                (int)input->right_stick_x - 128
            );

            g_text_input.panel_target_y += TitanTextInput_GetAnalogVelocity(
                (int)input->right_stick_y - 128
            );
        }
    }

    g_text_input.panel_target_x = TitanTextInput_Clamp(
        g_text_input.panel_target_x,
        TITAN_TEXT_INPUT_SAFE_MARGIN_X,
        maximum_x
    );

    g_text_input.panel_target_y = TitanTextInput_Clamp(
        g_text_input.panel_target_y,
        TITAN_TEXT_INPUT_SAFE_MARGIN_Y +
            TITAN_TEXT_INPUT_TOP_EXTENT,
        maximum_y
    );

    g_text_input.panel_x = TitanAnimation_Approach(
        g_text_input.panel_x,
        g_text_input.panel_target_x,
        0.32f
    );

    g_text_input.panel_y = TitanAnimation_Approach(
        g_text_input.panel_y,
        g_text_input.panel_target_y,
        0.32f
    );
}

static void TitanTextInput_RequestConfirm(void)
{
    if (g_text_input.state == TITAN_TEXT_INPUT_ACTIVE)
    {
        g_text_input.state = TITAN_TEXT_INPUT_VALIDATING;
    }
}

static void TitanTextInput_ActivateSelectedKey(void)
{
    int character_rows;
    unsigned int codepoint;

    character_rows = TitanTextInput_GetCharacterRowCount();

    if (g_text_input.selected_row < character_rows)
    {
        codepoint = TitanTextInput_GetCharacter(
            g_text_input.selected_row,
            g_text_input.selected_column
        );

        if (codepoint != 0u)
        {
            TitanTextInput_InsertCodepoint(codepoint);
        }

        return;
    }

    if (
        g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_IP_ADDRESS ||
        g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_PORT
    )
    {
        TitanTextInput_RequestConfirm();
        return;
    }

    if (g_text_input.selected_column == 0)
    {
        g_text_input.layout =
            g_text_input.layout == TITAN_TEXT_INPUT_LAYOUT_ALPHA
                ? TITAN_TEXT_INPUT_LAYOUT_NUMBER
                : TITAN_TEXT_INPUT_LAYOUT_ALPHA;
        g_text_input.selected_row = 0;
        g_text_input.selected_column = 0;
        TitanTextInput_RebuildCache();
    }
    else if (g_text_input.selected_column == 1)
    {
        TitanTextInput_InsertCodepoint((unsigned int)' ');
    }
    else if (g_text_input.selected_column == 2)
    {
        g_text_input.layout =
            g_text_input.layout == TITAN_TEXT_INPUT_LAYOUT_SYMBOL
                ? TITAN_TEXT_INPUT_LAYOUT_ALPHA
                : TITAN_TEXT_INPUT_LAYOUT_SYMBOL;
        g_text_input.selected_row = 0;
        g_text_input.selected_column = 0;
        TitanTextInput_RebuildCache();
    }
    else if (g_text_input.selected_column == 3)
    {
        TitanTextInput_RequestConfirm();
    }
}

static void TitanTextInput_ProcessValidation(void)
{
    int valid;

    g_text_input.message[0] = '\0';

    if (g_text_input.validation_callback != 0)
    {
        valid = g_text_input.validation_callback(
            g_text_input.text,
            g_text_input.message,
            sizeof(g_text_input.message),
            g_text_input.user_data
        );
    }
    else
    {
        valid = g_text_input.text[0] != '\0';

        if (!valid)
        {
            TitanTextInput_CopyString(
                g_text_input.message,
                sizeof(g_text_input.message),
                "Preencha este campo."
            );
        }
    }

    if (!valid)
    {
        g_text_input.state = TITAN_TEXT_INPUT_ACTIVE;
        return;
    }

    if (g_text_input.confirm_callback != 0)
    {
        g_text_input.confirm_callback(
            g_text_input.text,
            g_text_input.user_data
        );
    }

    g_text_input.state = TITAN_TEXT_INPUT_CLOSING;
    g_text_input.transition_started_ms = TitanTime_NowMilliseconds();
}

void TitanTextInput_Initialize(void)
{
    memset(&g_text_input, 0, sizeof(g_text_input));
    g_text_input.initialized = 1;
    g_text_input.state = TITAN_TEXT_INPUT_CLOSED;
    g_text_input.panel_x = TITAN_TEXT_INPUT_DEFAULT_X;
    g_text_input.panel_y = TITAN_TEXT_INPUT_DEFAULT_Y;
    g_text_input.panel_target_x = TITAN_TEXT_INPUT_DEFAULT_X;
    g_text_input.panel_target_y = TITAN_TEXT_INPUT_DEFAULT_Y;
}

int TitanTextInput_Open(
    const TitanTextInputRequest *request
)
{
    unsigned int source_index;
    unsigned int codepoint;

    if (!g_text_input.initialized)
    {
        TitanTextInput_Initialize();
    }

    if (
        request == 0 ||
        g_text_input.state != TITAN_TEXT_INPUT_CLOSED
    )
    {
        return 0;
    }

    TitanTextInput_CopyString(
        g_text_input.title,
        sizeof(g_text_input.title),
        request->title
    );

    TitanTextInput_CopyString(
        g_text_input.placeholder,
        sizeof(g_text_input.placeholder),
        request->placeholder
    );

    g_text_input.text[0] = '\0';
    g_text_input.message[0] = '\0';
    g_text_input.maximum_codepoints = request->maximum_codepoints;

    if (
        g_text_input.maximum_codepoints == 0u ||
        g_text_input.maximum_codepoints > 64u
    )
    {
        g_text_input.maximum_codepoints = 64u;
    }

    g_text_input.field_type = request->field_type;
    g_text_input.layout =
        (
            request->field_type == TITAN_TEXT_INPUT_FIELD_IP_ADDRESS ||
            request->field_type == TITAN_TEXT_INPUT_FIELD_PORT
        )
            ? TITAN_TEXT_INPUT_LAYOUT_NUMBER
            : TITAN_TEXT_INPUT_LAYOUT_ALPHA;

    g_text_input.uppercase = 0;
    g_text_input.reveal_password = 0;
    g_text_input.cursor_codepoint = 0u;

    if (request->initial_text != 0)
    {
        source_index = 0u;

        while (
            request->initial_text[source_index] != '\0' &&
            g_text_input.cursor_codepoint < g_text_input.maximum_codepoints
        )
        {
            codepoint = TitanTextInput_DecodeCodepoint(
                request->initial_text,
                &source_index
            );

            TitanTextInput_InsertCodepoint(codepoint);
        }
    }

    g_text_input.message[0] = '\0';

    g_text_input.selected_row = 0;
    g_text_input.selected_column = 0;
    g_text_input.navigation_direction = 0;
    g_text_input.navigation_next_repeat_ms = 0u;
    g_text_input.validation_callback = request->validation_callback;
    g_text_input.confirm_callback = request->confirm_callback;
    g_text_input.cancel_callback = request->cancel_callback;
    g_text_input.user_data = request->user_data;
    g_text_input.transition_started_ms = TitanTime_NowMilliseconds();
    TitanTextInput_RebuildCache();
    g_text_input.state = TITAN_TEXT_INPUT_OPENING;

    return 1;
}

void TitanTextInput_Update(
    const TitanInputState *input
)
{
    int navigation;
    unsigned int codepoint_count;

    if (g_text_input.state == TITAN_TEXT_INPUT_CLOSED)
    {
        return;
    }

    TitanTextInput_UpdatePanelPosition(input);

    g_text_input.reveal_password =
        input != 0 &&
        input->connected &&
        (input->buttons & TITAN_INPUT_BUTTON_L2) != 0;

    if (g_text_input.state == TITAN_TEXT_INPUT_OPENING)
    {
        if (TitanTime_NowMilliseconds() - g_text_input.transition_started_ms >=
            TITAN_TEXT_INPUT_OPEN_MS)
        {
            g_text_input.state = TITAN_TEXT_INPUT_ACTIVE;
        }
        return;
    }

    if (g_text_input.state == TITAN_TEXT_INPUT_CLOSING)
    {
        if (TitanTime_NowMilliseconds() - g_text_input.transition_started_ms >=
            TITAN_TEXT_INPUT_CLOSE_MS)
        {
            g_text_input.state = TITAN_TEXT_INPUT_CLOSED;
        }
        return;
    }

    if (g_text_input.state == TITAN_TEXT_INPUT_VALIDATING)
    {
        TitanTextInput_ProcessValidation();
        return;
    }

    if (input == 0 || !input->connected)
    {
        return;
    }

    navigation = TitanTextInput_GetNavigationDirection(input);

    if (navigation != 0)
    {
        TitanTextInput_MoveSelection(navigation);
        (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_INTERFACE_MENU_NAV);
    }

    if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
    {
        TitanTextInput_ActivateSelectedKey();
    }

    if (input->pressed & TITAN_INPUT_BUTTON_SQUARE)
    {
        TitanTextInput_DeletePrevious();
    }

    if (input->pressed & TITAN_INPUT_BUTTON_TRIANGLE)
    {
        if (g_text_input.layout == TITAN_TEXT_INPUT_LAYOUT_ALPHA)
        {
            g_text_input.uppercase = !g_text_input.uppercase;
            TitanTextInput_RebuildCache();
        }
    }

    codepoint_count = TitanTextInput_CountCodepoints(g_text_input.text);

    if (
        (input->pressed & TITAN_INPUT_BUTTON_L1) &&
        g_text_input.cursor_codepoint > 0u
    )
    {
        g_text_input.cursor_codepoint--;
    }

    if (
        (input->pressed & TITAN_INPUT_BUTTON_R1) &&
        g_text_input.cursor_codepoint < codepoint_count
    )
    {
        g_text_input.cursor_codepoint++;
    }

    if (
        input->pressed &
        (TITAN_INPUT_BUTTON_R2 | TITAN_INPUT_BUTTON_START)
    )
    {
        TitanTextInput_RequestConfirm();
    }

    if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
    {
        if (g_text_input.cancel_callback != 0)
        {
            g_text_input.cancel_callback(g_text_input.user_data);
        }

        g_text_input.state = TITAN_TEXT_INPUT_CLOSING;
        g_text_input.transition_started_ms = TitanTime_NowMilliseconds();
    }
}

static void TitanTextInput_DrawOutline(
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
    TitanVideo_DrawLineAlpha(x, y, x + width, y, thickness, red, green, blue, alpha);
    TitanVideo_DrawLineAlpha(x + width, y, x + width, y + height, thickness, red, green, blue, alpha);
    TitanVideo_DrawLineAlpha(x + width, y + height, x, y + height, thickness, red, green, blue, alpha);
    TitanVideo_DrawLineAlpha(x, y + height, x, y, thickness, red, green, blue, alpha);
}

static void TitanTextInput_BuildDisplayText(
    int reveal_password,
    char *destination,
    unsigned int capacity
)
{
    unsigned int count;
    unsigned int index;

    if (
        g_text_input.field_type != TITAN_TEXT_INPUT_FIELD_PASSWORD ||
        reveal_password
    )
    {
        TitanTextInput_CopyString(
            destination,
            capacity,
            g_text_input.text
        );
        return;
    }

    if (destination == 0 || capacity == 0u)
    {
        return;
    }

    count = TitanTextInput_CountCodepoints(g_text_input.text);
    index = 0u;

    while (index < count && index + 1u < capacity)
    {
        destination[index] = '*';
        index++;
    }

    destination[index] = '\0';
}

static void TitanTextInput_BuildCursorPrefix(
    const char *display_text,
    int masked,
    char *destination,
    unsigned int capacity
)
{
    unsigned int byte_offset;
    unsigned int copy_length;

    if (destination == 0 || capacity == 0u)
    {
        return;
    }

    if (masked)
    {
        copy_length = g_text_input.cursor_codepoint;

        if (copy_length >= capacity)
        {
            copy_length = capacity - 1u;
        }

        memset(destination, '*', copy_length);
        destination[copy_length] = '\0';
        return;
    }

    byte_offset = TitanTextInput_GetByteOffset(
        display_text,
        g_text_input.cursor_codepoint
    );

    if (byte_offset >= capacity)
    {
        byte_offset = capacity - 1u;
    }

    memcpy(destination, display_text, byte_offset);
    destination[byte_offset] = '\0';
}

static void TitanTextInput_GetFunctionLabel(
    int column,
    const char **label
)
{
    if (label == 0)
    {
        return;
    }

    if (
        g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_IP_ADDRESS ||
        g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_PORT
    )
    {
        *label = "OK";
        return;
    }

    if (column == 0)
    {
        *label =
            g_text_input.layout == TITAN_TEXT_INPUT_LAYOUT_ALPHA
                ? "123"
                : "ABC";
    }
    else if (column == 1)
    {
        *label = "Espaco";
    }
    else if (column == 2)
    {
        *label =
            g_text_input.layout == TITAN_TEXT_INPUT_LAYOUT_SYMBOL
                ? "ABC"
                : "#+=";
    }
    else
    {
        *label = "Concluir";
    }
}

static void TitanTextInput_GetKeyGeometry(
    int row,
    int column,
    float *x,
    float *y,
    float *width,
    float *height
)
{
    static const float function_widths[
        TITAN_TEXT_INPUT_FUNCTION_KEYS
    ] =
    {
        72.0f,
        198.0f,
        72.0f,
        98.0f
    };
    int character_rows;
    int key_count;
    int index;
    float row_width;
    float row_x;

    if (x == 0 || y == 0 || width == 0 || height == 0)
    {
        return;
    }

    character_rows = TitanTextInput_GetCharacterRowCount();
    key_count = TitanTextInput_GetKeyCount(row);
    *x = 0.0f;
    *y = 0.0f;
    *width = 0.0f;
    *height = TITAN_TEXT_INPUT_KEY_HEIGHT;

    if (row < character_rows)
    {
        row_width =
            ((float)key_count * TITAN_TEXT_INPUT_KEY_WIDTH) +
            ((float)(key_count - 1) * TITAN_TEXT_INPUT_KEY_GAP);
        row_x =
            (TITAN_TEXT_INPUT_PANEL_WIDTH - row_width) * 0.5f;
        *x = row_x +
            ((float)column *
             (TITAN_TEXT_INPUT_KEY_WIDTH + TITAN_TEXT_INPUT_KEY_GAP));
        *y = TITAN_TEXT_INPUT_CHARACTER_Y +
            ((float)row * TITAN_TEXT_INPUT_CHARACTER_STEP);
        *width = TITAN_TEXT_INPUT_KEY_WIDTH;
        return;
    }

    *y = TITAN_TEXT_INPUT_FUNCTION_Y;

    if (key_count == 1)
    {
        *x = 28.5f;
        *width = 455.0f;
        return;
    }

    row_x = 28.5f;

    for (index = 0; index < column; index++)
    {
        row_x +=
            function_widths[index] +
            TITAN_TEXT_INPUT_KEY_GAP;
    }

    *x = row_x;
    *width = function_widths[column];
}

static unsigned short TitanTextInput_CacheColor(
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    return (unsigned short)(
        ((unsigned short)(red >> 3u)) |
        ((unsigned short)(green >> 3u) << 5u) |
        ((unsigned short)(blue >> 3u) << 10u) |
        0x8000u
    );
}

static int TitanTextInput_CacheX(float logical_x)
{
    return (int)(
        (logical_x *
         (float)TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH /
         TITAN_TEXT_INPUT_PANEL_WIDTH) +
        0.5f
    );
}

static void TitanTextInput_CacheFillRect(
    int x,
    int y,
    int width,
    int height,
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    int row;
    int column;
    unsigned short color;

    if (x < 0)
    {
        width += x;
        x = 0;
    }

    if (y < 0)
    {
        height += y;
        y = 0;
    }

    if (
        x + width > (int)TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH
    )
    {
        width =
            (int)TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH - x;
    }

    if (
        y + height > (int)TITAN_TEXT_INPUT_CACHE_VISIBLE_HEIGHT
    )
    {
        height =
            (int)TITAN_TEXT_INPUT_CACHE_VISIBLE_HEIGHT - y;
    }

    if (width <= 0 || height <= 0)
    {
        return;
    }

    color = TitanTextInput_CacheColor(red, green, blue);

    for (row = y; row < y + height; row++)
    {
        for (column = x; column < x + width; column++)
        {
            g_text_input.cache_pixels[
                ((unsigned int)row * TITAN_TEXT_INPUT_CACHE_WIDTH) +
                (unsigned int)column
            ] = color;
        }
    }
}

static void TitanTextInput_CacheOutline(
    int x,
    int y,
    int width,
    int height,
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    TitanTextInput_CacheFillRect(x, y, width, 1, red, green, blue);
    TitanTextInput_CacheFillRect(x, y + height - 1, width, 1, red, green, blue);
    TitanTextInput_CacheFillRect(x, y, 1, height, red, green, blue);
    TitanTextInput_CacheFillRect(x + width - 1, y, 1, height, red, green, blue);
}

static void TitanTextInput_CacheLine(
    int x0,
    int y0,
    int x1,
    int y1,
    unsigned char red,
    unsigned char green,
    unsigned char blue
)
{
    int delta_x;
    int delta_y;
    int step_x;
    int step_y;
    int error;
    unsigned short color;

    delta_x = x1 >= x0 ? x1 - x0 : x0 - x1;
    delta_y = y1 >= y0 ? y0 - y1 : y1 - y0;
    step_x = x0 < x1 ? 1 : -1;
    step_y = y0 < y1 ? 1 : -1;
    error = delta_x + delta_y;
    color = TitanTextInput_CacheColor(red, green, blue);

    for (;;)
    {
        int doubled_error;

        if (
            x0 >= 0 &&
            y0 >= 0 &&
            x0 < (int)TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH &&
            y0 < (int)TITAN_TEXT_INPUT_CACHE_VISIBLE_HEIGHT
        )
        {
            g_text_input.cache_pixels[
                ((unsigned int)y0 * TITAN_TEXT_INPUT_CACHE_WIDTH) +
                (unsigned int)x0
            ] = color;
        }

        if (x0 == x1 && y0 == y1)
        {
            break;
        }

        doubled_error = error * 2;

        if (doubled_error >= delta_y)
        {
            error += delta_y;
            x0 += step_x;
        }

        if (doubled_error <= delta_x)
        {
            error += delta_x;
            y0 += step_y;
        }
    }
}

static void TitanTextInput_CacheLabel(
    float logical_x,
    float logical_y,
    float logical_width,
    float logical_height,
    const char *label
)
{
    float label_width;
    int x;
    int y;
    int width;
    int height;

    x = TitanTextInput_CacheX(logical_x);
    y = (int)(logical_y + 0.5f);
    width = TitanTextInput_CacheX(
        logical_x + logical_width
    ) - x;
    height = (int)(logical_height + 0.5f);
    label_width = TitanFont_MeasureTextRaster(0.40f, label);

    TitanFont_RasterizeText16(
        g_text_input.cache_pixels,
        TITAN_TEXT_INPUT_CACHE_WIDTH,
        TITAN_TEXT_INPUT_CACHE_HEIGHT,
        TITAN_TEXT_INPUT_CACHE_WIDTH,
        x + (int)(((float)width - label_width) * 0.5f),
        y + ((height - 12) / 2),
        0.40f,
        0.50f,
        label,
        247,
        248,
        250
    );
}

static void TitanTextInput_BuildCache(void)
{
    int character_rows;
    int row;
    int column;
    int key_count;
    unsigned int codepoint;
    char label_buffer[5];
    const char *label;
    float key_x;
    float key_y;
    float key_width;
    float key_height;
    int physical_x;
    int physical_y;
    int physical_width;
    int physical_height;
    float footer_width;

    memset(
        g_text_input.cache_pixels,
        0,
        sizeof(g_text_input.cache_pixels)
    );

    TitanTextInput_CacheFillRect(
        0,
        0,
        TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH,
        TITAN_TEXT_INPUT_CACHE_VISIBLE_HEIGHT,
        8,
        9,
        12
    );

    TitanTextInput_CacheFillRect(
        0,
        0,
        TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH,
        28,
        1,
        2,
        4
    );

    TitanTextInput_CacheFillRect(
        (int)TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH - 28,
        0,
        28,
        28,
        48,
        48,
        49
    );

    TitanTextInput_CacheLine(
        (int)TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH - 20,
        8,
        (int)TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH - 8,
        20,
        248,
        248,
        248
    );
    TitanTextInput_CacheLine(
        (int)TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH - 8,
        8,
        (int)TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH - 20,
        20,
        248,
        248,
        248
    );

    TitanTextInput_CacheOutline(
        0,
        0,
        TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH,
        TITAN_TEXT_INPUT_CACHE_VISIBLE_HEIGHT,
        126,
        132,
        145
    );

    character_rows = TitanTextInput_GetCharacterRowCount();

    for (row = 0; row <= character_rows; row++)
    {
        key_count = TitanTextInput_GetKeyCount(row);

        for (column = 0; column < key_count; column++)
        {
            TitanTextInput_GetKeyGeometry(
                row,
                column,
                &key_x,
                &key_y,
                &key_width,
                &key_height
            );

            physical_x = TitanTextInput_CacheX(key_x);
            physical_y = (int)(key_y + 0.5f);
            physical_width =
                TitanTextInput_CacheX(key_x + key_width) -
                physical_x;
            physical_height = (int)(key_height + 0.5f);

            if (row == character_rows && column == key_count - 1)
            {
                TitanTextInput_CacheFillRect(
                    physical_x,
                    physical_y,
                    physical_width,
                    physical_height,
                    156,
                    27,
                    53
                );
            }
            else
            {
                TitanTextInput_CacheFillRect(
                    physical_x,
                    physical_y,
                    physical_width,
                    physical_height,
                    45,
                    45,
                    47
                );
            }

            if (row < character_rows)
            {
                codepoint = TitanTextInput_GetCharacter(row, column);
                TitanTextInput_EncodeCodepoint(codepoint, label_buffer);
                label = label_buffer;
            }
            else
            {
                TitanTextInput_GetFunctionLabel(column, &label);
            }

            TitanTextInput_CacheLabel(
                key_x,
                key_y,
                key_width,
                key_height,
                label
            );
        }
    }

    footer_width = TitanFont_MeasureTextRaster(
        0.30f,
        "L1/R1 Cursor   QUADRADO Apagar   R2 Concluir"
    );

    TitanFont_RasterizeText16(
        g_text_input.cache_pixels,
        TITAN_TEXT_INPUT_CACHE_WIDTH,
        TITAN_TEXT_INPUT_CACHE_HEIGHT,
        TITAN_TEXT_INPUT_CACHE_WIDTH,
        ((int)TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH -
         (int)footer_width) /
            2,
        213,
        0.30f,
        0.40f,
        "L1/R1 Cursor   QUADRADO Apagar   R2 Concluir",
        174,
        179,
        188
    );
}

static void TitanTextInput_RebuildCache(void)
{
    TitanTextInput_BuildCache();

    g_text_input.cache_ready =
        TitanVideo_UploadCachedLayer16(
            g_text_input.cache_pixels,
            TITAN_TEXT_INPUT_CACHE_WIDTH,
            TITAN_TEXT_INPUT_CACHE_HEIGHT,
            TITAN_TEXT_INPUT_CACHE_WIDTH
        ) == TITAN_RESULT_OK;
}

static void TitanTextInput_DrawKey(
    float x,
    float y,
    float width,
    float height,
    const char *label,
    int selected,
    unsigned char alpha
)
{
    float label_width;

    TitanVideo_DrawRectAlpha(
        x,
        y,
        width,
        height,
        selected ? 72 : 45,
        selected ? 105 : 45,
        selected ? 113 : 47,
        alpha
    );

    if (selected)
    {
        TitanTextInput_DrawOutline(
            x,
            y,
            width,
            height,
            1.5f,
            209,
            249,
            255,
            alpha
        );
    }

    label_width = TitanFont_MeasureText(0.50f, label);

    TitanFont_DrawTextAlpha(
        x + ((width - label_width) * 0.5f),
        y + 6.0f,
        0.50f,
        label,
        247,
        248,
        250,
        alpha
    );
}

static void TitanTextInput_DrawKeyboard(
    float panel_x,
    float panel_y,
    unsigned char alpha
)
{
    int character_rows;
    int row;
    int column;
    int key_count;
    unsigned int codepoint;
    char label_buffer[5];
    const char *label;
    float key_x;
    float key_y;
    float key_width;
    float key_height;

    character_rows = TitanTextInput_GetCharacterRowCount();

    for (row = 0; row <= character_rows; row++)
    {
        key_count = TitanTextInput_GetKeyCount(row);

        for (column = 0; column < key_count; column++)
        {
            TitanTextInput_GetKeyGeometry(
                row,
                column,
                &key_x,
                &key_y,
                &key_width,
                &key_height
            );

            if (row < character_rows)
            {
                codepoint = TitanTextInput_GetCharacter(row, column);
                TitanTextInput_EncodeCodepoint(codepoint, label_buffer);
                label = label_buffer;
            }
            else
            {
                TitanTextInput_GetFunctionLabel(column, &label);
            }

            TitanTextInput_DrawKey(
                panel_x + key_x,
                panel_y + key_y,
                key_width,
                key_height,
                label,
                g_text_input.selected_row == row &&
                    g_text_input.selected_column == column,
                alpha
            );
        }
    }
}

static void TitanTextInput_DrawSelection(
    float panel_x,
    float panel_y,
    unsigned char alpha
)
{
    float key_x;
    float key_y;
    float key_width;
    float key_height;

    TitanTextInput_GetKeyGeometry(
        g_text_input.selected_row,
        g_text_input.selected_column,
        &key_x,
        &key_y,
        &key_width,
        &key_height
    );

    TitanTextInput_DrawOutline(
        panel_x + key_x - 2.0f,
        panel_y + key_y - 2.0f,
        key_width + 4.0f,
        key_height + 4.0f,
        1.0f,
        73,
        211,
        235,
        alpha / 3u
    );

    TitanTextInput_DrawOutline(
        panel_x + key_x,
        panel_y + key_y,
        key_width,
        key_height,
        1.5f,
        224,
        252,
        255,
        alpha
    );
}

void TitanTextInput_Draw(
    unsigned int frame
)
{
    char display_text[TITAN_TEXT_INPUT_TEXT_CAPACITY];
    char prefix[TITAN_TEXT_INPUT_TEXT_CAPACITY];
    float visibility;
    float cursor_x;
    float field_y;
    int reveal_password;
    int masked;
    unsigned char alpha;
    unsigned char dim_alpha;

    if (g_text_input.state == TITAN_TEXT_INPUT_CLOSED)
    {
        return;
    }

    if (g_text_input.state == TITAN_TEXT_INPUT_OPENING)
    {
        visibility = (float)(
            TitanTime_NowMilliseconds() - g_text_input.transition_started_ms
        ) / (float)TITAN_TEXT_INPUT_OPEN_MS;
    }
    else if (g_text_input.state == TITAN_TEXT_INPUT_CLOSING)
    {
        visibility =
            1.0f -
            (
                (float)(TitanTime_NowMilliseconds() -
                    g_text_input.transition_started_ms) /
                (float)TITAN_TEXT_INPUT_CLOSE_MS
            );
    }
    else
    {
        visibility = 1.0f;
    }

    visibility = TitanAnimation_SmoothStep(
        TitanTextInput_Clamp(visibility, 0.0f, 1.0f)
    );

    alpha = TitanTextInput_Alpha(255.0f * visibility);
    dim_alpha = TitanTextInput_Alpha(86.0f * visibility);
    field_y = g_text_input.panel_y - 39.0f;

    TitanVideo_DrawRectAlpha(
        0.0f,
        0.0f,
        (float)TITAN_UI_WIDTH,
        (float)TITAN_UI_HEIGHT,
        0,
        0,
        0,
        dim_alpha
    );

    TitanFont_DrawTextAlpha(
        g_text_input.panel_x,
        g_text_input.panel_y - 64.0f,
        0.64f,
        g_text_input.title,
        246,
        248,
        252,
        alpha
    );

    TitanVideo_DrawRectAlpha(
        g_text_input.panel_x,
        field_y,
        TITAN_TEXT_INPUT_PANEL_WIDTH,
        31.0f,
        4,
        8,
        14,
        TitanTextInput_Alpha(224.0f * visibility)
    );

    TitanTextInput_DrawOutline(
        g_text_input.panel_x,
        field_y,
        TITAN_TEXT_INPUT_PANEL_WIDTH,
        31.0f,
        1.0f,
        154,
        164,
        178,
        alpha
    );

    if (g_text_input.cache_ready)
    {
        TitanVideo_DrawCachedLayer16(
            g_text_input.panel_x,
            g_text_input.panel_y,
            TITAN_TEXT_INPUT_PANEL_WIDTH,
            TITAN_TEXT_INPUT_PANEL_HEIGHT,
            TITAN_TEXT_INPUT_CACHE_VISIBLE_WIDTH,
            TITAN_TEXT_INPUT_CACHE_VISIBLE_HEIGHT,
            alpha
        );
    }
    else
    {
        TitanVideo_DrawRectAlpha(
            g_text_input.panel_x,
            g_text_input.panel_y,
            TITAN_TEXT_INPUT_PANEL_WIDTH,
            TITAN_TEXT_INPUT_PANEL_HEIGHT,
            8,
            9,
            12,
            alpha
        );

        TitanTextInput_DrawKeyboard(
            g_text_input.panel_x,
            g_text_input.panel_y,
            alpha
        );
    }

    reveal_password = g_text_input.reveal_password;
    masked =
        g_text_input.field_type == TITAN_TEXT_INPUT_FIELD_PASSWORD &&
        !reveal_password;

    TitanTextInput_BuildDisplayText(
        reveal_password,
        display_text,
        sizeof(display_text)
    );

    if (display_text[0] == '\0')
    {
        TitanFont_DrawTextAlpha(
            g_text_input.panel_x + 12.0f,
            field_y + 6.0f,
            0.58f,
            g_text_input.placeholder,
            137,
            144,
            159,
            alpha
        );
    }
    else
    {
        TitanFont_DrawTextAlpha(
            g_text_input.panel_x + 12.0f,
            field_y + 6.0f,
            0.58f,
            display_text,
            245,
            247,
            251,
            alpha
        );
    }

    TitanTextInput_BuildCursorPrefix(
        display_text,
        masked,
        prefix,
        sizeof(prefix)
    );

    cursor_x =
        g_text_input.panel_x + 12.0f +
        TitanFont_MeasureText(0.58f, prefix);

    if (((frame / 24u) & 1u) == 0u)
    {
        TitanVideo_DrawLineAlpha(
            cursor_x,
            field_y + 5.0f,
            cursor_x,
            field_y + 26.0f,
            1.0f,
            222,
            252,
            255,
            alpha
        );
    }

    if (g_text_input.cache_ready)
    {
        TitanTextInput_DrawSelection(
            g_text_input.panel_x,
            g_text_input.panel_y,
            alpha
        );
    }

    if (g_text_input.message[0] != '\0')
    {
        TitanFont_DrawTextAlpha(
            g_text_input.panel_x + 12.0f,
            g_text_input.panel_y + 7.0f,
            0.44f,
            g_text_input.message,
            255,
            114,
            130,
            alpha
        );
    }
}

int TitanTextInput_IsOpen(void)
{
    return g_text_input.state != TITAN_TEXT_INPUT_CLOSED;
}

TitanTextInputState TitanTextInput_GetState(void)
{
    return g_text_input.state;
}

const char *TitanTextInput_GetText(void)
{
    return g_text_input.text;
}

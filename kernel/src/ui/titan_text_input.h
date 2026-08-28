#ifndef PROJECT_TITAN_TEXT_INPUT_H
#define PROJECT_TITAN_TEXT_INPUT_H

#include "titan_input.h"

#define TITAN_TEXT_INPUT_TEXT_CAPACITY 256
#define TITAN_TEXT_INPUT_MESSAGE_CAPACITY 96

typedef enum TitanTextInputState
{
    TITAN_TEXT_INPUT_CLOSED = 0,
    TITAN_TEXT_INPUT_OPENING,
    TITAN_TEXT_INPUT_ACTIVE,
    TITAN_TEXT_INPUT_VALIDATING,
    TITAN_TEXT_INPUT_CLOSING
} TitanTextInputState;

typedef enum TitanTextInputFieldType
{
    TITAN_TEXT_INPUT_FIELD_TEXT = 0,
    TITAN_TEXT_INPUT_FIELD_PASSWORD,
    TITAN_TEXT_INPUT_FIELD_IP_ADDRESS,
    TITAN_TEXT_INPUT_FIELD_PORT,
    TITAN_TEXT_INPUT_FIELD_PATH
} TitanTextInputFieldType;

typedef int (*TitanTextInputValidationCallback)(
    const char *text,
    char *message,
    unsigned int message_capacity,
    void *user_data
);

typedef void (*TitanTextInputConfirmCallback)(
    const char *text,
    void *user_data
);

typedef void (*TitanTextInputCancelCallback)(
    void *user_data
);

typedef struct TitanTextInputRequest
{
    const char *title;
    const char *initial_text;
    const char *placeholder;
    unsigned int maximum_codepoints;
    TitanTextInputFieldType field_type;
    TitanTextInputValidationCallback validation_callback;
    TitanTextInputConfirmCallback confirm_callback;
    TitanTextInputCancelCallback cancel_callback;
    void *user_data;
} TitanTextInputRequest;

void TitanTextInput_Initialize(void);

int TitanTextInput_Open(
    const TitanTextInputRequest *request
);

void TitanTextInput_Update(
    const TitanInputState *input
);

void TitanTextInput_Draw(
    unsigned int frame
);

int TitanTextInput_IsOpen(void);
TitanTextInputState TitanTextInput_GetState(void);
const char *TitanTextInput_GetText(void);

#endif

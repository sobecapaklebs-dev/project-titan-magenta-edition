#ifndef PROJECT_TITAN_AVATAR_PICKER_H
#define PROJECT_TITAN_AVATAR_PICKER_H

#include "titan_input.h"

typedef void (*TitanAvatarPickerConfirmCallback)(
    unsigned int avatar_id,
    void *user_data
);

typedef void (*TitanAvatarPickerCancelCallback)(
    void *user_data
);

typedef struct TitanAvatarPickerRequest
{
    const char *title;
    const char *profile_name;
    unsigned int initial_avatar_id;
    TitanAvatarPickerConfirmCallback confirm_callback;
    TitanAvatarPickerCancelCallback cancel_callback;
    void *user_data;
} TitanAvatarPickerRequest;

void TitanAvatarPicker_Initialize(void);

int TitanAvatarPicker_Open(
    const TitanAvatarPickerRequest *request
);

void TitanAvatarPicker_Update(
    const TitanInputState *input
);

void TitanAvatarPicker_Draw(
    unsigned int frame
);

int TitanAvatarPicker_IsOpen(void);
unsigned int TitanAvatarPicker_GetSelected(void);

#endif

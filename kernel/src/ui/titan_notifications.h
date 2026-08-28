#ifndef PROJECT_TITAN_NOTIFICATIONS_H
#define PROJECT_TITAN_NOTIFICATIONS_H

#include "titan_input.h"

typedef enum TitanNotificationKind
{
    TITAN_NOTIFICATION_INFO = 0,
    TITAN_NOTIFICATION_ACTIVITY,
    TITAN_NOTIFICATION_SUCCESS,
    TITAN_NOTIFICATION_WARNING,
    TITAN_NOTIFICATION_ERROR
} TitanNotificationKind;

typedef enum TitanBackgroundActivityState
{
    TITAN_BACKGROUND_ACTIVITY_IDLE = 0,
    TITAN_BACKGROUND_ACTIVITY_RUNNING,
    TITAN_BACKGROUND_ACTIVITY_COMPLETE,
    TITAN_BACKGROUND_ACTIVITY_ERROR
} TitanBackgroundActivityState;

typedef struct TitanBackgroundActivityStatus
{
    TitanBackgroundActivityState state;
    char title[48];
    char message[112];
    unsigned int progress_percent;
    unsigned int processed;
    unsigned int total;
    unsigned int games_found;
    unsigned int applications_found;
    unsigned int transfer_progress_percent;
    unsigned int transfer_bytes_received;
    unsigned int transfer_bytes_total;
    unsigned int transfer_bytes_per_second;
} TitanBackgroundActivityStatus;

typedef void (*TitanNotificationConfirmationCallback)(
    int confirmed,
    void *user_data
);

void TitanNotifications_Initialize(void);

int TitanNotifications_Push(
    TitanNotificationKind kind,
    const char *title,
    const char *message
);

void TitanNotifications_BeginBackgroundActivity(
    const char *title,
    const char *message
);

void TitanNotifications_UpdateBackgroundActivity(
    const char *message,
    unsigned int progress_percent,
    unsigned int processed,
    unsigned int total,
    unsigned int games_found,
    unsigned int applications_found
);

void TitanNotifications_CompleteBackgroundActivity(
    const char *message,
    unsigned int games_found,
    unsigned int applications_found
);

void TitanNotifications_FailBackgroundActivity(
    const char *message
);

void TitanNotifications_ClearBackgroundActivity(void);

void TitanNotifications_BeginDownloadActivity(
    const char *title,
    const char *message
);

void TitanNotifications_UpdateDownloadActivity(
    const char *message,
    unsigned int progress_percent,
    unsigned int processed,
    unsigned int total,
    unsigned int transfer_progress_percent,
    unsigned int transfer_bytes_received,
    unsigned int transfer_bytes_total,
    unsigned int transfer_bytes_per_second
);

void TitanNotifications_CompleteDownloadActivity(
    const char *message,
    unsigned int processed,
    unsigned int total
);

void TitanNotifications_FailDownloadActivity(
    const char *message,
    unsigned int processed,
    unsigned int total
);

void TitanNotifications_ClearDownloadActivity(void);

void TitanNotifications_DismissAllCards(void);
void TitanNotifications_ClearAll(void);

const TitanBackgroundActivityStatus *TitanNotifications_GetBackgroundActivity(void);
const TitanBackgroundActivityStatus *TitanNotifications_GetDownloadActivity(void);
const char *TitanNotifications_GetHeaderStatus(void);

int TitanNotifications_OpenConfirmation(
    const char *title,
    const char *message_line_1,
    const char *message_line_2,
    const char *cancel_label,
    const char *confirm_label,
    TitanNotificationConfirmationCallback callback,
    void *user_data
);


int TitanNotifications_OpenInformation(
    const char *title,
    const char *line_1,
    const char *line_2,
    const char *line_3,
    const char *line_4,
    const char *line_5
);

int TitanNotifications_Update(
    const TitanInputState *input
);

void TitanNotifications_Draw(
    unsigned int frame
);

int TitanNotifications_IsModalOpen(void);

unsigned int TitanNotifications_GetDroppedCount(void);

#endif

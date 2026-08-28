#include "titan_notifications.h"

#include "titan_audio.h"
#include "titan_system_error.h"

#include "titan_animation.h"
#include "titan_font.h"
#include "titan_video.h"
#include "titan_ui_assets.h"
#include "titan_time.h"

#include <stdio.h>
#include <string.h>

#define TITAN_NOTIFICATION_QUEUE_CAPACITY 4u
#define TITAN_NOTIFICATION_TITLE_CAPACITY 48u
#define TITAN_NOTIFICATION_MESSAGE_CAPACITY 112u
#define TITAN_NOTIFICATION_LABEL_CAPACITY 32u

#define TITAN_NOTIFICATION_ENTER_MS 400u
#define TITAN_NOTIFICATION_HOLD_MS 3000u
#define TITAN_NOTIFICATION_EXIT_MS 500u

#define TITAN_NOTIFICATION_CARD_WIDTH 312.0f
#define TITAN_NOTIFICATION_CARD_HEIGHT 60.0f
#define TITAN_NOTIFICATION_ALERT_SIZE 128u
#define TITAN_NOTIFICATION_ACTIVITY_HEADER_MS 4000u

typedef struct TitanNotificationCard
{
    TitanNotificationKind kind;
    char title[TITAN_NOTIFICATION_TITLE_CAPACITY];
    char message[TITAN_NOTIFICATION_MESSAGE_CAPACITY];
    unsigned int repeat_count;
} TitanNotificationCard;

typedef struct TitanNotificationModal
{
    int open;
    int information;
    int selected_action;
    char title[TITAN_NOTIFICATION_TITLE_CAPACITY];
    char message_line_1[TITAN_NOTIFICATION_MESSAGE_CAPACITY];
    char message_line_2[TITAN_NOTIFICATION_MESSAGE_CAPACITY];
    char message_line_3[TITAN_NOTIFICATION_MESSAGE_CAPACITY];
    char message_line_4[TITAN_NOTIFICATION_MESSAGE_CAPACITY];
    char message_line_5[TITAN_NOTIFICATION_MESSAGE_CAPACITY];
    char cancel_label[TITAN_NOTIFICATION_LABEL_CAPACITY];
    char confirm_label[TITAN_NOTIFICATION_LABEL_CAPACITY];
    TitanNotificationConfirmationCallback callback;
    void *user_data;
} TitanNotificationModal;

typedef struct TitanNotificationManager
{
    int initialized;
    int card_active;
    unsigned long long card_started_ms;
    unsigned int queue_count;
    TitanNotificationCard current;
    TitanNotificationCard queue[TITAN_NOTIFICATION_QUEUE_CAPACITY];
    TitanNotificationModal modal;
    TitanBackgroundActivityStatus activity;
    unsigned long long activity_header_deadline_ms;
    TitanBackgroundActivityStatus download_activity;
    unsigned long long download_header_deadline_ms;
    char download_header[112];
    unsigned int dropped_count;
} TitanNotificationManager;

static TitanNotificationManager g_notifications;

static void TitanNotifications_CopyString(
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

    while (
        index + 1u < capacity &&
        source[index] != '\0'
    )
    {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

static unsigned char TitanNotifications_Alpha(
    float value
)
{
    if (value <= 0.0f)
    {
        return 0u;
    }

    if (value >= 255.0f)
    {
        return 255u;
    }

    return (unsigned char)value;
}

static void TitanNotifications_GetAccent(
    TitanNotificationKind kind,
    unsigned char *red,
    unsigned char *green,
    unsigned char *blue
)
{
    if (kind == TITAN_NOTIFICATION_ACTIVITY)
    {
        *red = 91u;
        *green = 200u;
        *blue = 232u;
    }
    else if (kind == TITAN_NOTIFICATION_SUCCESS)
    {
        *red = 72u;
        *green = 204u;
        *blue = 142u;
    }
    else if (kind == TITAN_NOTIFICATION_WARNING)
    {
        *red = 245u;
        *green = 176u;
        *blue = 65u;
    }
    else if (kind == TITAN_NOTIFICATION_ERROR)
    {
        *red = 223u;
        *green = 46u;
        *blue = 58u;
    }
    else
    {
        *red = 91u;
        *green = 200u;
        *blue = 232u;
    }
}

static int TitanNotifications_Priority(TitanNotificationKind kind)
{
    switch (kind)
    {
        case TITAN_NOTIFICATION_ERROR:
            return 4;
        case TITAN_NOTIFICATION_WARNING:
            return 3;
        case TITAN_NOTIFICATION_SUCCESS:
            return 2;
        case TITAN_NOTIFICATION_INFO:
            return 1;
        case TITAN_NOTIFICATION_ACTIVITY:
        default:
            return 0;
    }
}

static int TitanNotifications_CardMatches(
    const TitanNotificationCard *card,
    TitanNotificationKind kind,
    const char *title,
    const char *message
)
{
    return
        card != 0 &&
        card->kind == kind &&
        strcmp(card->title, title) == 0 &&
        strcmp(card->message, message) == 0;
}

static void TitanNotifications_RemoveQueued(unsigned int index)
{
    unsigned int cursor;

    if (index >= g_notifications.queue_count)
    {
        return;
    }

    for (cursor = index + 1u; cursor < g_notifications.queue_count; ++cursor)
    {
        g_notifications.queue[cursor - 1u] = g_notifications.queue[cursor];
    }

    g_notifications.queue_count--;
}

static void TitanNotifications_StartNextCard(void)
{
    unsigned int index;
    unsigned int best_index;
    int best_priority;

    if (g_notifications.card_active || g_notifications.queue_count == 0u)
    {
        return;
    }

    best_index = 0u;
    best_priority = TitanNotifications_Priority(g_notifications.queue[0].kind);
    for (index = 1u; index < g_notifications.queue_count; ++index)
    {
        int priority = TitanNotifications_Priority(g_notifications.queue[index].kind);
        if (priority > best_priority)
        {
            best_index = index;
            best_priority = priority;
        }
    }

    g_notifications.current = g_notifications.queue[best_index];
    TitanNotifications_RemoveQueued(best_index);
    g_notifications.card_started_ms = TitanTime_NowMilliseconds();
    g_notifications.card_active = 1;
}

void TitanNotifications_Initialize(void)
{
    memset(&g_notifications, 0, sizeof(g_notifications));
    g_notifications.initialized = 1;
}

int TitanNotifications_Push(
    TitanNotificationKind kind,
    const char *title,
    const char *message
)
{
    TitanNotificationCard *card;
    unsigned int index;

    if (!g_notifications.initialized)
    {
        TitanNotifications_Initialize();
    }

    if (title == 0 || message == 0)
    {
        return 0;
    }

    /* BuildFix24 notification policy:
     * - component/system errors are not notification cards; they are promoted
     *   to the dedicated System Error popup.
     * - routine activity/success events already have page/ticker feedback and
     *   should not cover Home with redundant cards.
     * - info and warning remain card-eligible because they may require user
     *   attention without being system faults. */
    if (kind == TITAN_NOTIFICATION_ERROR)
    {
        return TitanSystemError_Raise(title, message);
    }
    if (kind == TITAN_NOTIFICATION_ACTIVITY || kind == TITAN_NOTIFICATION_SUCCESS)
    {
        return 1;
    }

    if (
        g_notifications.card_active &&
        TitanNotifications_CardMatches(
            &g_notifications.current,
            kind,
            title,
            message
        )
    )
    {
        g_notifications.current.repeat_count++;
        g_notifications.card_started_ms = TitanTime_NowMilliseconds();
        return 1;
    }

    for (index = 0u; index < g_notifications.queue_count; ++index)
    {
        if (TitanNotifications_CardMatches(&g_notifications.queue[index], kind, title, message))
        {
            g_notifications.queue[index].repeat_count++;
            return 1;
        }
    }

    if (g_notifications.queue_count >= TITAN_NOTIFICATION_QUEUE_CAPACITY)
    {
        unsigned int lowest_index = 0u;
        int lowest_priority = TitanNotifications_Priority(g_notifications.queue[0].kind);
        int incoming_priority = TitanNotifications_Priority(kind);

        for (index = 1u; index < g_notifications.queue_count; ++index)
        {
            int priority = TitanNotifications_Priority(g_notifications.queue[index].kind);
            if (priority < lowest_priority)
            {
                lowest_priority = priority;
                lowest_index = index;
            }
        }

        if (incoming_priority <= lowest_priority)
        {
            g_notifications.dropped_count++;
            return 0;
        }

        TitanNotifications_RemoveQueued(lowest_index);
        g_notifications.dropped_count++;
    }

    card = &g_notifications.queue[g_notifications.queue_count++];
    memset(card, 0, sizeof(*card));
    card->kind = kind;
    card->repeat_count = 1u;
    TitanNotifications_CopyString(card->title, sizeof(card->title), title);
    TitanNotifications_CopyString(card->message, sizeof(card->message), message);
    TitanNotifications_StartNextCard();

    if (kind != TITAN_NOTIFICATION_ACTIVITY)
    {
        (void)TitanAudio_PlaySfx(TITAN_AUDIO_SFX_NOTIFICATION);
    }

    return 1;
}

void TitanNotifications_BeginBackgroundActivity(
    const char *title,
    const char *message
)
{
    if (!g_notifications.initialized)
    {
        TitanNotifications_Initialize();
    }

    memset(&g_notifications.activity, 0, sizeof(g_notifications.activity));
    g_notifications.activity.state = TITAN_BACKGROUND_ACTIVITY_RUNNING;
    TitanNotifications_CopyString(
        g_notifications.activity.title,
        sizeof(g_notifications.activity.title),
        title != 0 ? title : "Atualizando biblioteca"
    );
    TitanNotifications_CopyString(
        g_notifications.activity.message,
        sizeof(g_notifications.activity.message),
        message != 0 ? message : "Preparando varredura em segundo plano"
    );
    g_notifications.activity_header_deadline_ms = 0u;

    TitanNotifications_Push(
        TITAN_NOTIFICATION_ACTIVITY,
        g_notifications.activity.title,
        "Acompanhe o progresso em Notificacoes"
    );
}

void TitanNotifications_UpdateBackgroundActivity(
    const char *message,
    unsigned int progress_percent,
    unsigned int processed,
    unsigned int total,
    unsigned int games_found,
    unsigned int applications_found
)
{
    if (g_notifications.activity.state != TITAN_BACKGROUND_ACTIVITY_RUNNING)
    {
        return;
    }

    if (progress_percent > 100u)
    {
        progress_percent = 100u;
    }

    if (message != 0)
    {
        TitanNotifications_CopyString(
            g_notifications.activity.message,
            sizeof(g_notifications.activity.message),
            message
        );
    }

    g_notifications.activity.progress_percent = progress_percent;
    g_notifications.activity.processed = processed;
    g_notifications.activity.total = total;
    g_notifications.activity.games_found = games_found;
    g_notifications.activity.applications_found = applications_found;
}

void TitanNotifications_CompleteBackgroundActivity(
    const char *message,
    unsigned int games_found,
    unsigned int applications_found
)
{
    if (!g_notifications.initialized)
    {
        TitanNotifications_Initialize();
    }

    g_notifications.activity.state = TITAN_BACKGROUND_ACTIVITY_COMPLETE;
    g_notifications.activity.progress_percent = 100u;
    g_notifications.activity.games_found = games_found;
    g_notifications.activity.applications_found = applications_found;
    TitanNotifications_CopyString(
        g_notifications.activity.title,
        sizeof(g_notifications.activity.title),
        "Biblioteca atualizada"
    );
    TitanNotifications_CopyString(
        g_notifications.activity.message,
        sizeof(g_notifications.activity.message),
        message != 0 ? message : "Jogos e aplicativos estao prontos"
    );
    g_notifications.activity_header_deadline_ms =
        TitanTime_NowMilliseconds() + TITAN_NOTIFICATION_ACTIVITY_HEADER_MS;

    TitanNotifications_Push(
        TITAN_NOTIFICATION_SUCCESS,
        g_notifications.activity.title,
        g_notifications.activity.message
    );
}

void TitanNotifications_FailBackgroundActivity(
    const char *message
)
{
    if (!g_notifications.initialized)
    {
        TitanNotifications_Initialize();
    }

    g_notifications.activity.state = TITAN_BACKGROUND_ACTIVITY_ERROR;
    TitanNotifications_CopyString(
        g_notifications.activity.title,
        sizeof(g_notifications.activity.title),
        "Atualizacao interrompida"
    );
    TitanNotifications_CopyString(
        g_notifications.activity.message,
        sizeof(g_notifications.activity.message),
        message != 0 ? message : "Nao foi possivel concluir a varredura"
    );
    g_notifications.activity_header_deadline_ms =
        TitanTime_NowMilliseconds() + TITAN_NOTIFICATION_ACTIVITY_HEADER_MS;

    TitanNotifications_Push(
        TITAN_NOTIFICATION_ERROR,
        g_notifications.activity.title,
        g_notifications.activity.message
    );
}

void TitanNotifications_ClearBackgroundActivity(void)
{
    memset(&g_notifications.activity, 0, sizeof(g_notifications.activity));
    g_notifications.activity_header_deadline_ms = 0u;
}


void TitanNotifications_BeginDownloadActivity(const char *title, const char *message)
{
    if (!g_notifications.initialized) TitanNotifications_Initialize();
    memset(&g_notifications.download_activity, 0, sizeof(g_notifications.download_activity));
    g_notifications.download_activity.state = TITAN_BACKGROUND_ACTIVITY_RUNNING;
    TitanNotifications_CopyString(g_notifications.download_activity.title,
        sizeof(g_notifications.download_activity.title), title != 0 ? title : "Assets dos jogos");
    TitanNotifications_CopyString(g_notifications.download_activity.message,
        sizeof(g_notifications.download_activity.message), message != 0 ? message : "Preparando downloads");
    g_notifications.download_header_deadline_ms = 0u;
    TitanNotifications_CopyString(g_notifications.download_header, sizeof(g_notifications.download_header),
        g_notifications.download_activity.message);
    TitanNotifications_Push(TITAN_NOTIFICATION_ACTIVITY, g_notifications.download_activity.title,
        "Acompanhe o progresso em Notificacoes");
}

void TitanNotifications_UpdateDownloadActivity(const char *message, unsigned int progress_percent,
    unsigned int processed, unsigned int total, unsigned int transfer_progress_percent,
    unsigned int transfer_bytes_received, unsigned int transfer_bytes_total,
    unsigned int transfer_bytes_per_second)
{
    char header[112];
    unsigned int speed_kib = (transfer_bytes_per_second + 512u) / 1024u;
    if (g_notifications.download_activity.state != TITAN_BACKGROUND_ACTIVITY_RUNNING) return;
    if (progress_percent > 100u) progress_percent = 100u;
    if (transfer_progress_percent > 100u) transfer_progress_percent = 100u;
    if (message != 0) TitanNotifications_CopyString(g_notifications.download_activity.message,
        sizeof(g_notifications.download_activity.message), message);
    g_notifications.download_activity.progress_percent = progress_percent;
    g_notifications.download_activity.processed = processed;
    g_notifications.download_activity.total = total;
    g_notifications.download_activity.transfer_progress_percent = transfer_progress_percent;
    g_notifications.download_activity.transfer_bytes_received = transfer_bytes_received;
    g_notifications.download_activity.transfer_bytes_total = transfer_bytes_total;
    g_notifications.download_activity.transfer_bytes_per_second = transfer_bytes_per_second;
    if (total > 0u && transfer_bytes_per_second > 0u)
        snprintf(header, sizeof(header), "%s %u/%u - %u KiB/s - %s", g_notifications.download_activity.title, processed, total, speed_kib,
            g_notifications.download_activity.message);
    else if (total > 0u)
        snprintf(header, sizeof(header), "%s %u/%u - %s", g_notifications.download_activity.title, processed, total,
            g_notifications.download_activity.message);
    else snprintf(header, sizeof(header), "%s - %s", g_notifications.download_activity.title, g_notifications.download_activity.message);
    TitanNotifications_CopyString(g_notifications.download_header, sizeof(g_notifications.download_header), header);
}

void TitanNotifications_CompleteDownloadActivity(const char *message, unsigned int processed, unsigned int total)
{
    if (!g_notifications.initialized) TitanNotifications_Initialize();
    g_notifications.download_activity.state = TITAN_BACKGROUND_ACTIVITY_COMPLETE;
    g_notifications.download_activity.progress_percent = 100u;
    g_notifications.download_activity.processed = processed;
    g_notifications.download_activity.total = total;
    TitanNotifications_CopyString(g_notifications.download_activity.title,
        sizeof(g_notifications.download_activity.title), "Assets concluidos");
    TitanNotifications_CopyString(g_notifications.download_activity.message,
        sizeof(g_notifications.download_activity.message), message != 0 ? message : "Dados dos jogos atualizados");
    TitanNotifications_CopyString(g_notifications.download_header, sizeof(g_notifications.download_header),
        "Assets concluidos");
    g_notifications.download_header_deadline_ms = TitanTime_NowMilliseconds() + TITAN_NOTIFICATION_ACTIVITY_HEADER_MS;
    TitanNotifications_Push(TITAN_NOTIFICATION_SUCCESS, g_notifications.download_activity.title,
        g_notifications.download_activity.message);
}

void TitanNotifications_FailDownloadActivity(const char *message, unsigned int processed, unsigned int total)
{
    if (!g_notifications.initialized) TitanNotifications_Initialize();
    g_notifications.download_activity.state = TITAN_BACKGROUND_ACTIVITY_ERROR;
    g_notifications.download_activity.processed = processed;
    g_notifications.download_activity.total = total;
    TitanNotifications_CopyString(g_notifications.download_activity.title,
        sizeof(g_notifications.download_activity.title), "Assets interrompidos");
    TitanNotifications_CopyString(g_notifications.download_activity.message,
        sizeof(g_notifications.download_activity.message), message != 0 ? message : "O download pode ser retomado");
    TitanNotifications_CopyString(g_notifications.download_header, sizeof(g_notifications.download_header),
        "Assets interrompidos - abra Notificacoes");
    g_notifications.download_header_deadline_ms = TitanTime_NowMilliseconds() + TITAN_NOTIFICATION_ACTIVITY_HEADER_MS;
    TitanNotifications_Push(TITAN_NOTIFICATION_WARNING, g_notifications.download_activity.title,
        g_notifications.download_activity.message);
}

void TitanNotifications_ClearDownloadActivity(void)
{
    memset(&g_notifications.download_activity, 0, sizeof(g_notifications.download_activity));
    g_notifications.download_header_deadline_ms = 0u;
    g_notifications.download_header[0] = '\0';
}

void TitanNotifications_DismissAllCards(void)
{
    g_notifications.card_active = 0;
    g_notifications.card_started_ms = 0u;
    g_notifications.queue_count = 0u;
    memset(&g_notifications.current, 0, sizeof(g_notifications.current));
    memset(g_notifications.queue, 0, sizeof(g_notifications.queue));
}

void TitanNotifications_ClearAll(void)
{
    TitanNotifications_DismissAllCards();
    TitanNotifications_ClearBackgroundActivity();
    TitanNotifications_ClearDownloadActivity();
    g_notifications.dropped_count = 0u;
}


const TitanBackgroundActivityStatus *TitanNotifications_GetBackgroundActivity(void)
{
    return &g_notifications.activity;
}

const TitanBackgroundActivityStatus *TitanNotifications_GetDownloadActivity(void)
{
    return &g_notifications.download_activity;
}

const char *TitanNotifications_GetHeaderStatus(void)
{
    if (g_notifications.download_activity.state == TITAN_BACKGROUND_ACTIVITY_RUNNING)
        return g_notifications.download_header[0] != '\0' ? g_notifications.download_header : "Baixando assets dos jogos";
    if (TitanTime_NowMilliseconds() < g_notifications.download_header_deadline_ms &&
        (g_notifications.download_activity.state == TITAN_BACKGROUND_ACTIVITY_COMPLETE ||
         g_notifications.download_activity.state == TITAN_BACKGROUND_ACTIVITY_ERROR))
        return g_notifications.download_header;

    if (g_notifications.activity.state == TITAN_BACKGROUND_ACTIVITY_RUNNING)
    {
        return "Atualizando biblioteca em segundo plano - abra Notificacoes";
    }

    if (
        TitanTime_NowMilliseconds() < g_notifications.activity_header_deadline_ms &&
        g_notifications.activity.state == TITAN_BACKGROUND_ACTIVITY_COMPLETE
    )
    {
        return "Biblioteca atualizada";
    }

    if (
        TitanTime_NowMilliseconds() < g_notifications.activity_header_deadline_ms &&
        g_notifications.activity.state == TITAN_BACKGROUND_ACTIVITY_ERROR
    )
    {
        return "Atualizacao interrompida";
    }

    return "";
}

int TitanNotifications_OpenConfirmation(
    const char *title,
    const char *message_line_1,
    const char *message_line_2,
    const char *cancel_label,
    const char *confirm_label,
    TitanNotificationConfirmationCallback callback,
    void *user_data
)
{
    TitanNotificationModal *modal;

    if (!g_notifications.initialized)
    {
        TitanNotifications_Initialize();
    }

    if (
        title == 0 ||
        message_line_1 == 0 ||
        cancel_label == 0 ||
        confirm_label == 0 ||
        callback == 0 ||
        g_notifications.modal.open
    )
    {
        return 0;
    }

    modal = &g_notifications.modal;
    memset(modal, 0, sizeof(*modal));
    modal->open = 1;
    modal->selected_action = 0;
    modal->callback = callback;
    modal->user_data = user_data;
    TitanNotifications_CopyString(
        modal->title,
        sizeof(modal->title),
        title
    );
    TitanNotifications_CopyString(
        modal->message_line_1,
        sizeof(modal->message_line_1),
        message_line_1
    );
    TitanNotifications_CopyString(
        modal->message_line_2,
        sizeof(modal->message_line_2),
        message_line_2
    );
    TitanNotifications_CopyString(
        modal->cancel_label,
        sizeof(modal->cancel_label),
        cancel_label
    );
    TitanNotifications_CopyString(
        modal->confirm_label,
        sizeof(modal->confirm_label),
        confirm_label
    );

    return 1;
}

int TitanNotifications_OpenInformation(
    const char *title,
    const char *line_1,
    const char *line_2,
    const char *line_3,
    const char *line_4,
    const char *line_5
)
{
    TitanNotificationModal *modal;
    if (!g_notifications.initialized) TitanNotifications_Initialize();
    if (!title || !line_1 || g_notifications.modal.open) return 0;
    modal=&g_notifications.modal;
    memset(modal,0,sizeof(*modal));
    modal->open=1;
    modal->information=1;
    TitanNotifications_CopyString(modal->title,sizeof(modal->title),title);
    TitanNotifications_CopyString(modal->message_line_1,sizeof(modal->message_line_1),line_1);
    TitanNotifications_CopyString(modal->message_line_2,sizeof(modal->message_line_2),line_2);
    TitanNotifications_CopyString(modal->message_line_3,sizeof(modal->message_line_3),line_3);
    TitanNotifications_CopyString(modal->message_line_4,sizeof(modal->message_line_4),line_4);
    TitanNotifications_CopyString(modal->message_line_5,sizeof(modal->message_line_5),line_5);
    TitanNotifications_CopyString(modal->cancel_label,sizeof(modal->cancel_label),"Fechar");
    return 1;
}

static void TitanNotifications_CloseModal(
    int confirmed
)
{
    TitanNotificationConfirmationCallback callback;
    void *user_data;

    callback = g_notifications.modal.callback;
    user_data = g_notifications.modal.user_data;
    memset(&g_notifications.modal, 0, sizeof(g_notifications.modal));

    if (callback != 0)
    {
        callback(confirmed, user_data);
    }
}

int TitanNotifications_Update(
    const TitanInputState *input
)
{
    int modal_was_open;

    if (!g_notifications.initialized)
    {
        return 0;
    }

    if (g_notifications.card_active)
    {
        const unsigned long long total_ms =
            TITAN_NOTIFICATION_ENTER_MS +
            TITAN_NOTIFICATION_HOLD_MS +
            TITAN_NOTIFICATION_EXIT_MS;
        const unsigned long long now_ms = TitanTime_NowMilliseconds();

        if (now_ms - g_notifications.card_started_ms >= total_ms)
        {
            g_notifications.card_active = 0;
            TitanNotifications_StartNextCard();
        }
    }
    else
    {
        TitanNotifications_StartNextCard();
    }

    modal_was_open = g_notifications.modal.open;

    if (
        modal_was_open &&
        input != 0 &&
        input->connected
    )
    {
        if (
            !g_notifications.modal.information &&
            (input->pressed &
            (TITAN_INPUT_BUTTON_LEFT | TITAN_INPUT_BUTTON_RIGHT))
        )
        {
            g_notifications.modal.selected_action =
                1 - g_notifications.modal.selected_action;
            (void)TitanAudio_PlaySfx(
                TITAN_AUDIO_SFX_INTERFACE_MENU_NAV
            );
        }

        if (input->pressed & TITAN_INPUT_BUTTON_CIRCLE)
        {
            TitanNotifications_CloseModal(0);
        }
        else if (input->pressed & TITAN_INPUT_BUTTON_CROSS)
        {
            if (g_notifications.modal.information)
                TitanNotifications_CloseModal(0);
            else
                TitanNotifications_CloseModal(g_notifications.modal.selected_action == 1);
        }
    }

    return modal_was_open;
}

static void TitanNotifications_DrawAlertIcon(
    float center_x,
    float center_y,
    float logical_size,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    if (
        alpha == 0u ||
        logical_size <= 0.0f
    )
    {
        return;
    }

    TitanUiAssets_DrawAlert(
        center_x,
        center_y,
        logical_size,
        logical_size,
        red,
        green,
        blue,
        alpha
    );
}

static void TitanNotifications_DrawStatusIcon(
    float x,
    float y,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    unsigned char alpha
)
{
    TitanVideo_DrawCircleAlpha(
        x + 12.0f,
        y + 12.0f,
        11.0f,
        red,
        green,
        blue,
        alpha
    );
    TitanVideo_DrawRectAlpha(
        x + 10.5f,
        y + 5.0f,
        3.0f,
        10.0f,
        17u,
        20u,
        26u,
        alpha
    );
    TitanVideo_DrawRectAlpha(
        x + 10.5f,
        y + 18.0f,
        3.0f,
        3.0f,
        17u,
        20u,
        26u,
        alpha
    );
}

static void TitanNotifications_DrawCard(void)
{
    float x;
    float progress;
    float exit_progress;
    unsigned long long card_age_ms;
    unsigned char alpha;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    const float target_x = 30.0f;
    const float y = 20.0f;
    const float width = TITAN_NOTIFICATION_CARD_WIDTH;
    const float height = TITAN_NOTIFICATION_CARD_HEIGHT;

    if (!g_notifications.card_active)
    {
        return;
    }

    progress = 1.0f;
    exit_progress = 0.0f;
    card_age_ms = TitanTime_NowMilliseconds() - g_notifications.card_started_ms;

    if (card_age_ms < TITAN_NOTIFICATION_ENTER_MS)
    {
        progress = TitanAnimation_EaseOutCubic(
            (float)card_age_ms /
            (float)TITAN_NOTIFICATION_ENTER_MS
        );
    }
    else if (
        card_age_ms >
        TITAN_NOTIFICATION_ENTER_MS +
        TITAN_NOTIFICATION_HOLD_MS
    )
    {
        exit_progress = TitanAnimation_SmoothStep(
            (float)(
                card_age_ms -
                TITAN_NOTIFICATION_ENTER_MS -
                TITAN_NOTIFICATION_HOLD_MS
            ) /
            (float)TITAN_NOTIFICATION_EXIT_MS
        );
    }

    x =
        (-width - 8.0f) +
        ((target_x + width + 8.0f) * progress) -
        (24.0f * exit_progress);
    alpha = TitanNotifications_Alpha(
        255.0f * progress * (1.0f - exit_progress)
    );

    TitanNotifications_GetAccent(
        g_notifications.current.kind,
        &red,
        &green,
        &blue
    );

    TitanVideo_DrawRectAlpha(
        x + 4.0f,
        y + 4.0f,
        width,
        height,
        0u,
        0u,
        0u,
        TitanNotifications_Alpha((float)alpha * 0.20f)
    );
    TitanVideo_DrawRectAlpha(
        x,
        y,
        width,
        height,
        246u,
        247u,
        250u,
        TitanNotifications_Alpha((float)alpha * 0.98f)
    );
    TitanVideo_DrawRectAlpha(
        x,
        y,
        3.0f,
        height,
        red,
        green,
        blue,
        alpha
    );
    if (
        (
            g_notifications.current.kind == TITAN_NOTIFICATION_ACTIVITY ||
            g_notifications.current.kind == TITAN_NOTIFICATION_WARNING
        )
    )
    {
        TitanNotifications_DrawAlertIcon(
            x + 26.0f,
            y + (height * 0.5f),
            34.0f,
            26u,
            30u,
            38u,
            alpha
        );
    }
    else
    {
        TitanNotifications_DrawStatusIcon(
            x + 14.0f,
            y + 14.0f,
            red,
            green,
            blue,
            alpha
        );
    }

    TitanFont_DrawTextAlpha(
        x + 56.0f,
        y + 9.0f,
        0.60f,
        g_notifications.current.title,
        18u,
        21u,
        28u,
        alpha
    );
    TitanFont_DrawTextAlpha(
        x + 56.0f,
        y + 31.0f,
        0.46f,
        g_notifications.current.message,
        62u,
        68u,
        80u,
        alpha
    );
}

static void TitanNotifications_DrawButton(
    float x,
    float y,
    float width,
    const char *label,
    int selected
)
{
    float label_width;
    unsigned char red;
    unsigned char green;
    unsigned char blue;

    red = selected ? 210u : 66u;
    green = selected ? 32u : 71u;
    blue = selected ? 47u : 82u;

    TitanVideo_DrawRectAlpha(
        x,
        y,
        width,
        34.0f,
        selected ? 61u : 30u,
        selected ? 18u : 33u,
        selected ? 25u : 40u,
        244u
    );
    TitanVideo_DrawLineAlpha(x, y, x + width, y, 1.5f, red, green, blue, 255u);
    TitanVideo_DrawLineAlpha(x, y + 34.0f, x + width, y + 34.0f, 1.5f, red, green, blue, 255u);
    TitanVideo_DrawLineAlpha(x, y, x, y + 34.0f, 1.5f, red, green, blue, 255u);
    TitanVideo_DrawLineAlpha(x + width, y, x + width, y + 34.0f, 1.5f, red, green, blue, 255u);

    label_width = TitanFont_MeasureText(0.47f, label);
    TitanFont_DrawTextAlpha(
        x + ((width - label_width) * 0.5f),
        y + 10.0f,
        0.47f,
        label,
        244u,
        245u,
        248u,
        255u
    );
}

static void TitanNotifications_DrawModal(void)
{
    const float panel_x = 164.0f;
    const float panel_y = g_notifications.modal.information ? 98.0f : 132.0f;
    const float panel_width = 526.0f;
    const float panel_height = g_notifications.modal.information ? 252.0f : 184.0f;
    unsigned char accent_r = g_notifications.modal.information ? 70u : 210u;
    unsigned char accent_g = g_notifications.modal.information ? 132u : 32u;
    unsigned char accent_b = g_notifications.modal.information ? 220u : 47u;

    if (!g_notifications.modal.open) return;

    TitanVideo_DrawRectAlpha(0.0f,0.0f,(float)TITAN_UI_WIDTH,(float)TITAN_UI_HEIGHT,0u,0u,0u,154u);
    TitanVideo_DrawRectAlpha(panel_x+5.0f,panel_y+5.0f,panel_width,panel_height,0u,0u,0u,90u);
    TitanVideo_DrawRectAlpha(panel_x,panel_y,panel_width,panel_height,19u,22u,29u,250u);
    TitanVideo_DrawRectAlpha(panel_x,panel_y,5.0f,panel_height,accent_r,accent_g,accent_b,255u);

    if (g_notifications.modal.information) {
        TitanFont_DrawTextAlpha(panel_x+28.0f,panel_y+21.0f,0.45f,"i",accent_r,accent_g,accent_b,255u);
        TitanFont_DrawTextAlpha(panel_x+52.0f,panel_y+20.0f,0.70f,g_notifications.modal.title,248u,249u,252u,255u);
        TitanFont_DrawTextAlpha(panel_x+28.0f,panel_y+62.0f,0.46f,g_notifications.modal.message_line_1,218u,223u,232u,255u);
        TitanFont_DrawTextAlpha(panel_x+28.0f,panel_y+86.0f,0.46f,g_notifications.modal.message_line_2,190u,196u,208u,255u);
        TitanFont_DrawTextAlpha(panel_x+28.0f,panel_y+110.0f,0.46f,g_notifications.modal.message_line_3,190u,196u,208u,255u);
        TitanFont_DrawTextAlpha(panel_x+28.0f,panel_y+134.0f,0.46f,g_notifications.modal.message_line_4,190u,196u,208u,255u);
        TitanFont_DrawTextAlpha(panel_x+28.0f,panel_y+158.0f,0.46f,g_notifications.modal.message_line_5,190u,196u,208u,255u);
        /* Information dialogs deliberately avoid the confirmation/error button
         * renderer because its selected state is red. Keep the same geometry
         * but use the neutral information accent instead. */
        TitanVideo_DrawRectAlpha(panel_x+28.0f,panel_y+198.0f,156.0f,34.0f,30u,38u,54u,244u);
        TitanVideo_DrawLineAlpha(panel_x+28.0f,panel_y+198.0f,panel_x+184.0f,panel_y+198.0f,1.5f,accent_r,accent_g,accent_b,255u);
        TitanVideo_DrawLineAlpha(panel_x+28.0f,panel_y+232.0f,panel_x+184.0f,panel_y+232.0f,1.5f,accent_r,accent_g,accent_b,255u);
        TitanVideo_DrawLineAlpha(panel_x+28.0f,panel_y+198.0f,panel_x+28.0f,panel_y+232.0f,1.5f,accent_r,accent_g,accent_b,255u);
        TitanVideo_DrawLineAlpha(panel_x+184.0f,panel_y+198.0f,panel_x+184.0f,panel_y+232.0f,1.5f,accent_r,accent_g,accent_b,255u);
        {
            float info_label_width = TitanFont_MeasureText(0.47f, g_notifications.modal.cancel_label);
            TitanFont_DrawTextAlpha(panel_x+28.0f+((156.0f-info_label_width)*0.5f),panel_y+208.0f,0.47f,g_notifications.modal.cancel_label,244u,245u,248u,255u);
        }
    } else {
        TitanFont_DrawTextAlpha(panel_x+28.0f,panel_y+24.0f,0.72f,g_notifications.modal.title,248u,249u,252u,255u);
        TitanFont_DrawTextAlpha(panel_x+28.0f,panel_y+67.0f,0.48f,g_notifications.modal.message_line_1,190u,196u,208u,255u);
        TitanFont_DrawTextAlpha(panel_x+28.0f,panel_y+88.0f,0.48f,g_notifications.modal.message_line_2,190u,196u,208u,255u);
        TitanNotifications_DrawButton(panel_x+28.0f,panel_y+128.0f,156.0f,g_notifications.modal.cancel_label,g_notifications.modal.selected_action==0);
        TitanNotifications_DrawButton(panel_x+198.0f,panel_y+128.0f,272.0f,g_notifications.modal.confirm_label,g_notifications.modal.selected_action==1);
    }
}

void TitanNotifications_Draw(
    unsigned int frame
)
{
    (void)frame;

    if (!g_notifications.initialized)
    {
        return;
    }

    TitanVideo_ResetTransform();
    TitanNotifications_DrawModal();
    TitanNotifications_DrawCard();
}

unsigned int TitanNotifications_GetDroppedCount(void)
{
    return g_notifications.dropped_count;
}

int TitanNotifications_IsModalOpen(void)
{
    return g_notifications.modal.open;
}

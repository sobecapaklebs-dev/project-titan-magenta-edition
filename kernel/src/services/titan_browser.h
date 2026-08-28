#ifndef PROJECT_TITAN_BROWSER_H
#define PROJECT_TITAN_BROWSER_H

#include <stddef.h>
#include <stdint.h>

#define TITAN_BROWSER_URL_CAPACITY 768u
#define TITAN_BROWSER_TITLE_CAPACITY 96u
#define TITAN_BROWSER_STATUS_CAPACITY 160u
#define TITAN_BROWSER_LINE_CAPACITY 112u
#define TITAN_BROWSER_LINE_COUNT 30u
#define TITAN_BROWSER_LINK_CAPACITY 24u

typedef enum TitanBrowserState
{
    TITAN_BROWSER_HOME = 0,
    TITAN_BROWSER_LOADING,
    TITAN_BROWSER_READY,
    TITAN_BROWSER_DOWNLOADING,
    TITAN_BROWSER_ERROR
} TitanBrowserState;

typedef enum TitanBrowserAction
{
    TITAN_BROWSER_ACTION_NONE = 0,
    TITAN_BROWSER_ACTION_OPEN_ADDRESS,
    TITAN_BROWSER_ACTION_PLAY_STREAM,
    TITAN_BROWSER_ACTION_DOWNLOAD_STARTED
} TitanBrowserAction;

typedef enum TitanBrowserPlayerResult
{
    TITAN_BROWSER_PLAYER_OK = 0,
    TITAN_BROWSER_PLAYER_NO_STREAM,
    TITAN_BROWSER_PLAYER_MISSING,
    TITAN_BROWSER_PLAYER_INVALID_ELF,
    TITAN_BROWSER_PLAYER_HANDOFF_FAILED
} TitanBrowserPlayerResult;

typedef struct TitanBrowserLink
{
    char label[80];
    char url[TITAN_BROWSER_URL_CAPACITY];
} TitanBrowserLink;

typedef struct TitanBrowserSnapshot
{
    TitanBrowserState state;
    const char *title;
    const char *url;
    const char *status;
    unsigned int line_count;
    unsigned int link_count;
    unsigned int selected_link;
    unsigned int scroll_line;
    uint64_t bytes_received;
    uint64_t content_length;
    int can_go_back;
    int can_go_forward;
} TitanBrowserSnapshot;

void TitanBrowser_Initialize(void);
void TitanBrowser_Update(void);
void TitanBrowser_Shutdown(void);

int TitanBrowser_Navigate(const char *url);
int TitanBrowser_GoBack(void);
int TitanBrowser_GoForward(void);
void TitanBrowser_GoHome(void);
void TitanBrowser_Cancel(void);

void TitanBrowser_MoveLink(int delta);
void TitanBrowser_Scroll(int delta);
TitanBrowserAction TitanBrowser_ActivateSelected(void);
TitanBrowserAction TitanBrowser_DownloadSelected(void);

const TitanBrowserSnapshot *TitanBrowser_GetSnapshot(void);
const char *TitanBrowser_GetLine(unsigned int index);
const TitanBrowserLink *TitanBrowser_GetLink(unsigned int index);
const char *TitanBrowser_GetPendingMediaUrl(void);
TitanBrowserPlayerResult TitanBrowser_LaunchPendingMedia(void);
const char *TitanBrowser_GetPlayerResultName(TitanBrowserPlayerResult result);

int TitanBrowser_SetBridgeBase(const char *base_url);
const char *TitanBrowser_GetBridgeBase(void);

/* Pure helpers are exported for deterministic host tests. */
int TitanBrowser_NormalizeUrl(
    const char *input,
    char *output,
    size_t output_capacity
);
int TitanBrowser_BuildTransportUrl(
    const char *normalized_url,
    char *output,
    size_t output_capacity
);
int TitanBrowser_LoadDocumentForTest(
    const char *url,
    const char *html,
    size_t html_size
);

#endif

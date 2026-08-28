#ifndef PROJECT_TITAN_BROWSER_NET_H
#define PROJECT_TITAN_BROWSER_NET_H

#include <stddef.h>
#include <stdint.h>

#define TITAN_BROWSER_NET_PAGE_CAPACITY (64u * 1024u)
#define TITAN_BROWSER_NET_TYPE_CAPACITY 64u
#define TITAN_BROWSER_NET_ERROR_CAPACITY 128u

typedef enum TitanBrowserNetState
{
    TITAN_BROWSER_NET_IDLE = 0,
    TITAN_BROWSER_NET_QUEUED,
    TITAN_BROWSER_NET_CONNECTING,
    TITAN_BROWSER_NET_RECEIVING,
    TITAN_BROWSER_NET_COMPLETE,
    TITAN_BROWSER_NET_FAILED,
    TITAN_BROWSER_NET_CANCELLED
} TitanBrowserNetState;

typedef struct TitanBrowserNetSnapshot
{
    TitanBrowserNetState state;
    int http_status;
    uint64_t bytes_received;
    uint64_t content_length;
    const char *content_type;
    const char *error;
} TitanBrowserNetSnapshot;

void TitanBrowserNet_Initialize(void);
void TitanBrowserNet_Shutdown(void);

int TitanBrowserNet_BeginPage(const char *url);
int TitanBrowserNet_BeginDownload(const char *url, const char *destination);
void TitanBrowserNet_Cancel(void);

const TitanBrowserNetSnapshot *TitanBrowserNet_GetSnapshot(void);
const unsigned char *TitanBrowserNet_GetPageData(void);
size_t TitanBrowserNet_GetPageSize(void);
void TitanBrowserNet_ReleaseResult(void);

#endif

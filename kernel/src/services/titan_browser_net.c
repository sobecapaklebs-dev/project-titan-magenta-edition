#include "titan_browser_net.h"

#include "titan_network_client.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"

#include <stdio.h>
#include <string.h>

#if defined(__EE__) || defined(_EE)
#include <delaythread.h>
#include <kernel.h>
#endif

#define TITAN_BROWSER_NET_URL_CAPACITY 768u
#define TITAN_BROWSER_NET_PATH_CAPACITY 256u
#define TITAN_BROWSER_NET_STACK_SIZE (24u * 1024u)
#define TITAN_BROWSER_NET_DEADLINE_MS 60000u

typedef enum TitanBrowserNetMode
{
    TITAN_BROWSER_NET_MODE_PAGE = 0,
    TITAN_BROWSER_NET_MODE_DOWNLOAD
} TitanBrowserNetMode;

static volatile TitanBrowserNetState g_net_state;
static volatile int g_net_cancel;
static TitanBrowserNetMode g_net_mode;
static char g_net_url[TITAN_BROWSER_NET_URL_CAPACITY];
static char g_net_destination[TITAN_BROWSER_NET_PATH_CAPACITY];
static unsigned char g_net_page[TITAN_BROWSER_NET_PAGE_CAPACITY];
static size_t g_net_page_size;
static TitanBrowserNetSnapshot g_net_snapshot;
static char g_net_content_type[TITAN_BROWSER_NET_TYPE_CAPACITY];
static char g_net_error[TITAN_BROWSER_NET_ERROR_CAPACITY];

#if defined(__EE__) || defined(_EE)
extern void *_gp;
static int g_net_thread_id = -1;
static unsigned char g_net_thread_stack[TITAN_BROWSER_NET_STACK_SIZE]
    __attribute__((aligned(16)));
#endif

static void TitanBrowserNet_CopyString(char *destination, size_t capacity, const char *source)
{
    size_t index = 0u;
    if (destination == 0 || capacity == 0u) return;
    while (source != 0 && source[index] != '\0' && index + 1u < capacity)
    {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void TitanBrowserNet_SetError(const char *message)
{
    TitanBrowserNet_CopyString(g_net_error, sizeof(g_net_error), message);
    g_net_snapshot.error = g_net_error;
    g_net_state = TITAN_BROWSER_NET_FAILED;
    g_net_snapshot.state = TITAN_BROWSER_NET_FAILED;
}

#if defined(__EE__) || defined(_EE)
static int TitanBrowserNet_ShouldCancel(void *user_data)
{
    (void)user_data;
    return g_net_cancel != 0;
}

static void TitanBrowserNet_Progress(void *user_data, unsigned int received, unsigned int total)
{
    (void)user_data;
    g_net_snapshot.bytes_received = (uint64_t)received;
    if (total != 0u) g_net_snapshot.content_length = (uint64_t)total;
    if (g_net_state == TITAN_BROWSER_NET_CONNECTING || g_net_state == TITAN_BROWSER_NET_QUEUED)
    {
        g_net_state = TITAN_BROWSER_NET_RECEIVING;
        g_net_snapshot.state = TITAN_BROWSER_NET_RECEIVING;
    }
}

static const char *TitanBrowserNet_FailureText(TitanHttpFailureReason reason)
{
    switch (reason)
    {
        case TITAN_HTTP_FAILURE_CANCELLED: return "Requisicao cancelada";
        case TITAN_HTTP_FAILURE_TIMEOUT: return "Tempo limite da requisicao excedido";
        case TITAN_HTTP_FAILURE_DNS: return "Falha ao resolver o servidor";
        case TITAN_HTTP_FAILURE_CONNECT: return "Falha ao conectar ao servidor";
        case TITAN_HTTP_FAILURE_TLS: return "Falha na conexao TLS";
        case TITAN_HTTP_FAILURE_TOO_LARGE: return "Resposta maior que o limite seguro do Browser";
        case TITAN_HTTP_FAILURE_TRUNCATED: return "Resposta de rede truncada";
        case TITAN_HTTP_FAILURE_PROTOCOL: return "Resposta HTTP invalida";
        default: return "Falha no Titan Network Client";
    }
}

static int TitanBrowserNet_PublishDownload(const unsigned char *data, unsigned int size)
{
    char temporary_path[320];
    int descriptor;

    (void)TitanStorageIo_CreateDirectory(TITAN_STORAGE_DEVICE_ROOT);
    (void)TitanStorageIo_CreateDirectory(TITAN_STORAGE_DEVICE_ROOT "/downloads");
    if (snprintf(temporary_path, sizeof(temporary_path), "%s.part", g_net_destination) < 0)
        return 0;
    (void)TitanStorageIo_Remove(temporary_path);
    descriptor = TitanStorageIo_Open(temporary_path,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE);
    if (descriptor < 0) return 0;
    if (!TitanStorageIo_WriteExact(descriptor, data, size) ||
        TitanStorageIo_Flush(descriptor) < 0 || TitanStorageIo_Close(descriptor) < 0)
    {
        (void)TitanStorageIo_Close(descriptor);
        (void)TitanStorageIo_Remove(temporary_path);
        return 0;
    }
    if (!TitanStorageIo_AtomicReplace(temporary_path, g_net_destination, 0))
    {
        (void)TitanStorageIo_Remove(temporary_path);
        return 0;
    }
    return 1;
}

#endif

#if defined(__EE__) || defined(_EE)
static void TitanBrowserNet_RunRequest(void)
{
    TitanHttpClientOptions options;
    TitanHttpResultInfo result_info;
    unsigned int size = 0u;
    int status = 0;
    int ok;

    memset(&options, 0, sizeof(options));
    memset(&result_info, 0, sizeof(result_info));
    g_net_page_size = 0u;
    g_net_page[0] = '\0';
    g_net_content_type[0] = '\0';
    g_net_error[0] = '\0';
    g_net_snapshot.http_status = 0;
    g_net_snapshot.bytes_received = 0u;
    g_net_snapshot.content_length = 0u;
    g_net_snapshot.content_type = g_net_content_type;
    g_net_snapshot.error = g_net_error;

    options.should_cancel = TitanBrowserNet_ShouldCancel;
    options.progress = TitanBrowserNet_Progress;
    options.user_data = 0;
    options.yield_microseconds = 1000u;
    options.maximum_redirects = 3u;
    options.deadline_milliseconds = TITAN_BROWSER_NET_DEADLINE_MS;

    g_net_state = TITAN_BROWSER_NET_CONNECTING;
    g_net_snapshot.state = TITAN_BROWSER_NET_CONNECTING;
    ok = TitanNetworkClient_GetEx(
        TITAN_NETWORK_CLIENT_OWNER_BROWSER,
        g_net_url,
        g_net_page,
        TITAN_BROWSER_NET_PAGE_CAPACITY - 1u,
        &size,
        &status,
        &options,
        &result_info
    );

    g_net_snapshot.http_status = status;
    g_net_snapshot.bytes_received = result_info.bytes_received != 0u
        ? (uint64_t)result_info.bytes_received : (uint64_t)size;
    g_net_snapshot.content_length = (uint64_t)result_info.content_length;

    if (!ok)
    {
        if (result_info.failure_reason == TITAN_HTTP_FAILURE_CANCELLED && g_net_cancel)
        {
            g_net_state = TITAN_BROWSER_NET_CANCELLED;
            g_net_snapshot.state = TITAN_BROWSER_NET_CANCELLED;
            return;
        }
        TitanBrowserNet_SetError(TitanBrowserNet_FailureText(result_info.failure_reason));
        return;
    }

    if (status < 200 || status >= 300)
    {
        snprintf(g_net_error, sizeof(g_net_error), "Servidor retornou HTTP %d", status);
        g_net_state = TITAN_BROWSER_NET_FAILED;
        g_net_snapshot.state = TITAN_BROWSER_NET_FAILED;
        return;
    }

    if (size < TITAN_BROWSER_NET_PAGE_CAPACITY) g_net_page[size] = '\0';
    g_net_page_size = (size_t)size;
    if (g_net_mode == TITAN_BROWSER_NET_MODE_DOWNLOAD)
    {
        /* Browser downloads are still UI-disabled in this internal build. This
         * path nevertheless remains transactional for bounded responses and is
         * intentionally routed through Titan Storage rather than FileXio. */
        if (!TitanBrowserNet_PublishDownload(g_net_page, size))
        {
            TitanBrowserNet_SetError("Falha ao publicar o download concluido");
            return;
        }
    }

    g_net_state = TITAN_BROWSER_NET_COMPLETE;
    g_net_snapshot.state = TITAN_BROWSER_NET_COMPLETE;
}

static void TitanBrowserNet_Worker(void *argument)
{
    (void)argument;
    for (;;)
    {
        if (g_net_state == TITAN_BROWSER_NET_QUEUED) TitanBrowserNet_RunRequest();
        DelayThread(10000);
    }
}

static int TitanBrowserNet_EnsureWorker(void)
{
    ee_thread_t thread;
    if (g_net_thread_id >= 0) return 1;
    memset(&thread, 0, sizeof(thread));
    thread.func = TitanBrowserNet_Worker;
    thread.stack = g_net_thread_stack;
    thread.stack_size = sizeof(g_net_thread_stack);
    thread.gp_reg = &_gp;
    thread.initial_priority = 64;
    g_net_thread_id = CreateThread(&thread);
    if (g_net_thread_id < 0) return 0;
    if (StartThread(g_net_thread_id, 0) < 0)
    {
        DeleteThread(g_net_thread_id);
        g_net_thread_id = -1;
        return 0;
    }
    return 1;
}
#endif

void TitanBrowserNet_Initialize(void)
{
    memset(&g_net_snapshot, 0, sizeof(g_net_snapshot));
    g_net_state = TITAN_BROWSER_NET_IDLE;
    g_net_snapshot.state = TITAN_BROWSER_NET_IDLE;
    g_net_snapshot.content_type = g_net_content_type;
    g_net_snapshot.error = g_net_error;
    g_net_cancel = 0;
    g_net_page_size = 0u;
    /* Worker creation is lazy: simply having Internet installed does not add
     * another EE thread to boot. The first real HTTP request owns that cost. */
}

void TitanBrowserNet_Shutdown(void)
{
    TitanBrowserNet_Cancel();
}

static int TitanBrowserNet_Begin(const char *url, TitanBrowserNetMode mode, const char *destination)
{
    if (url == 0 || url[0] == '\0' || strlen(url) >= sizeof(g_net_url) ||
        (g_net_state != TITAN_BROWSER_NET_IDLE && g_net_state != TITAN_BROWSER_NET_COMPLETE &&
         g_net_state != TITAN_BROWSER_NET_FAILED && g_net_state != TITAN_BROWSER_NET_CANCELLED))
        return 0;
    if (mode == TITAN_BROWSER_NET_MODE_DOWNLOAD &&
        (destination == 0 || destination[0] == '\0' || strlen(destination) >= sizeof(g_net_destination)))
        return 0;
#if defined(__EE__) || defined(_EE)
    if (!TitanBrowserNet_EnsureWorker())
    {
        TitanBrowserNet_SetError("Nao foi possivel iniciar o worker HTTP");
        return 0;
    }
#else
    TitanBrowserNet_SetError("Titan Network Client indisponivel no build host");
    return 0;
#endif
    TitanBrowserNet_CopyString(g_net_url, sizeof(g_net_url), url);
    TitanBrowserNet_CopyString(g_net_destination, sizeof(g_net_destination), destination);
    g_net_mode = mode;
    g_net_cancel = 0;
    g_net_snapshot.http_status = 0;
    g_net_snapshot.bytes_received = 0u;
    g_net_snapshot.content_length = 0u;
    g_net_content_type[0] = '\0';
    g_net_error[0] = '\0';
    g_net_state = TITAN_BROWSER_NET_QUEUED;
    g_net_snapshot.state = TITAN_BROWSER_NET_QUEUED;
    return 1;
}

int TitanBrowserNet_BeginPage(const char *url)
{
    return TitanBrowserNet_Begin(url, TITAN_BROWSER_NET_MODE_PAGE, 0);
}

int TitanBrowserNet_BeginDownload(const char *url, const char *destination)
{
    return TitanBrowserNet_Begin(url, TITAN_BROWSER_NET_MODE_DOWNLOAD, destination);
}

void TitanBrowserNet_Cancel(void)
{
    if (g_net_state == TITAN_BROWSER_NET_QUEUED || g_net_state == TITAN_BROWSER_NET_CONNECTING ||
        g_net_state == TITAN_BROWSER_NET_RECEIVING)
        g_net_cancel = 1;
}

const TitanBrowserNetSnapshot *TitanBrowserNet_GetSnapshot(void)
{
    g_net_snapshot.state = g_net_state;
    return &g_net_snapshot;
}

const unsigned char *TitanBrowserNet_GetPageData(void) { return g_net_page; }
size_t TitanBrowserNet_GetPageSize(void) { return g_net_page_size; }

void TitanBrowserNet_ReleaseResult(void)
{
    if (g_net_state == TITAN_BROWSER_NET_COMPLETE || g_net_state == TITAN_BROWSER_NET_FAILED ||
        g_net_state == TITAN_BROWSER_NET_CANCELLED)
    {
        g_net_state = TITAN_BROWSER_NET_IDLE;
        g_net_snapshot.state = TITAN_BROWSER_NET_IDLE;
    }
}

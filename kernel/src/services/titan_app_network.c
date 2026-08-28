#include "titan_app_network.h"
#include "titan_network_client.h"

#include <stdlib.h>
#include <string.h>

#if defined(__EE__) || defined(_EE)
#include <delaythread.h>
#include <kernel.h>
extern void *_gp;
#endif

#define TITAN_APP_NETWORK_INTERNAL_CAPACITY (64u * 1024u)
#define TITAN_APP_NETWORK_URL_CAPACITY 1024u
#define TITAN_APP_NETWORK_STACK_SIZE (24u * 1024u)
#define TITAN_APP_NETWORK_DEFAULT_DEADLINE_MS 60000u

typedef struct TitanAppNetworkStateInternal
{
    volatile TitanAppNetworkState state;
    volatile int cancel;
    uint32_t generation;
    uint32_t request_generation;
    uint32_t maximum_bytes;
    uint32_t deadline_ms;
    uint32_t size;
    uint32_t content_length;
    volatile int worker_active;
    int http_status;
    TitanHttpFailureReason failure;
    char url[TITAN_APP_NETWORK_URL_CAPACITY];
    unsigned char *body;
#if defined(__EE__) || defined(_EE)
    int thread_id;
    unsigned char stack[TITAN_APP_NETWORK_STACK_SIZE] __attribute__((aligned(16)));
#endif
} TitanAppNetworkStateInternal;

static TitanAppNetworkStateInternal g_app_network;

#if defined(__EE__) || defined(_EE)
static int TitanAppNetwork_ShouldCancel(void *user_data)
{
    uint32_t generation = (uint32_t)(uintptr_t)user_data;
    return g_app_network.cancel != 0 || generation != g_app_network.generation;
}
#endif

static TitanAppNetworkFailure TitanAppNetwork_MapFailure(TitanHttpFailureReason reason)
{
    switch (reason)
    {
        case TITAN_HTTP_FAILURE_CANCELLED: return TITAN_APP_NETWORK_FAILURE_CANCELLED;
        case TITAN_HTTP_FAILURE_TIMEOUT: return TITAN_APP_NETWORK_FAILURE_TIMEOUT;
        case TITAN_HTTP_FAILURE_DNS: return TITAN_APP_NETWORK_FAILURE_DNS;
        case TITAN_HTTP_FAILURE_CONNECT: return TITAN_APP_NETWORK_FAILURE_CONNECT;
        case TITAN_HTTP_FAILURE_TLS: return TITAN_APP_NETWORK_FAILURE_TLS;
        case TITAN_HTTP_FAILURE_TOO_LARGE: return TITAN_APP_NETWORK_FAILURE_TOO_LARGE;
        case TITAN_HTTP_FAILURE_TRUNCATED: return TITAN_APP_NETWORK_FAILURE_TRUNCATED;
        case TITAN_HTTP_FAILURE_PROTOCOL: return TITAN_APP_NETWORK_FAILURE_PROTOCOL;
        default: return TITAN_APP_NETWORK_FAILURE_NONE;
    }
}

#if defined(__EE__) || defined(_EE)
static void TitanAppNetwork_Worker(void *argument)
{
    TitanHttpClientOptions options;
    TitanHttpResultInfo info;
    uint32_t generation = (uint32_t)(uintptr_t)argument;
    unsigned int size = 0u;
    int status = 0;
    int ok;

    memset(&options, 0, sizeof(options));
    memset(&info, 0, sizeof(info));
    options.should_cancel = TitanAppNetwork_ShouldCancel;
    options.user_data = (void *)(uintptr_t)generation;
    options.yield_microseconds = 1000u;
    options.maximum_redirects = 3u;
    options.deadline_milliseconds = g_app_network.deadline_ms;

    ok = TitanNetworkClient_GetEx(
        TITAN_NETWORK_CLIENT_OWNER_NATIVE_APP,
        g_app_network.url,
        g_app_network.body,
        g_app_network.maximum_bytes,
        &size,
        &status,
        &options,
        &info
    );

    if (generation == g_app_network.generation)
    {
        g_app_network.size = size;
        g_app_network.content_length = info.content_length;
        g_app_network.http_status = status;
        g_app_network.failure = info.failure_reason;
        if (g_app_network.cancel || info.failure_reason == TITAN_HTTP_FAILURE_CANCELLED)
            g_app_network.state = TITAN_APP_NETWORK_CANCELLED;
        else if (ok)
            g_app_network.state = TITAN_APP_NETWORK_COMPLETE;
        else
            g_app_network.state = TITAN_APP_NETWORK_FAILED;
    }
    else
    {
        free(g_app_network.body);
        g_app_network.body = 0;
    }
    g_app_network.worker_active = 0;
    g_app_network.thread_id = -1;
    /* No controller retains this completed worker's ID, so the worker must
     * destroy its own EE thread object instead of leaving a zombie behind. */
    ExitDeleteThread();
}

static int TitanAppNetwork_StartWorker(uint32_t generation)
{
    ee_thread_t thread;
    int id;
    memset(&thread, 0, sizeof(thread));
    thread.func = TitanAppNetwork_Worker;
    thread.stack = g_app_network.stack;
    thread.stack_size = sizeof(g_app_network.stack);
    thread.gp_reg = &_gp;
    thread.initial_priority = 66;
    id = CreateThread(&thread);
    if (id < 0) return 0;
    g_app_network.thread_id = id;
    if (StartThread(id, (void *)(uintptr_t)generation) < 0)
    {
        DeleteThread(id);
        g_app_network.thread_id = -1;
        return 0;
    }
    return 1;
}
#endif

void TitanAppNetwork_Initialize(void)
{
    memset(&g_app_network, 0, sizeof(g_app_network));
    g_app_network.state = TITAN_APP_NETWORK_IDLE;
#if defined(__EE__) || defined(_EE)
    g_app_network.thread_id = -1;
#endif
}

int TitanAppNetwork_BeginGet(const char *url, uint32_t maximum_bytes, uint32_t deadline_ms)
{
    size_t length;
    if (url == 0 || url[0] == '\0') return 0;
    length = strlen(url);
    if (length >= sizeof(g_app_network.url)) return 0;
    if (g_app_network.worker_active || g_app_network.state == TITAN_APP_NETWORK_PENDING) return 0;
    if (maximum_bytes == 0u || maximum_bytes > TITAN_APP_NETWORK_INTERNAL_CAPACITY) return 0;
    if (deadline_ms == 0u) deadline_ms = TITAN_APP_NETWORK_DEFAULT_DEADLINE_MS;
    if (deadline_ms > 120000u) deadline_ms = 120000u;

    free(g_app_network.body);
    g_app_network.body = (unsigned char *)malloc(maximum_bytes);
    if (g_app_network.body == 0) return 0;
    g_app_network.generation++;
    if (g_app_network.generation == 0u) g_app_network.generation = 1u;
    g_app_network.request_generation = g_app_network.generation;
    memcpy(g_app_network.url, url, length + 1u);
    g_app_network.maximum_bytes = maximum_bytes;
    g_app_network.deadline_ms = deadline_ms;
    g_app_network.size = 0u;
    g_app_network.content_length = 0u;
    g_app_network.http_status = 0;
    g_app_network.failure = TITAN_HTTP_FAILURE_NONE;
    g_app_network.cancel = 0;
    g_app_network.state = TITAN_APP_NETWORK_PENDING;
#if defined(__EE__) || defined(_EE)
    g_app_network.worker_active = 1;
    if (!TitanAppNetwork_StartWorker(g_app_network.request_generation))
    {
        g_app_network.worker_active = 0;
        free(g_app_network.body);
        g_app_network.body = 0;
        g_app_network.state = TITAN_APP_NETWORK_FAILED;
        g_app_network.failure = TITAN_HTTP_FAILURE_CONNECT;
        return 0;
    }
    return 1;
#else
    free(g_app_network.body);
    g_app_network.body = 0;
    g_app_network.state = TITAN_APP_NETWORK_FAILED;
    g_app_network.failure = TITAN_HTTP_FAILURE_PROTOCOL;
    return 0;
#endif
}

int TitanAppNetwork_GetStatus(TitanAppNetworkResult *result)
{
    if (result == 0) return 0;
    memset(result, 0, sizeof(*result));
    result->state = g_app_network.state;
    result->http_status = g_app_network.http_status;
    result->bytes_available = g_app_network.size;
    result->content_length = g_app_network.content_length;
    result->failure_reason = TitanAppNetwork_MapFailure(g_app_network.failure);
    return 1;
}

int TitanAppNetwork_Read(uint32_t offset, void *buffer, uint32_t capacity)
{
    uint32_t available;
    if (buffer == 0 || capacity == 0u || g_app_network.body == 0 ||
        g_app_network.state != TITAN_APP_NETWORK_COMPLETE) return -1;
    if (offset > g_app_network.size) return -1;
    available = g_app_network.size - offset;
    if (capacity > available) capacity = available;
    if (capacity != 0u) memcpy(buffer, g_app_network.body + offset, capacity);
    return (int)capacity;
}

void TitanAppNetwork_Cancel(void)
{
    if (g_app_network.state == TITAN_APP_NETWORK_PENDING) g_app_network.cancel = 1;
}

void TitanAppNetwork_AbandonSession(void)
{
    if (g_app_network.worker_active) g_app_network.cancel = 1;
    g_app_network.generation++;
    if (g_app_network.generation == 0u) g_app_network.generation = 1u;
    /* The old worker owns only service-static memory. Mark the app-visible
     * result abandoned immediately, but keep worker_active as the reuse gate
     * until that worker observes cancellation and exits. */
    g_app_network.state = TITAN_APP_NETWORK_IDLE;
    if (!g_app_network.worker_active)
    {
        free(g_app_network.body);
        g_app_network.body = 0;
    }
    g_app_network.size = 0u;
    g_app_network.content_length = 0u;
    g_app_network.http_status = 0;
    g_app_network.failure = TITAN_HTTP_FAILURE_NONE;
}

#include "titan_network_client.h"

#include <string.h>

#if defined(__EE__) || defined(_EE)
#include <kernel.h>
#endif

static volatile TitanNetworkClientOwner g_owner;
static TitanNetworkClientSnapshot g_snapshot;

static int TitanNetworkClient_TryAcquire(TitanNetworkClientOwner owner)
{
    int acquired = 0;
#if defined(__EE__) || defined(_EE)
    int state = DIntr();
#endif
    if (owner != TITAN_NETWORK_CLIENT_OWNER_NONE && g_owner == TITAN_NETWORK_CLIENT_OWNER_NONE)
    {
        g_owner = owner;
        g_snapshot.owner = owner;
        g_snapshot.requests_started++;
        acquired = 1;
    }
    else
    {
        g_snapshot.requests_rejected_busy++;
    }
#if defined(__EE__) || defined(_EE)
    if (state != 0) EIntr();
#endif
    return acquired;
}

static void TitanNetworkClient_Release(void)
{
#if defined(__EE__) || defined(_EE)
    int state = DIntr();
#endif
    g_owner = TITAN_NETWORK_CLIENT_OWNER_NONE;
    g_snapshot.owner = TITAN_NETWORK_CLIENT_OWNER_NONE;
#if defined(__EE__) || defined(_EE)
    if (state != 0) EIntr();
#endif
}

int TitanNetworkClient_GetEx(
    TitanNetworkClientOwner owner,
    const char *url,
    unsigned char *destination,
    unsigned int capacity,
    unsigned int *out_size,
    int *out_status,
    const TitanHttpClientOptions *options,
    TitanHttpResultInfo *result_info
)
{
    TitanHttpResultInfo local_info;
    TitanHttpResultInfo *info = result_info != 0 ? result_info : &local_info;
    int ok;

    memset(&local_info, 0, sizeof(local_info));
    if (!TitanNetworkClient_TryAcquire(owner))
    {
        if (out_size != 0) *out_size = 0u;
        if (out_status != 0) *out_status = 0;
        if (result_info != 0)
        {
            memset(result_info, 0, sizeof(*result_info));
            result_info->failure_reason = TITAN_HTTP_FAILURE_CANCELLED;
        }
        return 0;
    }

    ok = TitanHttpClient_GetEx(url, destination, capacity, out_size, out_status, options, info);
    if (ok) g_snapshot.requests_completed++;
    else if (info->failure_reason == TITAN_HTTP_FAILURE_CANCELLED) g_snapshot.requests_cancelled++;
    else if (info->failure_reason == TITAN_HTTP_FAILURE_TIMEOUT) g_snapshot.requests_timed_out++;
    TitanNetworkClient_Release();
    return ok;
}

void TitanNetworkClient_ResetSession(void)
{
    if (g_owner == TITAN_NETWORK_CLIENT_OWNER_NONE)
        TitanHttpClient_ResetSession();
}

void TitanNetworkClient_GetSnapshot(TitanNetworkClientSnapshot *snapshot)
{
    if (snapshot != 0) *snapshot = g_snapshot;
}

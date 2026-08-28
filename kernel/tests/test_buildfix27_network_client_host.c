#include "titan_network_client.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int g_nested_result = -1;

int TitanHttpClient_GetEx(const char *url, unsigned char *destination, unsigned int capacity,
    unsigned int *out_size, int *out_status, const TitanHttpClientOptions *options,
    TitanHttpResultInfo *result_info)
{
    (void)options;
    if (result_info) memset(result_info, 0, sizeof(*result_info));
    if (strcmp(url, "nested") == 0)
    {
        unsigned char scratch[4];
        TitanHttpResultInfo nested;
        g_nested_result = TitanNetworkClient_GetEx(TITAN_NETWORK_CLIENT_OWNER_BROWSER,
            "success", scratch, sizeof(scratch), 0, 0, 0, &nested);
    }
    if (strcmp(url, "timeout") == 0)
    {
        if (result_info) result_info->failure_reason = TITAN_HTTP_FAILURE_TIMEOUT;
        return 0;
    }
    if (strcmp(url, "cancel") == 0)
    {
        if (result_info) result_info->failure_reason = TITAN_HTTP_FAILURE_CANCELLED;
        return 0;
    }
    if (capacity < 2u) return 0;
    destination[0] = 'O'; destination[1] = 'K';
    if (out_size) *out_size = 2u;
    if (out_status) *out_status = 200;
    if (result_info) { result_info->bytes_received = 2u; result_info->content_length = 2u; }
    return 1;
}

void TitanHttpClient_ResetSession(void) { }

int main(void)
{
    unsigned char body[8];
    unsigned int size = 0u;
    int status = 0;
    TitanHttpResultInfo info;
    TitanNetworkClientSnapshot snap;

    assert(TitanNetworkClient_GetEx(TITAN_NETWORK_CLIENT_OWNER_ASSET_SERVICE,
        "success", body, sizeof(body), &size, &status, 0, &info));
    assert(size == 2u && status == 200 && body[0] == 'O' && body[1] == 'K');

    memset(&info, 0, sizeof(info));
    assert(!TitanNetworkClient_GetEx(TITAN_NETWORK_CLIENT_OWNER_BROWSER,
        "timeout", body, sizeof(body), &size, &status, 0, &info));
    assert(info.failure_reason == TITAN_HTTP_FAILURE_TIMEOUT);

    memset(&info, 0, sizeof(info));
    assert(!TitanNetworkClient_GetEx(TITAN_NETWORK_CLIENT_OWNER_NATIVE_APP,
        "cancel", body, sizeof(body), &size, &status, 0, &info));
    assert(info.failure_reason == TITAN_HTTP_FAILURE_CANCELLED);

    g_nested_result = -1;
    assert(TitanNetworkClient_GetEx(TITAN_NETWORK_CLIENT_OWNER_NATIVE_APP,
        "nested", body, sizeof(body), &size, &status, 0, &info));
    assert(g_nested_result == 0);

    TitanNetworkClient_GetSnapshot(&snap);
    assert(snap.owner == TITAN_NETWORK_CLIENT_OWNER_NONE);
    assert(snap.requests_started == 4u);
    assert(snap.requests_completed == 2u);
    assert(snap.requests_timed_out == 1u);
    assert(snap.requests_cancelled == 1u);
    assert(snap.requests_rejected_busy == 1u);

    puts("buildfix27-network-client-host: OK success/timeout/cancel/nested-busy telemetry");
    return 0;
}

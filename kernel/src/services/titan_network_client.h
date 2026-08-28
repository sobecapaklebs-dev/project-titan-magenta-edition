#ifndef PROJECT_TITAN_NETWORK_CLIENT_H
#define PROJECT_TITAN_NETWORK_CLIENT_H

#include "titan_http_client.h"

typedef enum TitanNetworkClientOwner
{
    TITAN_NETWORK_CLIENT_OWNER_NONE = 0,
    TITAN_NETWORK_CLIENT_OWNER_ASSET_SERVICE,
    TITAN_NETWORK_CLIENT_OWNER_BROWSER,
    TITAN_NETWORK_CLIENT_OWNER_NATIVE_APP,
    TITAN_NETWORK_CLIENT_OWNER_DOWNLOAD_SERVICE
} TitanNetworkClientOwner;

typedef struct TitanNetworkClientSnapshot
{
    TitanNetworkClientOwner owner;
    unsigned int requests_started;
    unsigned int requests_completed;
    unsigned int requests_cancelled;
    unsigned int requests_timed_out;
    unsigned int requests_rejected_busy;
} TitanNetworkClientSnapshot;

/* Single-flight reusable HTTP/HTTPS client boundary. The network stack, DNS,
 * TCP, TLS and pooled sockets stay owned by Titan. Callers supply bounded
 * buffers and cooperative cancel/progress callbacks through TitanHttpClientOptions.
 * A request that cannot acquire ownership fails closed instead of starting a
 * second browser-private or app-private transport stack. */
int TitanNetworkClient_GetEx(
    TitanNetworkClientOwner owner,
    const char *url,
    unsigned char *destination,
    unsigned int capacity,
    unsigned int *out_size,
    int *out_status,
    const TitanHttpClientOptions *options,
    TitanHttpResultInfo *result_info
);

void TitanNetworkClient_ResetSession(void);
void TitanNetworkClient_GetSnapshot(TitanNetworkClientSnapshot *snapshot);

#endif

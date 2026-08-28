#ifndef PROJECT_TITAN_HTTP_CLIENT_H
#define PROJECT_TITAN_HTTP_CLIENT_H

typedef int (*TitanHttpCancelCallback)(void *user_data);

typedef enum TitanHttpTracePhase
{
    TITAN_HTTP_TRACE_DNS_BEGIN = 1,
    TITAN_HTTP_TRACE_DNS_END,
    TITAN_HTTP_TRACE_TCP_BEGIN,
    TITAN_HTTP_TRACE_TCP_END,
    TITAN_HTTP_TRACE_TLS_BEGIN,
    TITAN_HTTP_TRACE_TLS_END,
    TITAN_HTTP_TRACE_HEADERS_BEGIN,
    TITAN_HTTP_TRACE_HEADERS_END,
    TITAN_HTTP_TRACE_BODY_BEGIN,
    TITAN_HTTP_TRACE_BODY_END,
    TITAN_HTTP_TRACE_TLS_ENGINE_ERROR,
    TITAN_HTTP_TRACE_PROTOCOL_ERROR,
    TITAN_HTTP_TRACE_SOCKET_ERROR,
    TITAN_HTTP_TRACE_CONNECT_ERROR,
    TITAN_HTTP_TRACE_CONNECTION_REUSE,
    TITAN_HTTP_TRACE_CONNECTION_CLOSE
} TitanHttpTracePhase;

typedef void (*TitanHttpTraceCallback)(
    void *user_data,
    TitanHttpTracePhase phase,
    int result
);

typedef void (*TitanHttpProgressCallback)(
    void *user_data,
    unsigned int received_bytes,
    unsigned int total_bytes
);

/* Optional streaming body sink. Returning 0 cancels/fails the transfer.
 * When set, a successful 200 response is delivered incrementally and the
 * caller does not need a body-sized RAM buffer. */
typedef int (*TitanHttpBodyWriteCallback)(
    void *user_data,
    const unsigned char *data,
    unsigned int size
);

typedef enum TitanHttpFailureReason
{
    TITAN_HTTP_FAILURE_NONE = 0,
    TITAN_HTTP_FAILURE_CANCELLED,
    TITAN_HTTP_FAILURE_TIMEOUT,
    TITAN_HTTP_FAILURE_DNS,
    TITAN_HTTP_FAILURE_CONNECT,
    TITAN_HTTP_FAILURE_TLS,
    TITAN_HTTP_FAILURE_PROTOCOL,
    TITAN_HTTP_FAILURE_TOO_LARGE,
    TITAN_HTTP_FAILURE_TRUNCATED
} TitanHttpFailureReason;

typedef enum TitanHttpTransferStage
{
    TITAN_HTTP_STAGE_IDLE = 0,
    TITAN_HTTP_STAGE_DNS,
    TITAN_HTTP_STAGE_TCP,
    TITAN_HTTP_STAGE_TLS,
    TITAN_HTTP_STAGE_HEADERS,
    TITAN_HTTP_STAGE_BODY
} TitanHttpTransferStage;

typedef struct TitanHttpResultInfo
{
    TitanHttpFailureReason failure_reason;
    TitanHttpTransferStage stage;
    unsigned int bytes_received;
    unsigned int content_length;
} TitanHttpResultInfo;

typedef struct TitanHttpClientOptions
{
    TitanHttpCancelCallback should_cancel;
    TitanHttpTraceCallback trace;
    TitanHttpProgressCallback progress;
    void *user_data;
    unsigned int yield_microseconds;
    unsigned int maximum_redirects;
    /* 0 uses the client default. This is a cooperative whole-request deadline;
     * socket/TLS callbacks check it between bounded I/O operations. */
    unsigned int deadline_milliseconds;
    TitanHttpBodyWriteCallback body_write;
} TitanHttpClientOptions;

/* Bounded HTTP/HTTPS GET. The caller owns destination. Returns 1 when a
 * complete HTTP response was received (including non-200 status codes), or 0
 * on transport/protocol/cancellation failure. HTTPS performs hostname and
 * certificate-chain validation with the build-pinned trust anchors. */
int TitanHttpClient_GetEx(
    const char *url,
    unsigned char *destination,
    unsigned int capacity,
    unsigned int *out_size,
    int *out_status,
    const TitanHttpClientOptions *options,
    TitanHttpResultInfo *result_info
);

/* Compatibility wrapper for existing callers. */
int TitanHttpClient_Get(
    const char *url,
    unsigned char *destination,
    unsigned int capacity,
    unsigned int *out_size,
    int *out_status,
    const TitanHttpClientOptions *options
);

/* Close pooled keep-alive sockets and clear the small DNS cache. Asset Service
 * calls this only at a drained session boundary, never while a request is live. */
void TitanHttpClient_ResetSession(void);

#endif

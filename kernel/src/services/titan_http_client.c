#include "titan_http_client.h"
#include "titan_time.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EE__) || defined(_EE)
#include <bearssl.h>
#include <delaythread.h>
#include <libcdvd.h>
#include <netdb.h>
#include <ps2ip.h>
#include <sys/select.h>
#include <timer.h>

#if defined(PROJECT_TITAN_TLS_TEST_STUB)
static const br_x509_trust_anchor TAs[1] = { { 0 } };
#define TAs_NUM 1
#else
#include "generated/titan_tls_ca_bundle.inc"
#endif
#endif

#define TITAN_HTTP_HOST_CAPACITY 160u
#define TITAN_HTTP_PATH_CAPACITY 768u
#define TITAN_HTTP_URL_CAPACITY 1024u
#define TITAN_HTTP_HEADER_CAPACITY 4096u
#define TITAN_HTTP_LINE_CAPACITY 256u
#define TITAN_HTTP_IO_CHUNK 4096u
#define TITAN_HTTP_DEFAULT_REDIRECTS 3u
#define TITAN_HTTP_MAX_REDIRECTS 5u
#define TITAN_HTTP_DEFAULT_DEADLINE_MS 60000u
#define TITAN_HTTP_DNS_CACHE_SLOTS 4u
#define TITAN_HTTP_CONNECTION_SLOTS 2u
#define TITAN_HTTP_REUSED_RETRY_LIMIT 1u
#define TITAN_HTTP_USER_AGENT "Project-Titan/NativeAssetHTTP-1"

#if defined(__EE__) || defined(_EE)
typedef enum TitanHttpScheme
{
    TITAN_HTTP_SCHEME_HTTP = 1,
    TITAN_HTTP_SCHEME_HTTPS = 2
} TitanHttpScheme;

typedef struct TitanHttpUrl
{
    TitanHttpScheme scheme;
    char host[TITAN_HTTP_HOST_CAPACITY];
    char path[TITAN_HTTP_PATH_CAPACITY];
    unsigned short port;
} TitanHttpUrl;

typedef struct TitanHttpTransport
{
    int fd;
    int tls;
    br_ssl_client_context ssl;
    br_x509_minimal_context x509;
    br_sslio_context ioc;
    const TitanHttpClientOptions *options;
    TitanHttpResultInfo *result_info;
    uint64_t deadline_at_ms;
    unsigned char tls_buffer[BR_SSL_BUFSIZE_BIDI] __attribute__((aligned(16)));
} TitanHttpTransport;

typedef struct TitanHttpDnsCacheEntry
{
    char host[TITAN_HTTP_HOST_CAPACITY];
    struct in_addr address;
    uint64_t last_used_ms;
    unsigned int connect_failures;
    int valid;
} TitanHttpDnsCacheEntry;

typedef struct TitanHttpConnectionSlot
{
    TitanHttpTransport transport;
    TitanHttpScheme scheme;
    char host[TITAN_HTTP_HOST_CAPACITY];
    unsigned short port;
    uint64_t last_used_ms;
    int valid;
} TitanHttpConnectionSlot;

typedef struct TitanHttpScratch
{
    TitanHttpUrl url;
    TitanHttpConnectionSlot connections[TITAN_HTTP_CONNECTION_SLOTS];
    TitanHttpDnsCacheEntry dns_cache[TITAN_HTTP_DNS_CACHE_SLOTS];
    char request[1280];
    char header[TITAN_HTTP_HEADER_CAPACITY];
    char location[TITAN_HTTP_URL_CAPACITY];
    char current[TITAN_HTTP_URL_CAPACITY];
    char redirect[TITAN_HTTP_URL_CAPACITY];
    unsigned char body_chunk[TITAN_HTTP_IO_CHUNK];
} TitanHttpScratch;

/* The native HTTP client is intentionally single-flight: Asset Service has one
 * network worker and no other Titan subsystem calls it. Keeping BearSSL and the
 * 4 KiB HTTP header scratch out of the EE thread stack prevents TLS handshakes
 * from overwriting adjacent service state. A second caller fails closed. */
static TitanHttpScratch g_http_scratch __attribute__((aligned(64)));
static volatile int g_http_scratch_busy;

static int TitanHttp_ShouldCancel(const TitanHttpClientOptions *options)
{
    return options != 0 && options->should_cancel != 0 &&
        options->should_cancel(options->user_data);
}

static uint64_t TitanHttp_DeadlineFromOptions(const TitanHttpClientOptions *options)
{
    unsigned int delta = options != 0 && options->deadline_milliseconds != 0u
        ? options->deadline_milliseconds : TITAN_HTTP_DEFAULT_DEADLINE_MS;
    if (delta > 120000u) delta = 120000u;
    return TitanTime_NowMilliseconds() + (uint64_t)delta;
}

static int TitanHttp_DeadlineExpired(uint64_t deadline_at_ms)
{
    return deadline_at_ms != 0u && TitanTime_NowMilliseconds() >= deadline_at_ms;
}

static void TitanHttp_ResultInit(TitanHttpResultInfo *info)
{
    if (info == 0) return;
    memset(info, 0, sizeof(*info));
    info->failure_reason = TITAN_HTTP_FAILURE_NONE;
    info->stage = TITAN_HTTP_STAGE_IDLE;
}

static void TitanHttp_ResultStage(TitanHttpResultInfo *info, TitanHttpTransferStage stage)
{
    if (info != 0) info->stage = stage;
}

static void TitanHttp_ResultFailure(TitanHttpResultInfo *info, TitanHttpFailureReason reason)
{
    if (info != 0 && info->failure_reason == TITAN_HTTP_FAILURE_NONE)
        info->failure_reason = reason;
}

static void TitanHttp_ResultStopReason(TitanHttpTransport *transport)
{
    if (transport == 0) return;
    if (TitanHttp_DeadlineExpired(transport->deadline_at_ms))
        TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_TIMEOUT);
    else if (TitanHttp_ShouldCancel(transport->options))
        TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_CANCELLED);
}

static int TitanHttp_TransportShouldCancel(const TitanHttpTransport *transport)
{
    return transport == 0 || TitanHttp_DeadlineExpired(transport->deadline_at_ms) ||
        TitanHttp_ShouldCancel(transport->options);
}

static void TitanHttp_Yield(const TitanHttpClientOptions *options)
{
    if (options != 0 && options->yield_microseconds != 0u)
        DelayThread(options->yield_microseconds);
}

static void TitanHttp_Trace(
    const TitanHttpClientOptions *options,
    TitanHttpTracePhase phase,
    int result
)
{
    if (options != 0 && options->trace != 0)
        options->trace(options->user_data, phase, result);
}

static void TitanHttp_Progress(
    const TitanHttpClientOptions *options,
    unsigned int received_bytes,
    unsigned int total_bytes
)
{
    if (options != 0 && options->progress != 0)
        options->progress(options->user_data, received_bytes, total_bytes);
}

static int TitanHttp_WaitSocket(TitanHttpTransport *transport, int for_write)
{
    while (!TitanHttp_TransportShouldCancel(transport))
    {
        fd_set read_fds;
        fd_set write_fds;
        struct timeval timeout;
        int result;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        if (for_write) FD_SET(transport->fd, &write_fds);
        else FD_SET(transport->fd, &read_fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        result = select(transport->fd + 1, for_write ? 0 : &read_fds,
            for_write ? &write_fds : 0, 0, &timeout);
        if (result > 0) return 1;
        if (result < 0) return 0;
        TitanHttp_Yield(transport->options);
    }
    TitanHttp_ResultStopReason(transport);
    return 0;
}

static int TitanHttp_SocketRecv(TitanHttpTransport *transport, void *buffer, unsigned int size)
{
    int result;
    if (transport == 0 || buffer == 0 || size == 0u || !TitanHttp_WaitSocket(transport, 0)) return -1;
    result = recv(transport->fd, buffer, (int)size, 0);
    if (result > 0) TitanHttp_Yield(transport->options);
    return result;
}

static int TitanHttp_SocketSend(TitanHttpTransport *transport, const void *buffer, unsigned int size)
{
    int result;
    if (transport == 0 || buffer == 0 || size == 0u || !TitanHttp_WaitSocket(transport, 1)) return -1;
    result = send(transport->fd, buffer, (int)size, 0);
    if (result > 0) TitanHttp_Yield(transport->options);
    return result;
}

static int TitanHttp_CopyRange(char *dst, unsigned int cap, const char *begin, const char *end)
{
    unsigned int len;
    if (dst == 0 || cap == 0u || begin == 0 || end == 0 || end < begin) return 0;
    len = (unsigned int)(end - begin);
    if (len + 1u > cap) return 0;
    memcpy(dst, begin, len);
    dst[len] = '\0';
    return 1;
}

static int TitanHttp_ParseUrl(const char *value, TitanHttpUrl *out)
{
    const char *authority;
    const char *slash;
    const char *colon = 0;
    const char *host_end;
    unsigned long port;
    char port_text[8];
    char *port_end;
    const char *p;

    if (value == 0 || out == 0) return 0;
    memset(out, 0, sizeof(*out));
    if (strncmp(value, "https://", 8u) == 0)
    {
        out->scheme = TITAN_HTTP_SCHEME_HTTPS;
        out->port = 443u;
        authority = value + 8u;
    }
    else if (strncmp(value, "http://", 7u) == 0)
    {
        out->scheme = TITAN_HTTP_SCHEME_HTTP;
        out->port = 80u;
        authority = value + 7u;
    }
    else return 0;

    slash = strchr(authority, '/');
    host_end = slash != 0 ? slash : authority + strlen(authority);
    if (host_end == authority) return 0;
    for (p = authority; p < host_end; p++)
    {
        unsigned char c = (unsigned char)*p;
        if (c <= 0x20u || c == 0x7fu || c == '@' || c == 0x5cu || c == '[' || c == ']') return 0;
        if (*p == ':')
        {
            if (colon != 0) return 0; /* IPv6 literals are intentionally unsupported. */
            colon = p;
        }
    }
    if (colon != 0)
    {
        if (!TitanHttp_CopyRange(out->host, sizeof(out->host), authority, colon)) return 0;
        if (!TitanHttp_CopyRange(port_text, sizeof(port_text), colon + 1, host_end)) return 0;
        port = strtoul(port_text, &port_end, 10);
        if (port_end == port_text || *port_end != '\0' || port == 0u || port > 65535u) return 0;
        out->port = (unsigned short)port;
    }
    else if (!TitanHttp_CopyRange(out->host, sizeof(out->host), authority, host_end)) return 0;

    if (out->host[0] == '\0') return 0;
    if (slash != 0)
    {
        for (p = slash; *p != '\0'; p++)
        {
            unsigned char c = (unsigned char)*p;
            if (c < 0x20u || c == 0x7fu || c == '\r' || c == '\n') return 0;
        }
    }
    if (slash == 0)
    {
        out->path[0] = '/';
        out->path[1] = '\0';
    }
    else
    {
        if (strlen(slash) + 1u > sizeof(out->path)) return 0;
        strcpy(out->path, slash);
    }
    return 1;
}

static int TitanHttp_BcdToInt(unsigned char value)
{
    return ((value >> 4u) & 0x0fu) * 10 + (value & 0x0fu);
}

static int TitanHttp_IsLeapYear(unsigned int year)
{
    return (year % 4u == 0u && year % 100u != 0u) || year % 400u == 0u;
}

static uint32_t TitanHttp_DaysBeforeYear(unsigned int year)
{
    unsigned int y;
    if (year == 0u) return 0u;
    y = year - 1u;
    /* BearSSL's epoch is 0000-01-01 in the proleptic Gregorian calendar;
     * year zero is a leap year. */
    return 365u * year + (y / 4u + 1u) - (y / 100u + 1u) + (y / 400u + 1u);
}

static int TitanHttp_ParseBuildDate(unsigned int *year, unsigned int *month, unsigned int *day)
{
    static const char *months[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    const char *date = __DATE__;
    unsigned int i;
    unsigned int parsed_day;
    unsigned int parsed_year;
    if (year == 0 || month == 0 || day == 0) return 0;
    for (i = 0u; i < 12u; i++)
        if (date[0] == months[i][0] && date[1] == months[i][1] && date[2] == months[i][2]) break;
    if (i == 12u) return 0;
    parsed_day = (date[4] == ' ' ? 0u : (unsigned int)(date[4] - '0') * 10u) +
        (unsigned int)(date[5] - '0');
    parsed_year =
        (unsigned int)(date[7] - '0') * 1000u +
        (unsigned int)(date[8] - '0') * 100u +
        (unsigned int)(date[9] - '0') * 10u +
        (unsigned int)(date[10] - '0');
    if (parsed_day < 1u || parsed_day > 31u || parsed_year < 2020u) return 0;
    *year = parsed_year;
    *month = i + 1u;
    *day = parsed_day;
    return 1;
}

static int TitanHttp_GetBearSslTime(uint32_t *days, uint32_t *seconds)
{
    static const unsigned short month_days[12] = {
        31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u
    };
    sceCdCLOCK clock;
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int i;
    uint32_t result;

    if (days == 0 || seconds == 0) return 0;
    if (sceCdReadClock(&clock))
    {
        year = 2000u + (unsigned int)TitanHttp_BcdToInt(clock.year);
        month = (unsigned int)TitanHttp_BcdToInt(clock.month);
        day = (unsigned int)TitanHttp_BcdToInt(clock.day);
    }
    else year = month = day = 0u;

    if (year < 2020u || year > 2099u || month < 1u || month > 12u || day < 1u || day > 31u)
    {
        /* A dead PS2 clock battery must not silently disable X.509 checks. */
        if (!TitanHttp_ParseBuildDate(&year, &month, &day)) return 0;
    }
    if (day > month_days[month - 1u] + ((month == 2u && TitanHttp_IsLeapYear(year)) ? 1u : 0u))
        return 0;

    result = TitanHttp_DaysBeforeYear(year);
    for (i = 1u; i < month; i++)
    {
        result += month_days[i - 1u];
        if (i == 2u && TitanHttp_IsLeapYear(year)) result++;
    }
    result += day - 1u;
    *days = result;
    *seconds = 12u * 60u * 60u;
    return 1;
}

static void TitanHttp_SeedTls(br_ssl_client_context *ssl)
{
    unsigned char seed[64];
    uint64_t state = (uint64_t)GetTimerSystemTime();
    uintptr_t mix = (uintptr_t)ssl ^ (uintptr_t)&state ^ (uintptr_t)seed;
    unsigned int i;
    state ^= ((uint64_t)mix << 17u) ^ ((uint64_t)mix >> 3u);
    state ^= TitanTime_NowMilliseconds() * 0x9e3779b97f4a7c15ULL;
    if (state == 0u) state = 0xd1b54a32d192ed03ULL;
    for (i = 0u; i < sizeof(seed); i++)
    {
        uint64_t timer = (uint64_t)GetTimerSystemTime();
        state ^= timer + 0x9e3779b97f4a7c15ULL + (state << 6u) + (state >> 2u);
        state ^= state << 13u;
        state ^= state >> 7u;
        state ^= state << 17u;
        seed[i] = (unsigned char)(state ^ timer ^ (uint64_t)(i * 0x5bu));
    }
    br_ssl_engine_inject_entropy(&ssl->eng, seed, sizeof(seed));
    memset(seed, 0, sizeof(seed));
}

static int TitanHttp_TlsRead(void *ctx, unsigned char *buffer, size_t size)
{
    TitanHttpTransport *transport = (TitanHttpTransport *)ctx;
    unsigned int request;
    int got;
    if (transport == 0 || buffer == 0 || size == 0u ||
        TitanHttp_TransportShouldCancel(transport)) return -1;
    request = size > TITAN_HTTP_IO_CHUNK ? TITAN_HTTP_IO_CHUNK : (unsigned int)size;
    got = TitanHttp_SocketRecv(transport, buffer, request);
    return got > 0 ? got : -1;
}

static int TitanHttp_TlsWrite(void *ctx, const unsigned char *buffer, size_t size)
{
    TitanHttpTransport *transport = (TitanHttpTransport *)ctx;
    unsigned int sent = 0u;
    if (transport == 0 || buffer == 0) return -1;
    while (sent < (unsigned int)size)
    {
        unsigned int request = (unsigned int)size - sent;
        int n;
        if (TitanHttp_TransportShouldCancel(transport)) return -1;
        if (request > TITAN_HTTP_IO_CHUNK) request = TITAN_HTTP_IO_CHUNK;
        n = TitanHttp_SocketSend(transport, buffer + sent, request);
        if (n <= 0) return -1;
        sent += (unsigned int)n;
    }
    return (int)size;
}

static int TitanHttp_DnsCacheLookup(const char *host, struct in_addr *out)
{
    unsigned int i;
    if (host == 0 || out == 0) return 0;
    for (i = 0u; i < TITAN_HTTP_DNS_CACHE_SLOTS; i++)
    {
        TitanHttpDnsCacheEntry *entry = &g_http_scratch.dns_cache[i];
        if (!entry->valid || strcmp(entry->host, host) != 0) continue;
        *out = entry->address;
        entry->last_used_ms = TitanTime_NowMilliseconds();
        return 1;
    }
    return 0;
}

static void TitanHttp_DnsCacheStore(const char *host, const struct in_addr *address)
{
    unsigned int i;
    unsigned int slot = 0u;
    uint64_t oldest = UINT64_MAX;
    TitanHttpDnsCacheEntry *entry;
    if (host == 0 || address == 0) return;
    for (i = 0u; i < TITAN_HTTP_DNS_CACHE_SLOTS; i++)
    {
        entry = &g_http_scratch.dns_cache[i];
        if (entry->valid && strcmp(entry->host, host) == 0)
        {
            slot = i;
            oldest = 0u;
            break;
        }
        if (!entry->valid)
        {
            slot = i;
            oldest = 0u;
            break;
        }
        if (entry->last_used_ms < oldest)
        {
            oldest = entry->last_used_ms;
            slot = i;
        }
    }
    entry = &g_http_scratch.dns_cache[slot];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->host, host, sizeof(entry->host) - 1u);
    entry->address = *address;
    entry->last_used_ms = TitanTime_NowMilliseconds();
    entry->valid = 1;
}

static void TitanHttp_DnsCacheConnectResult(const char *host, int success)
{
    unsigned int i;
    if (host == 0) return;
    for (i = 0u; i < TITAN_HTTP_DNS_CACHE_SLOTS; i++)
    {
        TitanHttpDnsCacheEntry *entry = &g_http_scratch.dns_cache[i];
        if (!entry->valid || strcmp(entry->host, host) != 0) continue;
        if (success)
        {
            entry->connect_failures = 0u;
            return;
        }
        if (entry->connect_failures < 0xffffffffu) entry->connect_failures++;
        if (entry->connect_failures >= 2u) memset(entry, 0, sizeof(*entry));
        return;
    }
}

static int TitanHttp_OpenTcp(
    const TitanHttpUrl *url,
    TitanHttpTransport *transport
)
{
    const TitanHttpClientOptions *options;
    struct hostent *host_entry;
    struct sockaddr_in address;
    int fd;
    int error_code;
    if (url == 0 || transport == 0) return -1;
    options = transport->options;
    if (TitanHttp_TransportShouldCancel(transport)) { TitanHttp_ResultStopReason(transport); return -1; }

    TitanHttp_ResultStage(transport->result_info, TITAN_HTTP_STAGE_DNS);
    TitanHttp_Trace(options, TITAN_HTTP_TRACE_DNS_BEGIN, 0);
    memset(&address, 0, sizeof(address));
    if (TitanHttp_DnsCacheLookup(url->host, &address.sin_addr))
    {
        TitanHttp_Trace(options, TITAN_HTTP_TRACE_DNS_END, 2); /* multi-host cache hit */
    }
    else
    {
        host_entry = gethostbyname(url->host);
        if (TitanHttp_TransportShouldCancel(transport) || host_entry == 0 ||
            host_entry->h_addr_list == 0 || host_entry->h_addr_list[0] == 0)
        {
            TitanHttp_ResultStopReason(transport);
            if (transport->result_info == 0 || transport->result_info->failure_reason == TITAN_HTTP_FAILURE_NONE)
                TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_DNS);
            TitanHttp_Trace(options, TITAN_HTTP_TRACE_DNS_END, -1);
            return -1;
        }
        memcpy(&address.sin_addr, host_entry->h_addr_list[0], sizeof(address.sin_addr));
        TitanHttp_DnsCacheStore(url->host, &address.sin_addr);
        TitanHttp_Trace(options, TITAN_HTTP_TRACE_DNS_END, 1);
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(url->port);
    if (TitanHttp_TransportShouldCancel(transport)) { TitanHttp_ResultStopReason(transport); return -1; }

    TitanHttp_ResultStage(transport->result_info, TITAN_HTTP_STAGE_TCP);
    TitanHttp_Trace(options, TITAN_HTTP_TRACE_TCP_BEGIN, 0);
    errno = 0;
    fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
    {
        error_code = errno;
        TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_CONNECT);
        TitanHttp_Trace(options, TITAN_HTTP_TRACE_SOCKET_ERROR,
            error_code != 0 ? -error_code : -1);
        TitanHttp_Trace(options, TITAN_HTTP_TRACE_TCP_END, -1);
        return -1;
    }

    errno = 0;
    if (TitanHttp_TransportShouldCancel(transport) ||
        connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        TitanHttp_TransportShouldCancel(transport))
    {
        error_code = errno;
        lwip_close(fd);
        TitanHttp_ResultStopReason(transport);
        if (transport->result_info == 0 || transport->result_info->failure_reason == TITAN_HTTP_FAILURE_NONE)
            TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_CONNECT);
        if (transport->result_info == 0 ||
            (transport->result_info->failure_reason != TITAN_HTTP_FAILURE_CANCELLED &&
             transport->result_info->failure_reason != TITAN_HTTP_FAILURE_TIMEOUT))
            TitanHttp_Trace(options, TITAN_HTTP_TRACE_CONNECT_ERROR,
                error_code != 0 ? -error_code : -1);
        TitanHttp_DnsCacheConnectResult(url->host, 0);
        TitanHttp_Trace(options, TITAN_HTTP_TRACE_TCP_END, -1);
        return -1;
    }

    TitanHttp_DnsCacheConnectResult(url->host, 1);
    TitanHttp_Trace(options, TITAN_HTTP_TRACE_TCP_END, 1);
    return fd;
}

static int TitanHttp_TransportOpen(
    TitanHttpTransport *transport,
    const TitanHttpUrl *url,
    const TitanHttpClientOptions *options,
    TitanHttpResultInfo *result_info
)
{
    uint32_t days;
    uint32_t seconds;
    int tls_error = 0;
    int tls_initialized = 0;
    if (transport == 0 || url == 0) return 0;
    memset(transport, 0, sizeof(*transport));
    transport->fd = -1;
    transport->options = options;
    transport->result_info = result_info;
    transport->deadline_at_ms = TitanHttp_DeadlineFromOptions(options);
    transport->fd = TitanHttp_OpenTcp(url, transport);
    if (transport->fd < 0) return 0;
    transport->tls = url->scheme == TITAN_HTTP_SCHEME_HTTPS;
    if (!transport->tls) return 1;

    TitanHttp_ResultStage(result_info, TITAN_HTTP_STAGE_TLS);
    TitanHttp_Trace(options, TITAN_HTTP_TRACE_TLS_BEGIN, 0);
    if (TitanHttp_TransportShouldCancel(transport)) goto fail;
    br_ssl_client_init_full(&transport->ssl, &transport->x509, TAs, TAs_NUM);
    tls_initialized = 1;
    if (!TitanHttp_GetBearSslTime(&days, &seconds)) goto fail;
    br_x509_minimal_set_time(&transport->x509, days, seconds);
    TitanHttp_SeedTls(&transport->ssl);
    br_ssl_engine_set_buffer(&transport->ssl.eng, transport->tls_buffer, sizeof(transport->tls_buffer), 1);
    br_sslio_init(&transport->ioc, &transport->ssl.eng,
        TitanHttp_TlsRead, transport, TitanHttp_TlsWrite, transport);
    /* BearSSL uses this hostname both for SNI and X.509 name validation. */
    if (!br_ssl_client_reset(&transport->ssl, url->host, 0)) goto fail;
    if (TitanHttp_TransportShouldCancel(transport) || br_sslio_flush(&transport->ioc) != 0) goto fail;
    if (TitanHttp_TransportShouldCancel(transport)) goto fail;
    TitanHttp_Trace(options, TITAN_HTTP_TRACE_TLS_END, 1);
    return 1;

fail:
    TitanHttp_ResultStopReason(transport);
    tls_error = tls_initialized ? br_ssl_engine_last_error(&transport->ssl.eng) : BR_ERR_OK;
    if (result_info == 0 || result_info->failure_reason == TITAN_HTTP_FAILURE_NONE)
        TitanHttp_ResultFailure(result_info, TITAN_HTTP_FAILURE_TLS);
    /* BearSSL reports an engine error when our socket callbacks intentionally
     * stop for cancellation/deadline. Do not misdiagnose that as TLS failure. */
    if (tls_error != BR_ERR_OK && result_info != 0 &&
        result_info->failure_reason != TITAN_HTTP_FAILURE_CANCELLED &&
        result_info->failure_reason != TITAN_HTTP_FAILURE_TIMEOUT)
        TitanHttp_Trace(options, TITAN_HTTP_TRACE_TLS_ENGINE_ERROR, -tls_error);
    TitanHttp_Trace(options, TITAN_HTTP_TRACE_TLS_END, tls_error == BR_ERR_OK ? -1 : -tls_error);
    if (transport->fd >= 0) lwip_close(transport->fd);
    memset(transport, 0, sizeof(*transport));
    transport->fd = -1;
    return 0;
}

static void TitanHttp_TransportClose(TitanHttpTransport *transport)
{
    if (transport == 0) return;
    if (transport->fd >= 0) lwip_close(transport->fd);
    memset(transport, 0, sizeof(*transport));
    transport->fd = -1;
}

static int TitanHttp_HeaderValue(const char *header, const char *name, char *value, unsigned int capacity);

static void TitanHttp_CloseConnectionSlot(
    TitanHttpConnectionSlot *slot,
    const TitanHttpClientOptions *options,
    int reason
)
{
    if (slot == 0 || !slot->valid) return;
    TitanHttp_Trace(options, TITAN_HTTP_TRACE_CONNECTION_CLOSE, reason);
    TitanHttp_TransportClose(&slot->transport);
    memset(slot, 0, sizeof(*slot));
    slot->transport.fd = -1;
}

static void TitanHttp_CloseTransportConnection(
    TitanHttpScratch *scratch,
    TitanHttpTransport *transport,
    const TitanHttpClientOptions *options,
    int reason
)
{
    unsigned int i;
    if (scratch == 0 || transport == 0) return;
    for (i = 0u; i < TITAN_HTTP_CONNECTION_SLOTS; i++)
    {
        if (&scratch->connections[i].transport != transport) continue;
        TitanHttp_CloseConnectionSlot(&scratch->connections[i], options, reason);
        return;
    }
}

static void TitanHttp_TouchTransportConnection(
    TitanHttpScratch *scratch,
    TitanHttpTransport *transport
)
{
    unsigned int i;
    if (scratch == 0 || transport == 0) return;
    for (i = 0u; i < TITAN_HTTP_CONNECTION_SLOTS; i++)
    {
        if (&scratch->connections[i].transport == transport && scratch->connections[i].valid)
        {
            scratch->connections[i].last_used_ms = TitanTime_NowMilliseconds();
            return;
        }
    }
}

static TitanHttpTransport *TitanHttp_AcquireConnection(
    TitanHttpScratch *scratch,
    const TitanHttpUrl *url,
    const TitanHttpClientOptions *options,
    TitanHttpResultInfo *result_info,
    int *out_reused
)
{
    unsigned int i;
    unsigned int slot_index = 0u;
    uint64_t oldest = UINT64_MAX;
    TitanHttpConnectionSlot *slot = 0;
    if (out_reused != 0) *out_reused = 0;
    if (scratch == 0 || url == 0) return 0;

    for (i = 0u; i < TITAN_HTTP_CONNECTION_SLOTS; i++)
    {
        slot = &scratch->connections[i];
        if (!slot->valid) continue;
        if (slot->transport.fd >= 0 && slot->scheme == url->scheme &&
            slot->port == url->port && strcmp(slot->host, url->host) == 0)
        {
            slot->transport.options = options;
            slot->transport.result_info = result_info;
            slot->transport.deadline_at_ms = TitanHttp_DeadlineFromOptions(options);
            slot->last_used_ms = TitanTime_NowMilliseconds();
            if (out_reused != 0) *out_reused = 1;
            TitanHttp_Trace(options, TITAN_HTTP_TRACE_CONNECTION_REUSE, 1);
            return &slot->transport;
        }
        if (slot->last_used_ms < oldest)
        {
            oldest = slot->last_used_ms;
            slot_index = i;
        }
    }

    for (i = 0u; i < TITAN_HTTP_CONNECTION_SLOTS; i++)
    {
        if (!scratch->connections[i].valid)
        {
            slot_index = i;
            break;
        }
    }

    slot = &scratch->connections[slot_index];
    if (slot->valid) TitanHttp_CloseConnectionSlot(slot, options, 1);
    memset(slot, 0, sizeof(*slot));
    slot->transport.fd = -1;
    if (!TitanHttp_TransportOpen(&slot->transport, url, options, result_info))
    {
        memset(slot, 0, sizeof(*slot));
        slot->transport.fd = -1;
        return 0;
    }

    slot->scheme = url->scheme;
    slot->port = url->port;
    strncpy(slot->host, url->host, sizeof(slot->host) - 1u);
    slot->last_used_ms = TitanTime_NowMilliseconds();
    slot->valid = 1;
    return &slot->transport;
}

static int TitanHttp_HeaderTokenEquals(const char *value, const char *token)
{
    unsigned int token_len;
    const char *p;
    if (value == 0 || token == 0) return 0;
    token_len = (unsigned int)strlen(token);
    p = value;
    while (*p != '\0')
    {
        const char *begin;
        const char *end;
        unsigned int i;
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        begin = p;
        while (*p != '\0' && *p != ',') p++;
        end = p;
        while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) end--;
        if ((unsigned int)(end - begin) == token_len)
        {
            for (i = 0u; i < token_len; i++)
                if (tolower((unsigned char)begin[i]) != tolower((unsigned char)token[i])) break;
            if (i == token_len) return 1;
        }
        if (*p == ',') p++;
    }
    return 0;
}

static int TitanHttp_ResponseCanReuse(const char *header, int status)
{
    char value[128];
    if (header == 0) return 0;
    if (TitanHttp_HeaderValue(header, "Connection", value, sizeof(value)) &&
        TitanHttp_HeaderTokenEquals(value, "close")) return 0;
    if (status == 204 || status == 304) return 1;
    if (TitanHttp_HeaderValue(header, "Content-Length", value, sizeof(value))) return 1;
    if (TitanHttp_HeaderValue(header, "Transfer-Encoding", value, sizeof(value)) &&
        TitanHttp_HeaderTokenEquals(value, "chunked")) return 1;
    return 0;
}

static int TitanHttp_TransportWrite(TitanHttpTransport *transport, const void *data, unsigned int size)
{
    if (transport == 0 || data == 0 || TitanHttp_TransportShouldCancel(transport)) return 0;
    if (transport->tls)
    {
        if (br_sslio_write_all(&transport->ioc, data, size) != 0) return 0;
        if (TitanHttp_TransportShouldCancel(transport)) return 0;
        return br_sslio_flush(&transport->ioc) == 0;
    }
    {
        unsigned int sent = 0u;
        const unsigned char *p = (const unsigned char *)data;
        while (sent < size)
        {
            unsigned int request = size - sent;
            int n;
            if (TitanHttp_TransportShouldCancel(transport)) return 0;
            if (request > TITAN_HTTP_IO_CHUNK) request = TITAN_HTTP_IO_CHUNK;
            n = TitanHttp_SocketSend(transport, p + sent, request);
            if (n <= 0) return 0;
            sent += (unsigned int)n;
        }
    }
    return 1;
}

static int TitanHttp_TransportRead(TitanHttpTransport *transport, void *data, unsigned int size)
{
    unsigned int request;
    int n;
    if (transport == 0 || data == 0 || size == 0u || TitanHttp_TransportShouldCancel(transport)) return -1;
    request = size > TITAN_HTTP_IO_CHUNK ? TITAN_HTTP_IO_CHUNK : size;
    if (transport->tls)
    {
        n = br_sslio_read(&transport->ioc, data, request);
        if (n >= 0) return n;
        TitanHttp_ResultStopReason(transport);
        n = br_ssl_engine_last_error(&transport->ssl.eng);
        if (n != BR_ERR_OK && transport->result_info != 0 &&
            transport->result_info->failure_reason != TITAN_HTTP_FAILURE_CANCELLED &&
            transport->result_info->failure_reason != TITAN_HTTP_FAILURE_TIMEOUT)
        {
            TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_TLS);
            TitanHttp_Trace(transport->options, TITAN_HTTP_TRACE_TLS_ENGINE_ERROR, -n);
        }
        return n == BR_ERR_OK ? 0 : -1;
    }
    return TitanHttp_SocketRecv(transport, data, request);
}

static int TitanHttp_ReadExact(TitanHttpTransport *transport, unsigned char *destination,
    unsigned int size, const TitanHttpClientOptions *options)
{
    unsigned int got = 0u;
    while (got < size)
    {
        unsigned int request = size - got;
        int n;
        if (TitanHttp_ShouldCancel(options)) return 0;
        if (request > TITAN_HTTP_IO_CHUNK) request = TITAN_HTTP_IO_CHUNK;
        n = TitanHttp_TransportRead(transport, destination + got, request);
        if (n <= 0) return 0;
        got += (unsigned int)n;
        TitanHttp_Yield(options);
    }
    return 1;
}

static int TitanHttp_ReadPayloadExact(TitanHttpTransport *transport, unsigned char *destination,
    unsigned int size, unsigned int *received, unsigned int total_hint,
    const TitanHttpClientOptions *options)
{
    unsigned int got = 0u;
    while (got < size)
    {
        unsigned int request = size - got;
        unsigned char *target;
        int n;
        if (TitanHttp_ShouldCancel(options)) return 0;
        if (request > TITAN_HTTP_IO_CHUNK) request = TITAN_HTTP_IO_CHUNK;
        target = options != 0 && options->body_write != 0
            ? g_http_scratch.body_chunk : destination + got;
        n = TitanHttp_TransportRead(transport, target, request);
        if (n <= 0)
        {
            TitanHttp_ResultStopReason(transport);
            if (transport->result_info == 0 || transport->result_info->failure_reason == TITAN_HTTP_FAILURE_NONE)
                TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_TRUNCATED);
            return 0;
        }
        if (options != 0 && options->body_write != 0 &&
            !options->body_write(options->user_data, target, (unsigned int)n))
        {
            TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_CANCELLED);
            return 0;
        }
        got += (unsigned int)n;
        if (transport->result_info != 0) transport->result_info->bytes_received += (unsigned int)n;
        if (received != 0)
        {
            *received += (unsigned int)n;
            TitanHttp_Progress(options, *received, total_hint);
        }
        TitanHttp_Yield(options);
    }
    return 1;
}

static int TitanHttp_ReadLine(TitanHttpTransport *transport, char *line, unsigned int capacity,
    const TitanHttpClientOptions *options)
{
    unsigned int used = 0u;
    if (line == 0 || capacity < 3u) return 0;
    while (used + 1u < capacity)
    {
        unsigned char c;
        int n;
        if (TitanHttp_ShouldCancel(options)) return 0;
        n = TitanHttp_TransportRead(transport, &c, 1u);
        if (n <= 0) return 0;
        line[used++] = (char)c;
        line[used] = '\0';
        if (used >= 2u && line[used - 2u] == '\r' && line[used - 1u] == '\n') return 1;
    }
    return 0;
}

static int TitanHttp_HeaderValue(const char *header, const char *name, char *value, unsigned int capacity)
{
    unsigned int name_len = (unsigned int)strlen(name);
    const char *line = header;
    while (line != 0 && *line != '\0')
    {
        const char *end = strstr(line, "\r\n");
        const char *p;
        unsigned int i;
        if (end == 0) end = line + strlen(line);
        if ((unsigned int)(end - line) > name_len && line[name_len] == ':')
        {
            int match = 1;
            for (i = 0u; i < name_len; i++)
                if (tolower((unsigned char)line[i]) != tolower((unsigned char)name[i])) { match = 0; break; }
            if (match)
            {
                p = line + name_len + 1u;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                while (end > p && (end[-1] == ' ' || end[-1] == '\t')) end--;
                return TitanHttp_CopyRange(value, capacity, p, end);
            }
        }
        if (*end == '\0') break;
        line = end + 2u;
    }
    return 0;
}

static int TitanHttp_BuildRedirect(const TitanHttpUrl *base, const char *location,
    char *destination, unsigned int capacity)
{
    const char *scheme;
    int n;
    if (strncmp(location, "http://", 7u) == 0 || strncmp(location, "https://", 8u) == 0)
    {
        /* Never downgrade a provider that started under TLS. */
        if (base->scheme == TITAN_HTTP_SCHEME_HTTPS && strncmp(location, "http://", 7u) == 0)
            return 0;
        if (strlen(location) + 1u > capacity) return 0;
        strcpy(destination, location);
        return 1;
    }
    if (location[0] != '/') return 0;
    scheme = base->scheme == TITAN_HTTP_SCHEME_HTTPS ? "https" : "http";
    if ((base->scheme == TITAN_HTTP_SCHEME_HTTPS && base->port == 443u) ||
        (base->scheme == TITAN_HTTP_SCHEME_HTTP && base->port == 80u))
        n = snprintf(destination, capacity, "%s://%s%s", scheme, base->host, location);
    else
        n = snprintf(destination, capacity, "%s://%s:%u%s", scheme, base->host, base->port, location);
    return n >= 0 && (unsigned int)n < capacity;
}

static int TitanHttp_ReadBody(TitanHttpTransport *transport, const char *header,
    unsigned char *destination, unsigned int capacity, unsigned int *out_size,
    const TitanHttpClientOptions *options)
{
    char value[128];
    unsigned int used = 0u;
    unsigned int received = 0u;
    TitanHttp_ResultStage(transport->result_info, TITAN_HTTP_STAGE_BODY);
    TitanHttp_Trace(options, TITAN_HTTP_TRACE_BODY_BEGIN, 0);
    TitanHttp_Progress(options, 0u, 0u);
    if (TitanHttp_HeaderValue(header, "Transfer-Encoding", value, sizeof(value)) && strstr(value, "chunked") != 0)
    {
        for (;;)
        {
            char line[TITAN_HTTP_LINE_CAPACITY];
            char *endptr;
            unsigned long chunk;
            unsigned char crlf[2];
            if (!TitanHttp_ReadLine(transport, line, sizeof(line), options)) return 0;
            chunk = strtoul(line, &endptr, 16);
            if (endptr == line) return 0;
            while (*endptr == ' ' || *endptr == '\t') endptr++;
            if (*endptr != '\r' && *endptr != ';') return 0;
            if (chunk == 0u)
            {
                do { if (!TitanHttp_ReadLine(transport, line, sizeof(line), options)) return 0; }
                while (strcmp(line, "\r\n") != 0);
                break;
            }
            if (chunk > (unsigned long)UINT_MAX - (unsigned long)used)
            { TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_TOO_LARGE); return 0; }
            if ((options == 0 || options->body_write == 0) && chunk > capacity - used)
            { TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_TOO_LARGE); return 0; }
            if (!TitanHttp_ReadPayloadExact(transport,
                    destination != 0 ? destination + used : 0, (unsigned int)chunk,
                    &received, 0u, options)) return 0;
            used += (unsigned int)chunk;
            if (!TitanHttp_ReadExact(transport, crlf, 2u, options) || crlf[0] != '\r' || crlf[1] != '\n') return 0;
        }
    }
    else if (TitanHttp_HeaderValue(header, "Content-Length", value, sizeof(value)))
    {
        char *endptr;
        unsigned long length = strtoul(value, &endptr, 10);
        if (endptr == value) return 0;
        while (*endptr == ' ' || *endptr == '\t') endptr++;
        if (*endptr != '\0') { TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_PROTOCOL); return 0; }
        if (length > (unsigned long)UINT_MAX) { TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_TOO_LARGE); return 0; }
        if ((options == 0 || options->body_write == 0) && length > capacity) { TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_TOO_LARGE); return 0; }
        if (transport->result_info != 0) transport->result_info->content_length = (unsigned int)length;
        TitanHttp_Progress(options, 0u, (unsigned int)length);
        if (length != 0u && !TitanHttp_ReadPayloadExact(transport, destination, (unsigned int)length,
                &received, (unsigned int)length, options)) return 0;
        used = (unsigned int)length;
    }
    else
    {
        for (;;)
        {
            unsigned int request;
            unsigned char *target;
            int n;
            if (TitanHttp_ShouldCancel(options)) return 0;
            if (options != 0 && options->body_write != 0)
            {
                request = TITAN_HTTP_IO_CHUNK;
                target = g_http_scratch.body_chunk;
            }
            else
            {
                if (used >= capacity)
                {
                    unsigned char extra;
                    n = TitanHttp_TransportRead(transport, &extra, 1u);
                    if (n > 0) { TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_TOO_LARGE); return 0; }
                    break;
                }
                request = capacity - used;
                if (request > TITAN_HTTP_IO_CHUNK) request = TITAN_HTTP_IO_CHUNK;
                target = destination + used;
            }
            n = TitanHttp_TransportRead(transport, target, request);
            if (n < 0) return 0;
            if (n == 0) break;
            if (options != 0 && options->body_write != 0 &&
                !options->body_write(options->user_data, target, (unsigned int)n))
            {
                TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_CANCELLED);
                return 0;
            }
            if ((unsigned int)n > UINT_MAX - used)
            { TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_TOO_LARGE); return 0; }
            used += (unsigned int)n;
            if (transport->result_info != 0)
            {
                if ((unsigned int)n > UINT_MAX - transport->result_info->bytes_received)
                { TitanHttp_ResultFailure(transport->result_info, TITAN_HTTP_FAILURE_TOO_LARGE); return 0; }
                transport->result_info->bytes_received += (unsigned int)n;
            }
            received = used;
            TitanHttp_Progress(options, received, 0u);
            TitanHttp_Yield(options);
        }
    }
    if (out_size != 0) *out_size = used;
    if (transport->result_info != 0 && transport->result_info->content_length == 0u)
        transport->result_info->bytes_received = used;
    TitanHttp_Trace(options, TITAN_HTTP_TRACE_BODY_END, 1);
    return 1;
}

static int TitanHttp_GetOnce(TitanHttpScratch *scratch, const char *url_text,
    unsigned char *destination, unsigned int capacity, unsigned int *out_size,
    int *out_status, char *redirect, unsigned int redirect_capacity,
    const TitanHttpClientOptions *options, TitanHttpResultInfo *result_info)
{
    TitanHttpUrl *url;
    TitanHttpTransport *transport = 0;
    char *request;
    char *header;
    char *location;
    unsigned int used = 0u;
    unsigned int reused_retries = 0u;
    int status = 0;
    int n;
    int ok = 0;
    int keep_connection = 0;
    int reused = 0;

    if (scratch == 0 || !TitanHttp_ParseUrl(url_text, &scratch->url))
    { TitanHttp_ResultFailure(result_info, TITAN_HTTP_FAILURE_PROTOCOL); return 0; }
    if (TitanHttp_ShouldCancel(options))
    { TitanHttp_ResultFailure(result_info, TITAN_HTTP_FAILURE_CANCELLED); return 0; }

    url = &scratch->url;
    request = scratch->request;
    header = scratch->header;
    location = scratch->location;
    n = snprintf(request, sizeof(scratch->request),
        "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nAccept: image/png,image/jpeg,*/*;q=0.2\r\nAccept-Encoding: identity\r\nConnection: keep-alive\r\n\r\n",
        url->path, url->host, TITAN_HTTP_USER_AGENT);
    if (n < 0 || (unsigned int)n >= sizeof(scratch->request))
    {
        TitanHttp_ResultFailure(result_info, TITAN_HTTP_FAILURE_PROTOCOL);
        return 0;
    }

acquire_connection:
    used = 0u;
    status = 0;
    ok = 0;
    keep_connection = 0;
    header[0] = '\0';
    location[0] = '\0';
    if (out_size != 0) *out_size = 0u;
    if (out_status != 0) *out_status = 0;
    transport = TitanHttp_AcquireConnection(scratch, url, options, result_info, &reused);
    if (transport == 0) return 0;

    if (!TitanHttp_TransportWrite(transport, request, (unsigned int)n))
    {
        if (reused && reused_retries < TITAN_HTTP_REUSED_RETRY_LIMIT &&
            !TitanHttp_TransportShouldCancel(transport))
        {
            reused_retries++;
            TitanHttp_CloseTransportConnection(scratch, transport, options, -2);
            TitanHttp_ResultInit(result_info);
            goto acquire_connection;
        }
        goto done;
    }

    TitanHttp_ResultStage(result_info, TITAN_HTTP_STAGE_HEADERS);
    TitanHttp_Trace(options, TITAN_HTTP_TRACE_HEADERS_BEGIN, 0);
    while (used + 1u < sizeof(scratch->header))
    {
        unsigned char c;
        int got;
        if (TitanHttp_ShouldCancel(options) || TitanHttp_DeadlineExpired(transport->deadline_at_ms)) goto done;
        got = TitanHttp_TransportRead(transport, &c, 1u);
        if (got <= 0)
        {
            if (reused && used == 0u && reused_retries < TITAN_HTTP_REUSED_RETRY_LIMIT &&
                !TitanHttp_TransportShouldCancel(transport))
            {
                reused_retries++;
                TitanHttp_CloseTransportConnection(scratch, transport, options, -3);
                TitanHttp_ResultInit(result_info);
                goto acquire_connection;
            }
            goto done;
        }
        header[used++] = (char)c;
        header[used] = '\0';
        if (used >= 4u && memcmp(&header[used - 4u], "\r\n\r\n", 4u) == 0) break;
    }

    if (used < 4u || memcmp(&header[used - 4u], "\r\n\r\n", 4u) != 0) goto protocol_fail;
    TitanHttp_Trace(options, TITAN_HTTP_TRACE_HEADERS_END, 1);
    if (sscanf(header, "HTTP/%*u.%*u %d", &status) != 1 || status < 100 || status > 599)
        goto protocol_fail;
    if (out_status != 0) *out_status = status;

    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
    {
        unsigned int discard_size = 0u;
        TitanHttpClientOptions discard_options;
        const TitanHttpClientOptions *discard_opts = options;
        unsigned char *discard_buffer = destination;
        unsigned int discard_capacity = capacity;
        if (options != 0 && options->body_write != 0)
        {
            discard_options = *options; discard_options.body_write = 0; discard_opts = &discard_options;
            discard_buffer = scratch->body_chunk; discard_capacity = sizeof(scratch->body_chunk);
        }
        if (redirect == 0 || !TitanHttp_HeaderValue(header, "Location", location, TITAN_HTTP_URL_CAPACITY) ||
            !TitanHttp_BuildRedirect(url, location, redirect, redirect_capacity)) goto protocol_fail;
        if (TitanHttp_ResponseCanReuse(header, status))
        {
            if (status == 304 ||
                TitanHttp_ReadBody(transport, header, discard_buffer, discard_capacity, &discard_size, discard_opts))
                keep_connection = 1;
        }
        ok = 1;
        goto done;
    }

    if (status != 200)
    {
        unsigned int discard_size = 0u;
        TitanHttpClientOptions discard_options;
        const TitanHttpClientOptions *discard_opts = options;
        unsigned char *discard_buffer = destination;
        unsigned int discard_capacity = capacity;
        if (options != 0 && options->body_write != 0)
        {
            discard_options = *options; discard_options.body_write = 0; discard_opts = &discard_options;
            discard_buffer = scratch->body_chunk; discard_capacity = sizeof(scratch->body_chunk);
        }
        /* Error pages are not image assets. Consume only their small framed
         * body so the same TLS/TCP connection can service the next Game-ID
         * lookup. This avoids one TCP PCB/TIME_WAIT cycle per missing provider.
         * If framing is unsuitable, close only this pooled connection and still
         * return the HTTP status to the caller. */
        if (TitanHttp_ResponseCanReuse(header, status))
        {
            if (status == 204 || status == 304 ||
                TitanHttp_ReadBody(transport, header, discard_buffer, discard_capacity, &discard_size, discard_opts))
                keep_connection = 1;
        }
        ok = 1;
        goto done;
    }

    if (!TitanHttp_ReadBody(transport, header, destination, capacity, out_size, options)) goto done;
    keep_connection = TitanHttp_ResponseCanReuse(header, status);
    ok = 1;
    goto done;

protocol_fail:
    TitanHttp_ResultFailure(result_info, TITAN_HTTP_FAILURE_PROTOCOL);
    TitanHttp_Trace(options, TITAN_HTTP_TRACE_PROTOCOL_ERROR, -1);
done:
    if (!ok)
    {
        TitanHttp_ResultStopReason(transport);
        if (result_info != 0 && result_info->failure_reason == TITAN_HTTP_FAILURE_NONE)
            TitanHttp_ResultFailure(result_info, TITAN_HTTP_FAILURE_PROTOCOL);
    }
    if (ok && keep_connection)
        TitanHttp_TouchTransportConnection(scratch, transport);
    else
        TitanHttp_CloseTransportConnection(scratch, transport, options, ok ? 0 : -1);
    return ok;
}

int TitanHttpClient_GetEx(const char *url, unsigned char *destination, unsigned int capacity,
    unsigned int *out_size, int *out_status, const TitanHttpClientOptions *options,
    TitanHttpResultInfo *result_info)
{
    TitanHttpScratch *scratch = &g_http_scratch;
    unsigned int redirects;
    unsigned int max_redirects = options != 0 && options->maximum_redirects != 0u
        ? options->maximum_redirects : TITAN_HTTP_DEFAULT_REDIRECTS;
    int result = 0;

    TitanHttp_ResultInit(result_info);
    if (url == 0 || ((destination == 0 || capacity == 0u) &&
        (options == 0 || options->body_write == 0)) ||
        strlen(url) + 1u > sizeof(scratch->current))
    { TitanHttp_ResultFailure(result_info, TITAN_HTTP_FAILURE_PROTOCOL); return 0; }
    if (g_http_scratch_busy)
    { TitanHttp_ResultFailure(result_info, TITAN_HTTP_FAILURE_CANCELLED); return 0; }
    g_http_scratch_busy = 1;
    if (max_redirects > TITAN_HTTP_MAX_REDIRECTS) max_redirects = TITAN_HTTP_MAX_REDIRECTS;
    strcpy(scratch->current, url);
    if (out_size != 0) *out_size = 0u;
    if (out_status != 0) *out_status = 0;

    for (redirects = 0u; redirects <= max_redirects; redirects++)
    {
        int status = 0;
        unsigned int size = 0u;
        scratch->redirect[0] = '\0';
        if (!TitanHttp_GetOnce(scratch, scratch->current, destination, capacity, &size, &status,
                scratch->redirect, sizeof(scratch->redirect), options, result_info)) goto done;
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
        {
            if (redirects == max_redirects || scratch->redirect[0] == '\0') goto done;
            strcpy(scratch->current, scratch->redirect);
            continue;
        }
        if (out_size != 0) *out_size = size;
        if (out_status != 0) *out_status = status;
        result = 1;
        break;
    }
done:
    g_http_scratch_busy = 0;
    return result;
}

int TitanHttpClient_Get(const char *url, unsigned char *destination, unsigned int capacity,
    unsigned int *out_size, int *out_status, const TitanHttpClientOptions *options)
{
    return TitanHttpClient_GetEx(url, destination, capacity, out_size, out_status, options, 0);
}

void TitanHttpClient_ResetSession(void)
{
    unsigned int i;
    if (g_http_scratch_busy) return;
    for (i = 0u; i < TITAN_HTTP_CONNECTION_SLOTS; i++)
        TitanHttp_CloseConnectionSlot(&g_http_scratch.connections[i], 0, 2);
    memset(g_http_scratch.dns_cache, 0, sizeof(g_http_scratch.dns_cache));
}
#else
int TitanHttpClient_GetEx(const char *url, unsigned char *destination, unsigned int capacity,
    unsigned int *out_size, int *out_status, const TitanHttpClientOptions *options,
    TitanHttpResultInfo *result_info)
{
    (void)url; (void)destination; (void)capacity; (void)options;
    if (out_size != 0) *out_size = 0u;
    if (out_status != 0) *out_status = 0;
    if (result_info != 0)
    {
        memset(result_info, 0, sizeof(*result_info));
        result_info->failure_reason = TITAN_HTTP_FAILURE_PROTOCOL;
    }
    return 0;
}

int TitanHttpClient_Get(const char *url, unsigned char *destination, unsigned int capacity,
    unsigned int *out_size, int *out_status, const TitanHttpClientOptions *options)
{
    return TitanHttpClient_GetEx(url, destination, capacity, out_size, out_status, options, 0);
}

void TitanHttpClient_ResetSession(void)
{
}
#endif

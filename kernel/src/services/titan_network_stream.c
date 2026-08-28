#include "titan_network_stream.h"

#include <string.h>

#if defined(__EE__) || defined(_EE)
#include <errno.h>
#include <ps2ip.h>
#include <sys/select.h>
#endif

#define TITAN_NETWORK_STREAM_LISTENER_SLOTS 2u
#define TITAN_NETWORK_STREAM_CONNECTION_SLOTS 4u
#define TITAN_NETWORK_STREAM_MAX_BACKLOG 4u
#define TITAN_NETWORK_STREAM_DEFAULT_DEADLINE_MS 5000u
#define TITAN_NETWORK_STREAM_MAX_DEADLINE_MS 30000u
#define TITAN_NETWORK_STREAM_HANDLE_LISTENER 0x10000000u
#define TITAN_NETWORK_STREAM_HANDLE_STREAM 0x20000000u
#define TITAN_NETWORK_STREAM_HANDLE_TYPE_MASK 0xF0000000u
#define TITAN_NETWORK_STREAM_HANDLE_GENERATION_MASK 0x0FFFFF00u
#define TITAN_NETWORK_STREAM_HANDLE_SLOT_MASK 0x000000FFu

#if defined(TITAN_NETWORK_STREAM_TEST)
int TitanNetworkStreamPlatform_Listen(unsigned short port, unsigned int backlog);
int TitanNetworkStreamPlatform_Accept(int listener_fd);
int TitanNetworkStreamPlatform_WaitReadable(int fd, unsigned int timeout_ms);
int TitanNetworkStreamPlatform_WaitWritable(int fd, unsigned int timeout_ms);
int TitanNetworkStreamPlatform_Read(int fd, void *buffer, unsigned int capacity);
int TitanNetworkStreamPlatform_Write(int fd, const void *buffer, unsigned int size);
void TitanNetworkStreamPlatform_Close(int fd);
#else
static int TitanNetworkStreamPlatform_Listen(unsigned short port, unsigned int backlog)
{
#if defined(__EE__) || defined(_EE)
    struct sockaddr_in address;
    int fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    int reuse = 1;
    if (fd < 0) return -1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(fd, (int)backlog) < 0)
    {
        lwip_close(fd);
        return -1;
    }
    return fd;
#else
    (void)port; (void)backlog;
    return -1;
#endif
}

static int TitanNetworkStreamPlatform_Accept(int listener_fd)
{
#if defined(__EE__) || defined(_EE)
    return accept(listener_fd, 0, 0);
#else
    (void)listener_fd;
    return -1;
#endif
}

static int TitanNetworkStreamPlatform_Wait(int fd, unsigned int timeout_ms, int write_ready)
{
#if defined(__EE__) || defined(_EE)
    fd_set set;
    struct timeval tv;
    int result;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    tv.tv_sec = (long)(timeout_ms / 1000u);
    tv.tv_usec = (long)((timeout_ms % 1000u) * 1000u);
    result = select(fd + 1, write_ready ? 0 : &set, write_ready ? &set : 0, 0, &tv);
    return result > 0 ? 1 : (result == 0 ? 0 : -1);
#else
    (void)fd; (void)timeout_ms; (void)write_ready;
    return -1;
#endif
}

static int TitanNetworkStreamPlatform_WaitReadable(int fd, unsigned int timeout_ms)
{
    return TitanNetworkStreamPlatform_Wait(fd, timeout_ms, 0);
}

static int TitanNetworkStreamPlatform_WaitWritable(int fd, unsigned int timeout_ms)
{
    return TitanNetworkStreamPlatform_Wait(fd, timeout_ms, 1);
}

static int TitanNetworkStreamPlatform_Read(int fd, void *buffer, unsigned int capacity)
{
#if defined(__EE__) || defined(_EE)
    return recv(fd, buffer, (int)capacity, 0);
#else
    (void)fd; (void)buffer; (void)capacity;
    return -1;
#endif
}

static int TitanNetworkStreamPlatform_Write(int fd, const void *buffer, unsigned int size)
{
#if defined(__EE__) || defined(_EE)
    return send(fd, buffer, (int)size, 0);
#else
    (void)fd; (void)buffer; (void)size;
    return -1;
#endif
}

static void TitanNetworkStreamPlatform_Close(int fd)
{
#if defined(__EE__) || defined(_EE)
    if (fd >= 0) lwip_close(fd);
#else
    (void)fd;
#endif
}
#endif

typedef struct TitanNetworkStreamSlot
{
    int fd;
    unsigned int generation;
    TitanNetworkStreamOwner owner;
    int active;
    int cancelled;
} TitanNetworkStreamSlot;

static TitanNetworkStreamSlot g_listeners[TITAN_NETWORK_STREAM_LISTENER_SLOTS];
static TitanNetworkStreamSlot g_streams[TITAN_NETWORK_STREAM_CONNECTION_SLOTS];
static TitanNetworkStreamSnapshot g_snapshot;
static unsigned int g_generation = 1u;

static unsigned int TitanNetworkStream_ClampDeadline(unsigned int deadline_ms)
{
    if (deadline_ms == 0u) return TITAN_NETWORK_STREAM_DEFAULT_DEADLINE_MS;
    if (deadline_ms > TITAN_NETWORK_STREAM_MAX_DEADLINE_MS)
        return TITAN_NETWORK_STREAM_MAX_DEADLINE_MS;
    return deadline_ms;
}

static unsigned int TitanNetworkStream_NextGeneration(void)
{
    unsigned int value = g_generation++ & 0x000FFFFFu;
    if (value == 0u) value = g_generation++ & 0x000FFFFFu;
    return value == 0u ? 1u : value;
}

static TitanNetworkStreamHandle TitanNetworkStream_MakeHandle(
    unsigned int type,
    unsigned int slot,
    unsigned int generation
)
{
    return type | ((generation & 0x000FFFFFu) << 8) | ((slot + 1u) & TITAN_NETWORK_STREAM_HANDLE_SLOT_MASK);
}

static TitanNetworkStreamSlot *TitanNetworkStream_Resolve(
    TitanNetworkStreamHandle handle,
    unsigned int type
)
{
    TitanNetworkStreamSlot *slot;
    unsigned int index;
    unsigned int generation;
    if ((handle & TITAN_NETWORK_STREAM_HANDLE_TYPE_MASK) != type) return 0;
    index = handle & TITAN_NETWORK_STREAM_HANDLE_SLOT_MASK;
    if (index == 0u) return 0;
    index--;
    generation = (handle & TITAN_NETWORK_STREAM_HANDLE_GENERATION_MASK) >> 8;
    if (type == TITAN_NETWORK_STREAM_HANDLE_LISTENER)
    {
        if (index >= TITAN_NETWORK_STREAM_LISTENER_SLOTS) return 0;
        slot = &g_listeners[index];
    }
    else
    {
        if (index >= TITAN_NETWORK_STREAM_CONNECTION_SLOTS) return 0;
        slot = &g_streams[index];
    }
    if (!slot->active || slot->generation != generation) return 0;
    return slot;
}

static void TitanNetworkStream_CloseSlot(TitanNetworkStreamSlot *slot)
{
    if (slot == 0 || !slot->active) return;
    TitanNetworkStreamPlatform_Close(slot->fd);
    memset(slot, 0, sizeof(*slot));
    slot->fd = -1;
}

int TitanNetworkStream_Listen(
    TitanNetworkStreamOwner owner,
    unsigned short port,
    unsigned int backlog,
    TitanNetworkStreamHandle *out_listener
)
{
    unsigned int i;
    int fd;
    if (out_listener != 0) *out_listener = TITAN_NETWORK_STREAM_INVALID_HANDLE;
    if (owner == TITAN_NETWORK_STREAM_OWNER_NONE || port == 0u || out_listener == 0) return 0;
    if (backlog == 0u) backlog = 1u;
    if (backlog > TITAN_NETWORK_STREAM_MAX_BACKLOG) backlog = TITAN_NETWORK_STREAM_MAX_BACKLOG;
    for (i = 0u; i < TITAN_NETWORK_STREAM_LISTENER_SLOTS; ++i)
    {
        if (!g_listeners[i].active) break;
    }
    if (i == TITAN_NETWORK_STREAM_LISTENER_SLOTS)
    {
        g_snapshot.rejected_capacity++;
        return 0;
    }
    fd = TitanNetworkStreamPlatform_Listen(port, backlog);
    if (fd < 0) return 0;
    memset(&g_listeners[i], 0, sizeof(g_listeners[i]));
    g_listeners[i].fd = fd;
    g_listeners[i].generation = TitanNetworkStream_NextGeneration();
    g_listeners[i].owner = owner;
    g_listeners[i].active = 1;
    *out_listener = TitanNetworkStream_MakeHandle(
        TITAN_NETWORK_STREAM_HANDLE_LISTENER, i, g_listeners[i].generation);
    g_snapshot.listeners_opened++;
    g_snapshot.active_listeners++;
    return 1;
}

int TitanNetworkStream_Accept(
    TitanNetworkStreamHandle listener,
    unsigned int deadline_ms,
    TitanNetworkStreamHandle *out_stream
)
{
    TitanNetworkStreamSlot *listen_slot;
    unsigned int i;
    int wait_result;
    int fd;
    if (out_stream != 0) *out_stream = TITAN_NETWORK_STREAM_INVALID_HANDLE;
    if (out_stream == 0) return 0;
    listen_slot = TitanNetworkStream_Resolve(listener, TITAN_NETWORK_STREAM_HANDLE_LISTENER);
    if (listen_slot == 0 || listen_slot->cancelled) return 0;
    for (i = 0u; i < TITAN_NETWORK_STREAM_CONNECTION_SLOTS; ++i)
    {
        if (!g_streams[i].active) break;
    }
    if (i == TITAN_NETWORK_STREAM_CONNECTION_SLOTS)
    {
        g_snapshot.rejected_capacity++;
        return 0;
    }
    wait_result = TitanNetworkStreamPlatform_WaitReadable(
        listen_slot->fd, TitanNetworkStream_ClampDeadline(deadline_ms));
    if (listen_slot->cancelled)
    {
        g_snapshot.operations_cancelled++;
        return 0;
    }
    if (wait_result == 0)
    {
        g_snapshot.waits_timed_out++;
        return 0;
    }
    if (wait_result < 0) return 0;
    fd = TitanNetworkStreamPlatform_Accept(listen_slot->fd);
    if (fd < 0) return 0;
    memset(&g_streams[i], 0, sizeof(g_streams[i]));
    g_streams[i].fd = fd;
    g_streams[i].generation = TitanNetworkStream_NextGeneration();
    g_streams[i].owner = listen_slot->owner;
    g_streams[i].active = 1;
    *out_stream = TitanNetworkStream_MakeHandle(
        TITAN_NETWORK_STREAM_HANDLE_STREAM, i, g_streams[i].generation);
    g_snapshot.connections_accepted++;
    g_snapshot.active_streams++;
    return 1;
}

TitanNetworkStreamIoStatus TitanNetworkStream_ReadStatus(
    TitanNetworkStreamHandle stream,
    void *buffer,
    unsigned int capacity,
    unsigned int deadline_ms,
    unsigned int *out_size
)
{
    TitanNetworkStreamSlot *slot = TitanNetworkStream_Resolve(stream, TITAN_NETWORK_STREAM_HANDLE_STREAM);
    int wait_result;
    int result;
    if (out_size != 0) *out_size = 0u;
    if (slot == 0 || buffer == 0 || capacity == 0u || out_size == 0) return TITAN_NETWORK_STREAM_IO_ERROR;
    if (slot->cancelled) return TITAN_NETWORK_STREAM_IO_CANCELLED;
    wait_result = TitanNetworkStreamPlatform_WaitReadable(slot->fd, TitanNetworkStream_ClampDeadline(deadline_ms));
    if (slot->cancelled)
    {
        g_snapshot.operations_cancelled++;
        return TITAN_NETWORK_STREAM_IO_CANCELLED;
    }
    if (wait_result == 0)
    {
        g_snapshot.waits_timed_out++;
        return TITAN_NETWORK_STREAM_IO_IDLE;
    }
    if (wait_result < 0) return TITAN_NETWORK_STREAM_IO_ERROR;
    result = TitanNetworkStreamPlatform_Read(slot->fd, buffer, capacity);
    if (result == 0) return TITAN_NETWORK_STREAM_IO_CLOSED;
    if (result < 0) return TITAN_NETWORK_STREAM_IO_ERROR;
    *out_size = (unsigned int)result;
    g_snapshot.bytes_received += (unsigned int)result;
    return TITAN_NETWORK_STREAM_IO_DATA;
}

int TitanNetworkStream_Read(
    TitanNetworkStreamHandle stream,
    void *buffer,
    unsigned int capacity,
    unsigned int deadline_ms,
    unsigned int *out_size
)
{
    return TitanNetworkStream_ReadStatus(stream, buffer, capacity, deadline_ms, out_size) ==
        TITAN_NETWORK_STREAM_IO_DATA;
}

int TitanNetworkStream_Write(
    TitanNetworkStreamHandle stream,
    const void *buffer,
    unsigned int size,
    unsigned int deadline_ms,
    unsigned int *out_size
)
{
    TitanNetworkStreamSlot *slot = TitanNetworkStream_Resolve(stream, TITAN_NETWORK_STREAM_HANDLE_STREAM);
    const unsigned char *source = (const unsigned char *)buffer;
    unsigned int total = 0u;
    unsigned int timeout = TitanNetworkStream_ClampDeadline(deadline_ms);
    if (out_size != 0) *out_size = 0u;
    if (slot == 0 || slot->cancelled || buffer == 0 || size == 0u || out_size == 0) return 0;
    while (total < size)
    {
        int wait_result = TitanNetworkStreamPlatform_WaitWritable(slot->fd, timeout);
        int result;
        if (slot->cancelled)
        {
            g_snapshot.operations_cancelled++;
            return 0;
        }
        if (wait_result == 0)
        {
            g_snapshot.waits_timed_out++;
            return 0;
        }
        if (wait_result < 0) return 0;
        result = TitanNetworkStreamPlatform_Write(slot->fd, source + total, size - total);
        if (result <= 0) return 0;
        total += (unsigned int)result;
    }
    *out_size = total;
    g_snapshot.bytes_sent += total;
    return 1;
}

void TitanNetworkStream_Close(TitanNetworkStreamHandle handle)
{
    TitanNetworkStreamSlot *slot;
    unsigned int type = handle & TITAN_NETWORK_STREAM_HANDLE_TYPE_MASK;
    if (type == TITAN_NETWORK_STREAM_HANDLE_LISTENER)
    {
        slot = TitanNetworkStream_Resolve(handle, TITAN_NETWORK_STREAM_HANDLE_LISTENER);
        if (slot != 0)
        {
            TitanNetworkStream_CloseSlot(slot);
            if (g_snapshot.active_listeners != 0u) g_snapshot.active_listeners--;
        }
    }
    else if (type == TITAN_NETWORK_STREAM_HANDLE_STREAM)
    {
        slot = TitanNetworkStream_Resolve(handle, TITAN_NETWORK_STREAM_HANDLE_STREAM);
        if (slot != 0)
        {
            TitanNetworkStream_CloseSlot(slot);
            if (g_snapshot.active_streams != 0u) g_snapshot.active_streams--;
        }
    }
}

void TitanNetworkStream_CancelOwner(TitanNetworkStreamOwner owner)
{
    unsigned int i;
    if (owner == TITAN_NETWORK_STREAM_OWNER_NONE) return;
    for (i = 0u; i < TITAN_NETWORK_STREAM_LISTENER_SLOTS; ++i)
        if (g_listeners[i].active && g_listeners[i].owner == owner) g_listeners[i].cancelled = 1;
    for (i = 0u; i < TITAN_NETWORK_STREAM_CONNECTION_SLOTS; ++i)
        if (g_streams[i].active && g_streams[i].owner == owner) g_streams[i].cancelled = 1;
}

void TitanNetworkStream_Reset(void)
{
    unsigned int i;
    for (i = 0u; i < TITAN_NETWORK_STREAM_LISTENER_SLOTS; ++i)
        TitanNetworkStream_CloseSlot(&g_listeners[i]);
    for (i = 0u; i < TITAN_NETWORK_STREAM_CONNECTION_SLOTS; ++i)
        TitanNetworkStream_CloseSlot(&g_streams[i]);
    memset(&g_snapshot, 0, sizeof(g_snapshot));
}

void TitanNetworkStream_GetSnapshot(TitanNetworkStreamSnapshot *snapshot)
{
    if (snapshot != 0) *snapshot = g_snapshot;
}

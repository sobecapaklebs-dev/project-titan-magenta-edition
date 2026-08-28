#include "titan_network_stream.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int g_next_fd = 10;
static int g_wait_readable = 1;
static int g_wait_writable = 1;
static int g_closed[32];
static unsigned char g_rx[] = "USER anonymous\r\n";
static unsigned int g_rx_offset;
static unsigned char g_tx[128];
static unsigned int g_tx_size;

int TitanNetworkStreamPlatform_Listen(unsigned short port, unsigned int backlog)
{
    assert(port == 2121u);
    assert(backlog == 2u);
    return g_next_fd++;
}
int TitanNetworkStreamPlatform_Accept(int listener_fd)
{
    assert(listener_fd == 10);
    return g_next_fd++;
}
int TitanNetworkStreamPlatform_WaitReadable(int fd, unsigned int timeout_ms)
{
    assert(fd >= 10);
    assert(timeout_ms > 0u && timeout_ms <= 30000u);
    return g_wait_readable;
}
int TitanNetworkStreamPlatform_WaitWritable(int fd, unsigned int timeout_ms)
{
    assert(fd >= 10);
    assert(timeout_ms > 0u && timeout_ms <= 30000u);
    return g_wait_writable;
}
int TitanNetworkStreamPlatform_Read(int fd, void *buffer, unsigned int capacity)
{
    unsigned int left;
    unsigned int take;
    assert(fd == 11);
    left = (unsigned int)sizeof(g_rx) - 1u - g_rx_offset;
    if (left == 0u) return 0;
    take = left < capacity ? left : capacity;
    memcpy(buffer, g_rx + g_rx_offset, take);
    g_rx_offset += take;
    return (int)take;
}
int TitanNetworkStreamPlatform_Write(int fd, const void *buffer, unsigned int size)
{
    unsigned int take = size > 4u ? 4u : size; /* prove partial-send loop */
    assert(fd == 11);
    assert(g_tx_size + take <= sizeof(g_tx));
    memcpy(g_tx + g_tx_size, buffer, take);
    g_tx_size += take;
    return (int)take;
}
void TitanNetworkStreamPlatform_Close(int fd)
{
    assert(fd >= 0 && fd < 32);
    g_closed[fd]++;
}

int main(void)
{
    TitanNetworkStreamHandle listener = 0u;
    TitanNetworkStreamHandle stream = 0u;
    TitanNetworkStreamHandle stale;
    TitanNetworkStreamSnapshot snapshot;
    char input[32];
    unsigned int count = 0u;
    const char reply[] = "220 Project Titan FTP ready\r\n";

    TitanNetworkStream_Reset();
    assert(TitanNetworkStream_Listen(TITAN_NETWORK_STREAM_OWNER_FTP, 2121u, 2u, &listener));
    assert(listener != TITAN_NETWORK_STREAM_INVALID_HANDLE);
    assert(TitanNetworkStream_Accept(listener, 1000u, &stream));
    assert(stream != TITAN_NETWORK_STREAM_INVALID_HANDLE);

    memset(input, 0, sizeof(input));
    assert(TitanNetworkStream_Read(stream, input, sizeof(input), 1000u, &count));
    assert(count == sizeof(g_rx) - 1u);
    assert(memcmp(input, g_rx, count) == 0);
    assert(TitanNetworkStream_ReadStatus(stream, input, sizeof(input), 1000u, &count) == TITAN_NETWORK_STREAM_IO_CLOSED);

    assert(TitanNetworkStream_Write(stream, reply, (unsigned int)strlen(reply), 1000u, &count));
    assert(count == strlen(reply));
    assert(g_tx_size == strlen(reply));
    assert(memcmp(g_tx, reply, g_tx_size) == 0);

    g_wait_readable = 0;
    count = 99u;
    assert(!TitanNetworkStream_Read(stream, input, sizeof(input), 1u, &count));
    assert(count == 0u);
    g_wait_readable = 1;

    TitanNetworkStream_CancelOwner(TITAN_NETWORK_STREAM_OWNER_FTP);
    assert(!TitanNetworkStream_Read(stream, input, sizeof(input), 100u, &count));

    stale = stream;
    TitanNetworkStream_Close(stream);
    TitanNetworkStream_Close(listener);
    assert(g_closed[10] == 1 && g_closed[11] == 1);
    assert(!TitanNetworkStream_Read(stale, input, sizeof(input), 100u, &count));

    TitanNetworkStream_GetSnapshot(&snapshot);
    assert(snapshot.listeners_opened == 1u);
    assert(snapshot.connections_accepted == 1u);
    assert(snapshot.bytes_received == sizeof(g_rx) - 1u);
    assert(snapshot.bytes_sent == strlen(reply));
    assert(snapshot.waits_timed_out == 1u);
    assert(snapshot.active_listeners == 0u);
    assert(snapshot.active_streams == 0u);

    puts("buildfix27-network-stream-host: OK bounded opaque TCP server lifecycle");
    return 0;
}

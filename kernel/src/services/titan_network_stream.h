#ifndef PROJECT_TITAN_NETWORK_STREAM_H
#define PROJECT_TITAN_NETWORK_STREAM_H

#include <stdint.h>

typedef enum TitanNetworkStreamOwner
{
    TITAN_NETWORK_STREAM_OWNER_NONE = 0,
    TITAN_NETWORK_STREAM_OWNER_FTP,
    TITAN_NETWORK_STREAM_OWNER_NATIVE_SERVICE
} TitanNetworkStreamOwner;

typedef uint32_t TitanNetworkStreamHandle;

typedef enum TitanNetworkStreamIoStatus
{
    TITAN_NETWORK_STREAM_IO_ERROR = -1,
    TITAN_NETWORK_STREAM_IO_IDLE = 0,
    TITAN_NETWORK_STREAM_IO_DATA = 1,
    TITAN_NETWORK_STREAM_IO_CLOSED = 2,
    TITAN_NETWORK_STREAM_IO_CANCELLED = 3
} TitanNetworkStreamIoStatus;

#define TITAN_NETWORK_STREAM_INVALID_HANDLE 0u

typedef struct TitanNetworkStreamSnapshot
{
    unsigned int listeners_opened;
    unsigned int connections_accepted;
    unsigned int bytes_received;
    unsigned int bytes_sent;
    unsigned int waits_timed_out;
    unsigned int operations_cancelled;
    unsigned int rejected_capacity;
    unsigned int active_listeners;
    unsigned int active_streams;
} TitanNetworkStreamSnapshot;

/* Titan-owned bounded TCP server surface. Socket descriptors never leave this
 * service; callers receive generation-checked opaque handles only. All waits
 * are deadline bounded and may be interrupted with CancelOwner(). */
int TitanNetworkStream_Listen(
    TitanNetworkStreamOwner owner,
    unsigned short port,
    unsigned int backlog,
    TitanNetworkStreamHandle *out_listener
);

int TitanNetworkStream_Accept(
    TitanNetworkStreamHandle listener,
    unsigned int deadline_ms,
    TitanNetworkStreamHandle *out_stream
);

int TitanNetworkStream_Read(
    TitanNetworkStreamHandle stream,
    void *buffer,
    unsigned int capacity,
    unsigned int deadline_ms,
    unsigned int *out_size
);

TitanNetworkStreamIoStatus TitanNetworkStream_ReadStatus(
    TitanNetworkStreamHandle stream,
    void *buffer,
    unsigned int capacity,
    unsigned int deadline_ms,
    unsigned int *out_size
);

int TitanNetworkStream_Write(
    TitanNetworkStreamHandle stream,
    const void *buffer,
    unsigned int size,
    unsigned int deadline_ms,
    unsigned int *out_size
);

void TitanNetworkStream_Close(TitanNetworkStreamHandle handle);
void TitanNetworkStream_CancelOwner(TitanNetworkStreamOwner owner);
void TitanNetworkStream_Reset(void);
void TitanNetworkStream_GetSnapshot(TitanNetworkStreamSnapshot *snapshot);

#endif

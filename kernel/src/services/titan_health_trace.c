#include "titan_health_trace.h"
#include "titan_storage_io.h"
#include "titan_time.h"

#include <stdio.h>
#include <string.h>

#if defined(__EE__) || defined(_EE)
#include <delaythread.h>
#include <kernel.h>
#endif

#define TITAN_HEALTH_TRACE_MAGIC 0x31544648u /* HFT1 */
#define TITAN_HEALTH_TRACE_VERSION 1u
#define TITAN_HEALTH_TRACE_CAPACITY 1024u
#define TITAN_HEALTH_TRACE_PATH "mass:/TITAN/logs/health-freeze-trace.bin"
#define TITAN_HEALTH_TRACE_LAST_PATH "mass:/TITAN/logs/health-freeze-last.txt"
#define TITAN_HEALTH_TRACE_DIR "mass:/TITAN/logs"
#define TITAN_HEALTH_TRACE_CRITICAL_WAIT_MS 180u

typedef struct TitanHealthTraceEvent {
    uint32_t sequence;
    uint32_t timestamp_us_low;
    uint32_t frame;
    uint16_t code;
    uint16_t reserved;
    uint32_t a;
    uint32_t b;
    uint32_t c;
} TitanHealthTraceEvent;

typedef struct TitanHealthTraceHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t event_count;
    uint32_t newest_sequence;
    uint32_t last_critical_sequence;
    uint32_t last_critical_code;
    uint32_t requested_generation;
    uint32_t flushed_generation;
} TitanHealthTraceHeader;

typedef struct TitanHealthTraceState {
    int initialized;
    volatile uint32_t next_sequence;
    volatile uint32_t event_count;
    volatile uint32_t request_generation;
    volatile uint32_t flushed_generation;
    volatile uint32_t last_critical_sequence;
    volatile uint32_t last_critical_code;
    TitanHealthTraceEvent events[TITAN_HEALTH_TRACE_CAPACITY];
} TitanHealthTraceState;

static TitanHealthTraceState g_health_trace;
static volatile int g_health_trace_enabled = 1;

void TitanHealthTrace_Initialize(void)
{
    memset(&g_health_trace, 0, sizeof(g_health_trace));
    g_health_trace_enabled = 1;
    g_health_trace.initialized = 1;
}

void TitanHealthTrace_SetEnabled(int enabled)
{
    g_health_trace_enabled = enabled ? 1 : 0;
}

int TitanHealthTrace_IsEnabled(void)
{
    return g_health_trace_enabled != 0;
}

static uint32_t TitanHealthTrace_Record(uint16_t code, uint32_t frame, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t sequence;
    uint32_t slot;
    uint32_t timestamp_us_low;
    TitanHealthTraceEvent *event;
#if defined(__EE__) || defined(_EE)
    int intr_state;
#endif

    if (!g_health_trace.initialized || !g_health_trace_enabled)
        return 0u;

    timestamp_us_low = (uint32_t)TitanTime_NowMicroseconds();
#if defined(__EE__) || defined(_EE)
    intr_state = DIntr();
#endif
    sequence = g_health_trace.next_sequence + 1u;
    g_health_trace.next_sequence = sequence;
    slot = (sequence - 1u) % TITAN_HEALTH_TRACE_CAPACITY;
    event = &g_health_trace.events[slot];

    /* sequence is committed last so a concurrent storage snapshot can reject
     * a partially-written record instead of inventing a checkpoint. */
    event->sequence = 0u;
    event->timestamp_us_low = timestamp_us_low;
    event->frame = frame;
    event->code = code;
    event->reserved = 0u;
    event->a = a;
    event->b = b;
    event->c = c;
    event->sequence = sequence;

    if (g_health_trace.event_count < TITAN_HEALTH_TRACE_CAPACITY)
        g_health_trace.event_count++;
#if defined(__EE__) || defined(_EE)
    if (intr_state != 0)
        EIntr();
#endif
    return sequence;
}

void TitanHealthTrace_Mark(uint16_t code, uint32_t frame, uint32_t a, uint32_t b, uint32_t c)
{
    (void)TitanHealthTrace_Record(code, frame, a, b, c);
}

void TitanHealthTrace_RequestFlush(void)
{
#if defined(__EE__) || defined(_EE)
    int intr_state;
#endif
    if (!g_health_trace.initialized || !g_health_trace_enabled)
        return;
#if defined(__EE__) || defined(_EE)
    intr_state = DIntr();
#endif
    g_health_trace.request_generation++;
#if defined(__EE__) || defined(_EE)
    if (intr_state != 0)
        EIntr();
#endif
}

void TitanHealthTrace_Critical(uint16_t code, uint32_t frame, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t generation;
    uint32_t sequence;
#if defined(__EE__) || defined(_EE)
    unsigned int waited;
    int intr_state;
#endif

    sequence = TitanHealthTrace_Record(code, frame, a, b, c);
    if (sequence == 0u)
        return;
#if defined(__EE__) || defined(_EE)
    intr_state = DIntr();
#endif
    g_health_trace.last_critical_sequence = sequence;
    g_health_trace.last_critical_code = (uint32_t)code;
    generation = g_health_trace.request_generation + 1u;
    g_health_trace.request_generation = generation;
#if defined(__EE__) || defined(_EE)
    if (intr_state != 0)
        EIntr();
#endif

#if defined(__EE__) || defined(_EE)
    /* Diagnostic build only: give the serialized storage worker a bounded
     * chance to persist the checkpoint before entering an IOP/RPC boundary.
     * Never wait indefinitely, otherwise the tracer could become the freeze. */
    for (waited = 0u; waited < TITAN_HEALTH_TRACE_CRITICAL_WAIT_MS; waited++)
    {
        if (g_health_trace.flushed_generation >= generation)
            break;
        DelayThread(1000);
    }
#else
    (void)generation;
#endif
}

int TitanHealthTrace_HasStorageWork(void)
{
    return g_health_trace.initialized &&
        g_health_trace_enabled &&
        g_health_trace.flushed_generation < g_health_trace.request_generation;
}

static int TitanHealthTrace_WriteLastText(const TitanHealthTraceHeader *header)
{
    char line[256];
    int fd;
    int length;

    fd = TitanStorageIo_Open(
        TITAN_HEALTH_TRACE_LAST_PATH,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE);
    if (fd < 0)
        return 0;

    length = snprintf(
        line, sizeof(line),
        "magic=HFT1 version=%lu events=%lu newest_seq=%lu critical_seq=%lu critical_code=0x%04x requested=%lu flushed=%lu\n",
        (unsigned long)header->version,
        (unsigned long)header->event_count,
        (unsigned long)header->newest_sequence,
        (unsigned long)header->last_critical_sequence,
        (unsigned int)header->last_critical_code,
        (unsigned long)header->requested_generation,
        (unsigned long)header->flushed_generation);
    if (length < 0 || (unsigned int)length >= sizeof(line) ||
        !TitanStorageIo_WriteExact(fd, line, (size_t)length))
    {
        TitanStorageIo_Close(fd);
        return 0;
    }

    if (TitanStorageIo_Flush(fd) < 0)
    {
        TitanStorageIo_Close(fd);
        return 0;
    }
    TitanStorageIo_Close(fd);
    return 1;
}

int TitanHealthTrace_StorageWorkerPump(void)
{
    TitanHealthTraceHeader header;
    uint32_t target_generation;
    uint32_t newest;
    uint32_t count;
    uint32_t first_sequence;
    uint32_t sequence;
    int fd;

    if (!TitanHealthTrace_HasStorageWork())
        return 0;

    target_generation = g_health_trace.request_generation;
    newest = g_health_trace.next_sequence;
    count = g_health_trace.event_count;
    if (count > newest)
        count = newest;
    first_sequence = count ? (newest - count + 1u) : 0u;

    (void)TitanStorageIo_CreateDirectory(TITAN_HEALTH_TRACE_DIR);

    fd = TitanStorageIo_Open(
        TITAN_HEALTH_TRACE_PATH,
        TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE);
    if (fd < 0)
        return 0;

    memset(&header, 0, sizeof(header));
    header.magic = TITAN_HEALTH_TRACE_MAGIC;
    header.version = TITAN_HEALTH_TRACE_VERSION;
    header.event_count = count;
    header.newest_sequence = newest;
    header.last_critical_sequence = g_health_trace.last_critical_sequence;
    header.last_critical_code = g_health_trace.last_critical_code;
    header.requested_generation = target_generation;
    header.flushed_generation = target_generation;

    if (!TitanStorageIo_WriteExact(fd, &header, sizeof(header)))
    {
        TitanStorageIo_Close(fd);
        return 0;
    }

    for (sequence = first_sequence; sequence != 0u && sequence <= newest; sequence++)
    {
        const TitanHealthTraceEvent *event =
            &g_health_trace.events[(sequence - 1u) % TITAN_HEALTH_TRACE_CAPACITY];
        TitanHealthTraceEvent copy = *event;
        if (copy.sequence != sequence)
            continue;
        if (!TitanStorageIo_WriteExact(fd, &copy, sizeof(copy)))
        {
            TitanStorageIo_Close(fd);
            return 0;
        }
    }

    if (TitanStorageIo_Flush(fd) < 0)
    {
        TitanStorageIo_Close(fd);
        return 0;
    }
    TitanStorageIo_Close(fd);

    g_health_trace.flushed_generation = target_generation;
    header.flushed_generation = target_generation;
    (void)TitanHealthTrace_WriteLastText(&header);
    return 1;
}

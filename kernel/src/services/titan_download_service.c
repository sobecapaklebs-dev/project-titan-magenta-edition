#include "titan_download_service.h"

#include "titan_http_client.h"
#include "titan_network_client.h"
#include "titan_package_installer.h"
#include "titan_sha256.h"
#include "titan_storage_io.h"
#include "titan_storage_scheduler.h"
#include "titan_time.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EE__) || defined(_EE)
#include <kernel.h>
#include <delaythread.h>
#include <malloc.h>
extern void *_gp;
#endif

#define TITAN_DOWNLOAD_RING_SLOTS 4u
#define TITAN_DOWNLOAD_CHUNK_SIZE 4096u
#define TITAN_DOWNLOAD_WORKER_STACK_SIZE (24u * 1024u)
#define TITAN_DOWNLOAD_NETWORK_DEADLINE_MS 120000u
#define TITAN_DOWNLOAD_STORAGE_WAIT_MS 10000u
#define TITAN_DOWNLOAD_DEFAULT_RETRY_LIMIT 2u
#define TITAN_DOWNLOAD_RETRY_DELAY_US 250000u

/* HTTP Range is intentionally not advertised until TitanHttpClient has an
 * audited Range/Content-Range contract. Pause is therefore queue-safe and an
 * active pause restarts from byte zero on Resume. This is honest rather than
 * pretending a partial file can be resumed. */
#define TITAN_DOWNLOAD_RANGE_RESUME_SUPPORTED 0

typedef enum TitanDownloadStorageCommand
{
    TITAN_DOWNLOAD_STORAGE_IDLE = 0,
    TITAN_DOWNLOAD_STORAGE_PREPARE,
    TITAN_DOWNLOAD_STORAGE_WRITE,
    TITAN_DOWNLOAD_STORAGE_FINALIZE,
    TITAN_DOWNLOAD_STORAGE_ABORT
} TitanDownloadStorageCommand;

typedef struct TitanDownloadRingSlot
{
    unsigned int size;
    unsigned char data[TITAN_DOWNLOAD_CHUNK_SIZE];
    int full;
} TitanDownloadRingSlot;

typedef struct TitanDownloadJob
{
    unsigned int id;
    unsigned int sequence;
    TitanDownloadRequest request;
    TitanDownloadState state;
    TitanDownloadFailure failure;
    TitanDownloadStorageCommand storage_command;
    int storage_descriptor;
    int storage_ready;
    int storage_done;
    int cancel_requested;
    int pause_requested;
    int network_done;
    int network_ok;
    int http_status;
    unsigned int retries_used;
    uint64_t bytes_received;
    uint64_t bytes_written;
    uint64_t content_length;
    uint64_t started_ms;
    uint64_t last_progress_ms;
    unsigned int speed_bps;
    char temporary_path[TITAN_DOWNLOAD_PATH_CAPACITY];
    char backup_path[TITAN_DOWNLOAD_PATH_CAPACITY];
    TitanSha256Context sha256;
    unsigned int ring_read;
    unsigned int ring_write;
    unsigned int ring_count;
} TitanDownloadJob;

typedef struct TitanDownloadRuntime
{
    int initialized;
    int stopping;
    unsigned int next_id;
    unsigned int next_sequence;
    TitanDownloadJob *jobs;
    TitanDownloadRingSlot *ring;
#if defined(__EE__) || defined(_EE)
    int worker_thread_id;
    int worker_wake_sema;
    int lock_sema;
    unsigned char *worker_stack;
#endif
} TitanDownloadRuntime;

static TitanDownloadRuntime g_downloads;

static void TitanDownload_Lock(void)
{
#if defined(__EE__) || defined(_EE)
    if (g_downloads.initialized && g_downloads.lock_sema >= 0)
        WaitSema(g_downloads.lock_sema);
#endif
}

static void TitanDownload_Unlock(void)
{
#if defined(__EE__) || defined(_EE)
    if (g_downloads.initialized && g_downloads.lock_sema >= 0)
        SignalSema(g_downloads.lock_sema);
#endif
}

static void TitanDownload_WakeNetwork(void)
{
#if defined(__EE__) || defined(_EE)
    if (g_downloads.initialized && g_downloads.worker_wake_sema >= 0)
        (void)SignalSema(g_downloads.worker_wake_sema);
#endif
}

static void TitanDownload_Copy(char *dst, size_t cap, const char *src)
{
    size_t i = 0u;
    if (dst == 0 || cap == 0u) return;
    while (src != 0 && src[i] != '\0' && i + 1u < cap)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int TitanDownload_RequestSafe(const TitanDownloadRequest *request)
{
    const char *p;
    size_t hash_len;
    if (request == 0 || request->package_id[0] == '\0' || request->title[0] == '\0' ||
        request->url[0] == '\0' || request->destination[0] == '\0') return 0;
    if (strncmp(request->url, "https://", 8u) != 0 && strncmp(request->url, "http://", 7u) != 0)
        return 0;
    if (strstr(request->destination, "..") != 0) return 0;
    hash_len = strlen(request->expected_sha256);
    if (hash_len != 64u) return 0;
    for (p = request->expected_sha256; *p != '\0'; ++p)
    {
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')))
            return 0;
    }
    return 1;
}

static void TitanDownload_BuildSidecarPath(char *out, size_t cap, const char *destination, const char *suffix)
{
    size_t base_len;
    size_t suffix_len;
    if (out == 0 || cap == 0u) return;
    out[0] = '\0';
    if (destination == 0 || suffix == 0) return;
    base_len = strlen(destination);
    suffix_len = strlen(suffix);
    if (base_len + suffix_len + 1u > cap) return;
    memcpy(out, destination, base_len);
    memcpy(out + base_len, suffix, suffix_len + 1u);
}

static TitanDownloadJob *TitanDownload_FindJobLocked(unsigned int id)
{
    unsigned int i;
    if (g_downloads.jobs == 0) return 0;
    for (i = 0u; i < TITAN_DOWNLOAD_QUEUE_CAPACITY; i++)
        if (g_downloads.jobs[i].id == id && g_downloads.jobs[i].state != TITAN_DOWNLOAD_EMPTY)
            return &g_downloads.jobs[i];
    return 0;
}

static TitanDownloadJob *TitanDownload_FindNextQueuedLocked(void)
{
    unsigned int i;
    if (g_downloads.jobs == 0) return 0;
    TitanDownloadJob *best = 0;
    for (i = 0u; i < TITAN_DOWNLOAD_QUEUE_CAPACITY; i++)
    {
        TitanDownloadJob *job = &g_downloads.jobs[i];
        if (job->state != TITAN_DOWNLOAD_QUEUED) continue;
        if (best == 0 || job->sequence < best->sequence) best = job;
    }
    return best;
}

static void TitanDownload_ResetTransferLocked(TitanDownloadJob *job)
{
    unsigned int i;
    if (job == 0 || g_downloads.ring == 0) return;
    job->storage_descriptor = -1;
    job->storage_ready = 0;
    job->storage_done = 0;
    job->network_done = 0;
    job->network_ok = 0;
    job->http_status = 0;
    job->bytes_received = 0u;
    job->bytes_written = 0u;
    job->content_length = 0u;
    job->started_ms = 0u;
    job->last_progress_ms = 0u;
    job->speed_bps = 0u;
    job->ring_read = 0u;
    job->ring_write = 0u;
    job->ring_count = 0u;
    for (i = 0u; i < TITAN_DOWNLOAD_RING_SLOTS; i++)
    {
        g_downloads.ring[i].size = 0u;
        g_downloads.ring[i].full = 0;
    }
}

static int TitanDownload_ShouldCancel(void *user_data)
{
    TitanDownloadJob *job = (TitanDownloadJob *)user_data;
    int cancel;
    TitanDownload_Lock();
    cancel = job == 0 || job->cancel_requested || job->pause_requested || g_downloads.stopping;
    TitanDownload_Unlock();
    return cancel;
}

static void TitanDownload_Progress(void *user_data, unsigned int received, unsigned int total)
{
    TitanDownloadJob *job = (TitanDownloadJob *)user_data;
    uint64_t now;
    uint64_t elapsed;
    if (job == 0) return;
    now = TitanTime_NowMilliseconds();
    TitanDownload_Lock();
    job->bytes_received = received;
    if (total != 0u) job->content_length = total;
    if (job->started_ms == 0u) job->started_ms = now;
    job->last_progress_ms = now;
    elapsed = now > job->started_ms ? now - job->started_ms : 0u;
    if (elapsed >= 250u && received != 0u)
    {
        uint64_t speed = ((uint64_t)received * 1000u) / elapsed;
        job->speed_bps = speed > UINT_MAX ? UINT_MAX : (unsigned int)speed;
    }
    TitanDownload_Unlock();
}

static int TitanDownload_BodyWrite(void *user_data, const unsigned char *data, unsigned int size)
{
    TitanDownloadJob *job = (TitanDownloadJob *)user_data;
    unsigned int offset = 0u;
    if (job == 0 || (data == 0 && size != 0u)) return 0;
    while (offset < size)
    {
        unsigned int chunk = size - offset;
        int accepted = 0;
        if (chunk > TITAN_DOWNLOAD_CHUNK_SIZE) chunk = TITAN_DOWNLOAD_CHUNK_SIZE;
        while (!accepted)
        {
            TitanDownload_Lock();
            if (job->cancel_requested || job->pause_requested || g_downloads.stopping)
            {
                TitanDownload_Unlock();
                return 0;
            }
            if (job->ring_count < TITAN_DOWNLOAD_RING_SLOTS)
            {
                TitanDownloadRingSlot *slot = &g_downloads.ring[job->ring_write];
                memcpy(slot->data, data + offset, chunk);
                slot->size = chunk;
                slot->full = 1;
                job->ring_write = (job->ring_write + 1u) % TITAN_DOWNLOAD_RING_SLOTS;
                job->ring_count++;
                job->storage_command = TITAN_DOWNLOAD_STORAGE_WRITE;
                accepted = 1;
            }
            TitanDownload_Unlock();
            if (!accepted)
            {
#if defined(TITAN_DOWNLOAD_SERVICE_HOST_TEST)
                (void)TitanDownloadService_StorageWorkerPump();
#elif defined(__EE__) || defined(_EE)
                DelayThread(1000u);
#endif
            }
        }
        TitanStorageScheduler_NotifyBackgroundWork();
#if defined(TITAN_DOWNLOAD_SERVICE_HOST_TEST)
        (void)TitanDownloadService_StorageWorkerPump();
#endif
        offset += chunk;
    }
    return 1;
}

static int TitanDownload_WaitForStorage(TitanDownloadJob *job, int want_done, unsigned int timeout_ms)
{
    uint64_t start = TitanTime_NowMilliseconds();
    for (;;)
    {
        int ready;
        int done;
        TitanDownload_Lock();
        ready = job->storage_ready;
        done = job->storage_done;
        TitanDownload_Unlock();
        if ((!want_done && ready) || (want_done && done)) return 1;
        if (TitanTime_NowMilliseconds() - start >= timeout_ms) return 0;
#if defined(TITAN_DOWNLOAD_SERVICE_HOST_TEST)
        (void)TitanDownloadService_StorageWorkerPump();
#elif defined(__EE__) || defined(_EE)
        DelayThread(1000u);
#endif
    }
}

static void TitanDownload_SetFailureLocked(TitanDownloadJob *job, TitanDownloadFailure failure)
{
    if (job == 0) return;
    job->failure = failure;
    job->state = failure == TITAN_DOWNLOAD_FAILURE_CANCELLED
        ? TITAN_DOWNLOAD_CANCELLED : TITAN_DOWNLOAD_FAILED;
}

static int TitanDownload_RunJob(TitanDownloadJob *job)
{
    TitanHttpClientOptions options;
    TitanHttpResultInfo result;
    unsigned int out_size = 0u;
    int status = 0;
    int ok;
    int pause;
    int cancel;

    TitanDownload_Lock();
    TitanDownload_ResetTransferLocked(job);
    job->state = TITAN_DOWNLOAD_PREPARING;
    job->failure = TITAN_DOWNLOAD_FAILURE_NONE;
    job->storage_command = TITAN_DOWNLOAD_STORAGE_PREPARE;
    TitanDownload_Unlock();
    TitanStorageScheduler_NotifyBackgroundWork();
    if (!TitanDownload_WaitForStorage(job, 0, TITAN_DOWNLOAD_STORAGE_WAIT_MS))
    {
        TitanDownload_Lock();
        TitanDownload_SetFailureLocked(job, TITAN_DOWNLOAD_FAILURE_STORAGE);
        TitanDownload_Unlock();
        return 0;
    }

    memset(&options, 0, sizeof(options));
    memset(&result, 0, sizeof(result));
    options.should_cancel = TitanDownload_ShouldCancel;
    options.progress = TitanDownload_Progress;
    options.body_write = TitanDownload_BodyWrite;
    options.user_data = job;
    options.deadline_milliseconds = TITAN_DOWNLOAD_NETWORK_DEADLINE_MS;
    options.maximum_redirects = 3u;
    options.yield_microseconds = 1000u;

    TitanDownload_Lock();
    job->state = TITAN_DOWNLOAD_DOWNLOADING;
    job->started_ms = TitanTime_NowMilliseconds();
    TitanDownload_Unlock();

    ok = TitanNetworkClient_GetEx(
        TITAN_NETWORK_CLIENT_OWNER_DOWNLOAD_SERVICE,
        job->request.url,
        0,
        0u,
        &out_size,
        &status,
        &options,
        &result
    );

    TitanDownload_Lock();
    job->network_done = 1;
    job->network_ok = ok;
    job->http_status = status;
    if (result.content_length != 0u) job->content_length = result.content_length;
    pause = job->pause_requested;
    cancel = job->cancel_requested;
    TitanDownload_Unlock();

    if (!ok || status != 200)
    {
        TitanDownload_Lock();
        job->storage_command = TITAN_DOWNLOAD_STORAGE_ABORT;
        TitanDownload_Unlock();
        TitanStorageScheduler_NotifyBackgroundWork();
        (void)TitanDownload_WaitForStorage(job, 1, TITAN_DOWNLOAD_STORAGE_WAIT_MS);

        TitanDownload_Lock();
        if (pause)
        {
            job->state = TITAN_DOWNLOAD_PAUSED;
            job->failure = TITAN_DOWNLOAD_FAILURE_NONE;
            job->pause_requested = 0;
        }
        else if (cancel)
        {
            TitanDownload_SetFailureLocked(job, TITAN_DOWNLOAD_FAILURE_CANCELLED);
        }
        else if (job->retries_used < job->request.retry_limit)
        {
            job->retries_used++;
            job->state = TITAN_DOWNLOAD_QUEUED;
            job->sequence = ++g_downloads.next_sequence;
            job->failure = TITAN_DOWNLOAD_FAILURE_NONE;
        }
        else
        {
            TitanDownload_SetFailureLocked(
                job,
                result.failure_reason == TITAN_HTTP_FAILURE_TIMEOUT
                    ? TITAN_DOWNLOAD_FAILURE_TIMEOUT
                    : TITAN_DOWNLOAD_FAILURE_NETWORK
            );
        }
        TitanDownload_Unlock();
        return 0;
    }

    /* Drain all accepted network chunks before final integrity checks. */
    for (;;)
    {
        unsigned int pending;
        TitanDownload_Lock();
        pending = job->ring_count;
        TitanDownload_Unlock();
        if (pending == 0u) break;
        TitanStorageScheduler_NotifyBackgroundWork();
#if defined(TITAN_DOWNLOAD_SERVICE_HOST_TEST)
        (void)TitanDownloadService_StorageWorkerPump();
#elif defined(__EE__) || defined(_EE)
        DelayThread(1000u);
#endif
    }

    TitanDownload_Lock();
    job->state = TITAN_DOWNLOAD_VERIFYING;
    job->storage_command = TITAN_DOWNLOAD_STORAGE_FINALIZE;
    job->storage_done = 0;
    TitanDownload_Unlock();
    TitanStorageScheduler_NotifyBackgroundWork();
    if (!TitanDownload_WaitForStorage(job, 1, TITAN_DOWNLOAD_STORAGE_WAIT_MS))
    {
        TitanDownload_Lock();
        TitanDownload_SetFailureLocked(job, TITAN_DOWNLOAD_FAILURE_STORAGE);
        TitanDownload_Unlock();
        return 0;
    }

    TitanDownload_Lock();
    ok = job->state == TITAN_DOWNLOAD_COMPLETE;
    TitanDownload_Unlock();
    return ok;
}

#if defined(__EE__) || defined(_EE)
static void TitanDownload_NetworkWorker(void *argument)
{
    (void)argument;
    for (;;)
    {
        TitanDownloadJob *job;
        WaitSema(g_downloads.worker_wake_sema);
        if (g_downloads.stopping) break;
        for (;;)
        {
            TitanDownload_Lock();
            job = TitanDownload_FindNextQueuedLocked();
            TitanDownload_Unlock();
            if (job == 0) break;
            (void)TitanDownload_RunJob(job);
            if (g_downloads.stopping) break;
            if (job->state == TITAN_DOWNLOAD_QUEUED)
                DelayThread(TITAN_DOWNLOAD_RETRY_DELAY_US);
        }
    }
    g_downloads.worker_thread_id=-1;
    ExitDeleteThread();
}
#endif

static int TitanDownload_EnsureResources(void)
{
    if (g_downloads.jobs != 0 && g_downloads.ring != 0)
#if defined(__EE__) || defined(_EE)
        return g_downloads.worker_thread_id >= 0;
#else
        return 1;
#endif

    if (g_downloads.jobs == 0)
    {
        g_downloads.jobs = (TitanDownloadJob *)calloc(TITAN_DOWNLOAD_QUEUE_CAPACITY, sizeof(TitanDownloadJob));
        if (g_downloads.jobs == 0) return 0;
    }
    if (g_downloads.ring == 0)
    {
        g_downloads.ring = (TitanDownloadRingSlot *)calloc(TITAN_DOWNLOAD_RING_SLOTS, sizeof(TitanDownloadRingSlot));
        if (g_downloads.ring == 0) return 0;
    }
#if defined(__EE__) || defined(_EE)
    if (g_downloads.lock_sema < 0)
    {
        ee_sema_t sema;
        memset(&sema,0,sizeof(sema)); sema.init_count=1; sema.max_count=1;
        g_downloads.lock_sema=CreateSema(&sema);
        if(g_downloads.lock_sema<0)return 0;
    }
    if (g_downloads.worker_wake_sema < 0)
    {
        ee_sema_t sema;
        memset(&sema,0,sizeof(sema)); sema.init_count=0; sema.max_count=1;
        g_downloads.worker_wake_sema=CreateSema(&sema);
        if(g_downloads.worker_wake_sema<0)return 0;
    }
    if (g_downloads.worker_stack == 0)
    {
        g_downloads.worker_stack=(unsigned char*)memalign(64,TITAN_DOWNLOAD_WORKER_STACK_SIZE);
        if(g_downloads.worker_stack==0)return 0;
    }
    if (g_downloads.worker_thread_id < 0)
    {
        ee_thread_t thread;
        memset(&thread,0,sizeof(thread));
        thread.func=TitanDownload_NetworkWorker;
        thread.stack=g_downloads.worker_stack;
        thread.stack_size=TITAN_DOWNLOAD_WORKER_STACK_SIZE;
        thread.gp_reg=&_gp;
        thread.initial_priority=52;
        g_downloads.worker_thread_id=CreateThread(&thread);
        if(g_downloads.worker_thread_id<0)return 0;
        if(StartThread(g_downloads.worker_thread_id,0)<0)
        {
            DeleteThread(g_downloads.worker_thread_id);
            g_downloads.worker_thread_id=-1;
            return 0;
        }
    }
#endif
    return 1;
}

int TitanDownloadService_Initialize(void)
{
    memset(&g_downloads,0,sizeof(g_downloads));
    g_downloads.next_id=1u;
#if defined(__EE__) || defined(_EE)
    g_downloads.worker_thread_id=-1;
    g_downloads.worker_wake_sema=-1;
    g_downloads.lock_sema=-1;
#endif
    /* No queue heap, ring, worker stack or EE thread exists until first enqueue. */
    g_downloads.initialized=1;
    return 1;
}

void TitanDownloadService_Shutdown(void)
{
    unsigned int i;
    if(!g_downloads.initialized)return;
    if(g_downloads.jobs==0){g_downloads.initialized=0;return;}
    TitanDownload_Lock();
    g_downloads.stopping=1;
    for(i=0u;i<TITAN_DOWNLOAD_QUEUE_CAPACITY;i++)
        if(g_downloads.jobs[i].state!=TITAN_DOWNLOAD_EMPTY&&g_downloads.jobs[i].state!=TITAN_DOWNLOAD_COMPLETE)
            g_downloads.jobs[i].cancel_requested=1;
    TitanDownload_Unlock();
    TitanDownload_WakeNetwork();
    TitanStorageScheduler_NotifyBackgroundWork();
#if defined(__EE__) || defined(_EE)
    {
        unsigned int waits=0u;
        while(g_downloads.worker_thread_id>=0&&waits++<100u)DelayThread(1000u);
        if(g_downloads.worker_thread_id>=0)
        {
            int id=g_downloads.worker_thread_id;
            g_downloads.worker_thread_id=-1;
            (void)TerminateThread(id);
            (void)DeleteThread(id);
        }
        if(g_downloads.worker_wake_sema>=0){DeleteSema(g_downloads.worker_wake_sema);g_downloads.worker_wake_sema=-1;}
        if(g_downloads.lock_sema>=0){DeleteSema(g_downloads.lock_sema);g_downloads.lock_sema=-1;}
        free(g_downloads.worker_stack);g_downloads.worker_stack=0;
    }
#endif
    free(g_downloads.ring);g_downloads.ring=0;
    free(g_downloads.jobs);g_downloads.jobs=0;
    g_downloads.initialized=0;
}

unsigned int TitanDownloadService_Enqueue(const TitanDownloadRequest *request)
{
    unsigned int i;
    TitanDownloadJob *job = 0;
    if (!g_downloads.initialized || !TitanDownload_RequestSafe(request)) return 0u;
    if (!TitanDownload_EnsureResources()) return 0u;
    TitanDownload_Lock();
    for (i = 0u; i < TITAN_DOWNLOAD_QUEUE_CAPACITY; i++)
    {
        TitanDownloadState s = g_downloads.jobs[i].state;
        if (s == TITAN_DOWNLOAD_EMPTY || s == TITAN_DOWNLOAD_COMPLETE ||
            s == TITAN_DOWNLOAD_CANCELLED || s == TITAN_DOWNLOAD_FAILED)
        {
            job = &g_downloads.jobs[i];
            break;
        }
    }
    if (job != 0)
    {
        memset(job, 0, sizeof(*job));
        job->id = g_downloads.next_id++;
        if (job->id == 0u) job->id = g_downloads.next_id++;
        job->sequence = ++g_downloads.next_sequence;
        job->request = *request;
        if (job->request.retry_limit == 0u)
            job->request.retry_limit = TITAN_DOWNLOAD_DEFAULT_RETRY_LIMIT;
        job->state = TITAN_DOWNLOAD_QUEUED;
        job->storage_descriptor = -1;
        TitanDownload_BuildSidecarPath(job->temporary_path, sizeof(job->temporary_path), request->destination, ".part");
        TitanDownload_BuildSidecarPath(job->backup_path, sizeof(job->backup_path), request->destination, ".bak");
        if (job->temporary_path[0] == '\0' || job->backup_path[0] == '\0')
        {
            memset(job, 0, sizeof(*job));
            job = 0;
        }
    }
    TitanDownload_Unlock();
    if (job != 0) TitanDownload_WakeNetwork();
    return job != 0 ? job->id : 0u;
}

int TitanDownloadService_Cancel(unsigned int job_id)
{
    TitanDownloadJob *job;
    int ok = 0;
    TitanDownload_Lock();
    job = TitanDownload_FindJobLocked(job_id);
    if (job != 0 && job->state != TITAN_DOWNLOAD_COMPLETE &&
        job->state != TITAN_DOWNLOAD_CANCELLED && job->state != TITAN_DOWNLOAD_FAILED)
    {
        job->cancel_requested = 1;
        if (job->state == TITAN_DOWNLOAD_QUEUED || job->state == TITAN_DOWNLOAD_PAUSED)
        {
            job->state = TITAN_DOWNLOAD_CANCELLED;
            job->failure = TITAN_DOWNLOAD_FAILURE_CANCELLED;
        }
        ok = 1;
    }
    TitanDownload_Unlock();
    return ok;
}

int TitanDownloadService_Pause(unsigned int job_id)
{
    TitanDownloadJob *job;
    int ok = 0;
    TitanDownload_Lock();
    job = TitanDownload_FindJobLocked(job_id);
    if (job != 0 && (job->state == TITAN_DOWNLOAD_QUEUED || job->state == TITAN_DOWNLOAD_DOWNLOADING))
    {
        if (job->state == TITAN_DOWNLOAD_QUEUED) job->state = TITAN_DOWNLOAD_PAUSED;
        else job->pause_requested = 1;
        ok = 1;
    }
    TitanDownload_Unlock();
    return ok;
}

int TitanDownloadService_Resume(unsigned int job_id)
{
    TitanDownloadJob *job;
    int ok = 0;
    TitanDownload_Lock();
    job = TitanDownload_FindJobLocked(job_id);
    if (job != 0 && job->state == TITAN_DOWNLOAD_PAUSED)
    {
        job->pause_requested = 0;
        job->cancel_requested = 0;
        job->state = TITAN_DOWNLOAD_QUEUED;
        job->sequence = ++g_downloads.next_sequence;
        ok = 1;
    }
    TitanDownload_Unlock();
    if (ok) TitanDownload_WakeNetwork();
    return ok;
}

unsigned int TitanDownloadService_GetCount(void)
{
    unsigned int i;
    if(g_downloads.jobs==0)return 0u;
    unsigned int count = 0u;
    TitanDownload_Lock();
    for (i = 0u; i < TITAN_DOWNLOAD_QUEUE_CAPACITY; i++)
        if (g_downloads.jobs[i].state != TITAN_DOWNLOAD_EMPTY) count++;
    TitanDownload_Unlock();
    return count;
}

static void TitanDownload_FillSnapshotLocked(const TitanDownloadJob *job, TitanDownloadSnapshot *snapshot)
{
    uint64_t total;
    uint64_t remaining;
    if (job == 0 || snapshot == 0) return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->job_id = job->id;
    snapshot->state = job->state;
    snapshot->failure = job->failure;
    TitanDownload_Copy(snapshot->package_id, sizeof(snapshot->package_id), job->request.package_id);
    TitanDownload_Copy(snapshot->title, sizeof(snapshot->title), job->request.title);
    TitanDownload_Copy(snapshot->destination, sizeof(snapshot->destination), job->request.destination);
    snapshot->bytes_downloaded = job->bytes_received;
    total = job->request.expected_size != 0u ? job->request.expected_size : job->content_length;
    snapshot->total_bytes = total;
    snapshot->total_known = total != 0u;
    if (total != 0u)
    {
        uint64_t percent = (job->bytes_received * 100u) / total;
        snapshot->progress_percent = percent > 100u ? 100u : (unsigned int)percent;
    }
    snapshot->speed_bytes_per_second = job->speed_bps;
    if (total > job->bytes_received && job->speed_bps != 0u)
    {
        remaining = total - job->bytes_received;
        snapshot->eta_seconds = (unsigned int)((remaining + job->speed_bps - 1u) / job->speed_bps);
    }
    snapshot->retries_used = job->retries_used;
    snapshot->pause_supported = 1;
    snapshot->resume_supported = TITAN_DOWNLOAD_RANGE_RESUME_SUPPORTED;
}

int TitanDownloadService_GetSnapshot(unsigned int index, TitanDownloadSnapshot *snapshot)
{
    unsigned int i;
    unsigned int seen = 0u;
    int ok = 0;
    if (snapshot == 0 || g_downloads.jobs == 0) return 0;
    TitanDownload_Lock();
    for (i = 0u; i < TITAN_DOWNLOAD_QUEUE_CAPACITY; i++)
    {
        if (g_downloads.jobs[i].state == TITAN_DOWNLOAD_EMPTY) continue;
        if (seen++ == index)
        {
            TitanDownload_FillSnapshotLocked(&g_downloads.jobs[i], snapshot);
            ok = 1;
            break;
        }
    }
    TitanDownload_Unlock();
    return ok;
}

int TitanDownloadService_Find(unsigned int job_id, TitanDownloadSnapshot *snapshot)
{
    TitanDownloadJob *job;
    int ok = 0;
    if (snapshot == 0 || g_downloads.jobs == 0) return 0;
    TitanDownload_Lock();
    job = TitanDownload_FindJobLocked(job_id);
    if (job != 0)
    {
        TitanDownload_FillSnapshotLocked(job, snapshot);
        ok = 1;
    }
    TitanDownload_Unlock();
    return ok;
}

int TitanDownloadService_HasStorageWork(void)
{
    unsigned int i;
    if(g_downloads.jobs==0)return 0;
    int result = 0;
    TitanDownload_Lock();
    for (i = 0u; i < TITAN_DOWNLOAD_QUEUE_CAPACITY; i++)
    {
        TitanDownloadJob *job = &g_downloads.jobs[i];
        if (job->storage_command != TITAN_DOWNLOAD_STORAGE_IDLE || job->ring_count != 0u)
        {
            result = 1;
            break;
        }
    }
    TitanDownload_Unlock();
    return result;
}

int TitanDownloadService_StorageWorkerPump(void)
{
    unsigned int i;
    if(g_downloads.jobs==0||g_downloads.ring==0)return 0;
    TitanDownloadJob *job = 0;
    TitanDownloadStorageCommand command = TITAN_DOWNLOAD_STORAGE_IDLE;
    unsigned char chunk[TITAN_DOWNLOAD_CHUNK_SIZE];
    unsigned int chunk_size = 0u;

    TitanDownload_Lock();
    for (i = 0u; i < TITAN_DOWNLOAD_QUEUE_CAPACITY; i++)
    {
        if (g_downloads.jobs[i].storage_command != TITAN_DOWNLOAD_STORAGE_IDLE ||
            g_downloads.jobs[i].ring_count != 0u)
        {
            job = &g_downloads.jobs[i];
            command = job->storage_command;
            if (job->ring_count != 0u && g_downloads.ring[job->ring_read].full)
            {
                TitanDownloadRingSlot *slot = &g_downloads.ring[job->ring_read];
                chunk_size = slot->size;
                memcpy(chunk, slot->data, chunk_size);
                slot->full = 0;
                slot->size = 0u;
                job->ring_read = (job->ring_read + 1u) % TITAN_DOWNLOAD_RING_SLOTS;
                job->ring_count--;
                command = TITAN_DOWNLOAD_STORAGE_WRITE;
            }
            break;
        }
    }
    TitanDownload_Unlock();
    if (job == 0) return 0;

    if (command == TITAN_DOWNLOAD_STORAGE_PREPARE)
    {
        int descriptor;
        (void)TitanStorageIo_Remove(job->temporary_path);
        descriptor = TitanStorageIo_Open(
            job->temporary_path,
            TITAN_STORAGE_IO_WRITE | TITAN_STORAGE_IO_CREATE | TITAN_STORAGE_IO_TRUNCATE
        );
        TitanDownload_Lock();
        if (descriptor < 0)
        {
            job->storage_done = 1;
            job->storage_command = TITAN_DOWNLOAD_STORAGE_IDLE;
            TitanDownload_SetFailureLocked(job, TITAN_DOWNLOAD_FAILURE_STORAGE);
        }
        else
        {
            job->storage_descriptor = descriptor;
            TitanSha256_Init(&job->sha256);
            job->storage_ready = 1;
            job->storage_command = TITAN_DOWNLOAD_STORAGE_IDLE;
        }
        TitanDownload_Unlock();
        return 1;
    }

    if (command == TITAN_DOWNLOAD_STORAGE_WRITE && chunk_size != 0u)
    {
        int descriptor;
        int ok;
        TitanDownload_Lock();
        descriptor = job->storage_descriptor;
        TitanDownload_Unlock();
        ok = descriptor >= 0 && TitanStorageIo_WriteExact(descriptor, chunk, chunk_size);
        TitanDownload_Lock();
        if (ok)
        {
            TitanSha256_Update(&job->sha256, chunk, chunk_size);
            job->bytes_written += chunk_size;
            job->storage_command = job->ring_count != 0u
                ? TITAN_DOWNLOAD_STORAGE_WRITE : TITAN_DOWNLOAD_STORAGE_IDLE;
        }
        else
        {
            job->storage_command = TITAN_DOWNLOAD_STORAGE_ABORT;
            TitanDownload_SetFailureLocked(job, TITAN_DOWNLOAD_FAILURE_STORAGE);
        }
        TitanDownload_Unlock();
        return 1;
    }

    if (command == TITAN_DOWNLOAD_STORAGE_ABORT)
    {
        int descriptor;
        TitanDownload_Lock();
        descriptor = job->storage_descriptor;
        job->storage_descriptor = -1;
        TitanDownload_Unlock();
        if (descriptor >= 0) (void)TitanStorageIo_Close(descriptor);
        (void)TitanStorageIo_Remove(job->temporary_path);
        TitanDownload_Lock();
        job->storage_command = TITAN_DOWNLOAD_STORAGE_IDLE;
        job->storage_ready = 0;
        job->storage_done = 1;
        TitanDownload_Unlock();
        return 1;
    }

    if (command == TITAN_DOWNLOAD_STORAGE_FINALIZE)
    {
        unsigned char digest[32];
        TitanPackageInfo info;
        int descriptor;
        int ok = 1;
        TitanDownloadFailure failure = TITAN_DOWNLOAD_FAILURE_NONE;
        uint64_t expected_size;
        uint64_t bytes_written;

        TitanDownload_Lock();
        descriptor = job->storage_descriptor;
        bytes_written = job->bytes_written;
        expected_size = job->request.expected_size != 0u
            ? job->request.expected_size : job->content_length;
        TitanDownload_Unlock();

        if (descriptor < 0 || TitanStorageIo_Flush(descriptor) < 0 || TitanStorageIo_Close(descriptor) < 0)
        {
            ok = 0;
            failure = TITAN_DOWNLOAD_FAILURE_STORAGE;
        }
        TitanDownload_Lock();
        job->storage_descriptor = -1;
        TitanDownload_Unlock();

        if (ok && expected_size != 0u && bytes_written != expected_size)
        {
            ok = 0;
            failure = TITAN_DOWNLOAD_FAILURE_SIZE_MISMATCH;
        }
        TitanSha256_Final(&job->sha256, digest);
        if (ok && !TitanSha256_HexEqual(digest, job->request.expected_sha256))
        {
            ok = 0;
            failure = TITAN_DOWNLOAD_FAILURE_HASH_MISMATCH;
        }
        if (ok && !TitanPackageInstaller_ValidatePackageFile(job->temporary_path, &info))
        {
            ok = 0;
            failure = TITAN_DOWNLOAD_FAILURE_PACKAGE_INVALID;
        }
        if (ok)
        {
            TitanDownload_Lock();
            job->state = TITAN_DOWNLOAD_PUBLISHING;
            TitanDownload_Unlock();
            if (!TitanStorageIo_AtomicReplace(job->temporary_path, job->request.destination, job->backup_path))
            {
                ok = 0;
                failure = TITAN_DOWNLOAD_FAILURE_PUBLISH;
            }
            else
            {
                (void)TitanStorageIo_Remove(job->backup_path);
            }
        }
        if (!ok) (void)TitanStorageIo_Remove(job->temporary_path);

        TitanDownload_Lock();
        job->storage_command = TITAN_DOWNLOAD_STORAGE_IDLE;
        job->storage_ready = 0;
        job->storage_done = 1;
        if (ok)
        {
            job->state = TITAN_DOWNLOAD_COMPLETE;
            job->failure = TITAN_DOWNLOAD_FAILURE_NONE;
        }
        else TitanDownload_SetFailureLocked(job, failure);
        TitanDownload_Unlock();
        return 1;
    }

    return 0;
}

void TitanDownloadService_Update(void)
{
    /* Intentionally no network/storage work here. Shell/notification code may
     * query snapshots at frame rate without activating an unused backend. */
}

#if defined(TITAN_DOWNLOAD_SERVICE_HOST_TEST)
int TitanDownloadService_TestRunNext(void)
{
    TitanDownloadJob *job;
    TitanDownload_Lock();
    job = TitanDownload_FindNextQueuedLocked();
    TitanDownload_Unlock();
    if (job == 0) return 0;
    return TitanDownload_RunJob(job);
}
#endif

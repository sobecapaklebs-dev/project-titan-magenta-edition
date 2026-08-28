#ifndef PROJECT_TITAN_DOWNLOAD_SERVICE_H
#define PROJECT_TITAN_DOWNLOAD_SERVICE_H

#include <stdint.h>

#define TITAN_DOWNLOAD_SERVICE_API_VERSION 1u

#define TITAN_DOWNLOAD_QUEUE_CAPACITY 6u
#define TITAN_DOWNLOAD_TITLE_CAPACITY 80u
#define TITAN_DOWNLOAD_URL_CAPACITY 768u
#define TITAN_DOWNLOAD_PATH_CAPACITY 256u
#define TITAN_DOWNLOAD_SHA256_HEX_CAPACITY 65u

/* Download Service deliberately owns no package-install semantics. It publishes
 * only a fully downloaded, SHA-256 verified, structurally valid Titan package.
 * Installation remains an explicit handoff to Package Installer. */
typedef enum TitanDownloadState
{
    TITAN_DOWNLOAD_EMPTY = 0,
    TITAN_DOWNLOAD_QUEUED,
    TITAN_DOWNLOAD_PREPARING,
    TITAN_DOWNLOAD_DOWNLOADING,
    TITAN_DOWNLOAD_PAUSED,
    TITAN_DOWNLOAD_VERIFYING,
    TITAN_DOWNLOAD_PUBLISHING,
    TITAN_DOWNLOAD_COMPLETE,
    TITAN_DOWNLOAD_CANCELLED,
    TITAN_DOWNLOAD_FAILED
} TitanDownloadState;

typedef enum TitanDownloadFailure
{
    TITAN_DOWNLOAD_FAILURE_NONE = 0,
    TITAN_DOWNLOAD_FAILURE_QUEUE_FULL,
    TITAN_DOWNLOAD_FAILURE_INVALID_REQUEST,
    TITAN_DOWNLOAD_FAILURE_NETWORK,
    TITAN_DOWNLOAD_FAILURE_TIMEOUT,
    TITAN_DOWNLOAD_FAILURE_CANCELLED,
    TITAN_DOWNLOAD_FAILURE_STORAGE,
    TITAN_DOWNLOAD_FAILURE_SIZE_MISMATCH,
    TITAN_DOWNLOAD_FAILURE_HASH_MISMATCH,
    TITAN_DOWNLOAD_FAILURE_PACKAGE_INVALID,
    TITAN_DOWNLOAD_FAILURE_PUBLISH
} TitanDownloadFailure;

typedef struct TitanDownloadRequest
{
    char package_id[32];
    char title[TITAN_DOWNLOAD_TITLE_CAPACITY];
    char url[TITAN_DOWNLOAD_URL_CAPACITY];
    char destination[TITAN_DOWNLOAD_PATH_CAPACITY];
    char expected_sha256[TITAN_DOWNLOAD_SHA256_HEX_CAPACITY];
    uint64_t expected_size;
    unsigned int retry_limit;
} TitanDownloadRequest;

typedef struct TitanDownloadSnapshot
{
    unsigned int job_id;
    TitanDownloadState state;
    TitanDownloadFailure failure;
    char package_id[32];
    char title[TITAN_DOWNLOAD_TITLE_CAPACITY];
    char destination[TITAN_DOWNLOAD_PATH_CAPACITY];
    uint64_t bytes_downloaded;
    uint64_t total_bytes;
    unsigned int progress_percent;
    unsigned int speed_bytes_per_second;
    unsigned int eta_seconds;
    unsigned int retries_used;
    int total_known;
    int pause_supported;
    int resume_supported;
} TitanDownloadSnapshot;

int TitanDownloadService_Initialize(void);
void TitanDownloadService_Shutdown(void);

/* Fixed-capacity queue, no heap allocation. Returns non-zero job id or 0. */
unsigned int TitanDownloadService_Enqueue(const TitanDownloadRequest *request);
int TitanDownloadService_Cancel(unsigned int job_id);
int TitanDownloadService_Pause(unsigned int job_id);
int TitanDownloadService_Resume(unsigned int job_id);

unsigned int TitanDownloadService_GetCount(void);
int TitanDownloadService_GetSnapshot(unsigned int index, TitanDownloadSnapshot *snapshot);
int TitanDownloadService_Find(unsigned int job_id, TitanDownloadSnapshot *snapshot);

/* Storage scheduler integration. Exactly one bounded storage quantum per call. */
int TitanDownloadService_HasStorageWork(void);
int TitanDownloadService_StorageWorkerPump(void);

/* Main-thread pump publishes notification-facing snapshots only; it performs
 * no filesystem or network I/O. */
void TitanDownloadService_Update(void);

#if defined(TITAN_DOWNLOAD_SERVICE_HOST_TEST)
/* Host-only deterministic execution hook. Production target uses a dedicated
 * EE network worker and the shared Titan storage worker. */
int TitanDownloadService_TestRunNext(void);
#endif

#endif

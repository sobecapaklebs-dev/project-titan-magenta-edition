#ifndef PROJECT_TITAN_STORE_SERVICE_H
#define PROJECT_TITAN_STORE_SERVICE_H

#include "titan_repository_protocol.h"

#define TITAN_STORE_SERVICE_API_VERSION 1u

typedef enum TitanStoreResult
{
    TITAN_STORE_OK = 0,
    TITAN_STORE_NO_REPOSITORY,
    TITAN_STORE_BAD_REPOSITORY,
    TITAN_STORE_PACKAGE_NOT_FOUND,
    TITAN_STORE_DEPENDENCY_MISSING,
    TITAN_STORE_DOWNLOAD_REJECTED
} TitanStoreResult;

void TitanStoreService_Initialize(void);
TitanStoreResult TitanStoreService_AcceptRepository(
    const char *source_url,
    const char *text,
    unsigned int size
);
const TitanRepositoryCatalog *TitanStoreService_GetCatalog(void);
const char *TitanStoreService_GetRepositoryUrl(void);
TitanStoreResult TitanStoreService_QueuePackage(const char *package_id, unsigned int *out_job_id);
void TitanStoreService_Update(void);
void TitanStoreService_Shutdown(void);

#endif

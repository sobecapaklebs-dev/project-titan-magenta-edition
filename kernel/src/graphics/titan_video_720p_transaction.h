#ifndef PROJECT_TITAN_VIDEO_720P_TRANSACTION_H
#define PROJECT_TITAN_VIDEO_720P_TRANSACTION_H

#include "titan_result.h"
#include "titan_video_720p_plan.h"

typedef struct TitanVideo720pTransactionResolution
{
    TitanVideo720pPath active_path;
    int initialized;
    int renderable;
    int packet_error;
    TitanResult result;
} TitanVideo720pTransactionResolution;

TitanVideo720pPath TitanVideo720pPath_Sanitize(TitanVideo720pPath path);

TitanVideo720pTransactionResolution TitanVideo720pTransaction_Resolve(
    TitanVideo720pPath previous,
    TitanVideo720pPath requested,
    TitanResult primary_result,
    int rollback_attempted,
    TitanResult rollback_result
);

#endif

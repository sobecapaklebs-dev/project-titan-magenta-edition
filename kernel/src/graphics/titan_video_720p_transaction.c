#include "titan_video_720p_transaction.h"

TitanVideo720pPath TitanVideo720pPath_Sanitize(TitanVideo720pPath path)
{
    if (path == TITAN_VIDEO_720P_PATH_REFERENCE)
        return TITAN_VIDEO_720P_PATH_REFERENCE;
    return TITAN_VIDEO_720P_PATH_OPTIMIZED;
}

TitanVideo720pTransactionResolution TitanVideo720pTransaction_Resolve(
    TitanVideo720pPath previous,
    TitanVideo720pPath requested,
    TitanResult primary_result,
    int rollback_attempted,
    TitanResult rollback_result
)
{
    TitanVideo720pTransactionResolution resolution;

    previous = TitanVideo720pPath_Sanitize(previous);
    requested = TitanVideo720pPath_Sanitize(requested);

    if (primary_result == TITAN_RESULT_OK)
    {
        resolution.active_path = requested;
        resolution.initialized = 1;
        resolution.renderable = 1;
        resolution.packet_error = 0;
        resolution.result = TITAN_RESULT_OK;
        return resolution;
    }

    if (rollback_attempted && rollback_result == TITAN_RESULT_OK)
    {
        resolution.active_path = previous;
        resolution.initialized = 1;
        resolution.renderable = 1;
        resolution.packet_error = 0;
        resolution.result = primary_result;
        return resolution;
    }

    resolution.active_path = previous;
    resolution.initialized = 0;
    resolution.renderable = 0;
    resolution.packet_error = 1;
    resolution.result = rollback_attempted ? rollback_result : primary_result;
    return resolution;
}

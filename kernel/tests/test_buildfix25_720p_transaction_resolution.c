#include <assert.h>
#include <stdio.h>
#include "titan_video_720p_transaction.h"

static void check(TitanVideo720pPath oldp, TitanVideo720pPath newp,
                  TitanResult primary, int attempted, TitanResult rollback,
                  TitanVideo720pPath expected_path, int initialized, int renderable)
{
    TitanVideo720pTransactionResolution r = TitanVideo720pTransaction_Resolve(
        oldp, newp, primary, attempted, rollback);
    assert(r.active_path == expected_path);
    assert(r.initialized == initialized);
    assert(r.renderable == renderable);
    assert(r.packet_error == (initialized ? 0 : 1));
}

int main(void)
{
    check(TITAN_VIDEO_720P_PATH_REFERENCE, TITAN_VIDEO_720P_PATH_OPTIMIZED,
          TITAN_RESULT_OK, 0, TITAN_RESULT_OK,
          TITAN_VIDEO_720P_PATH_OPTIMIZED, 1, 1);
    check(TITAN_VIDEO_720P_PATH_OPTIMIZED, TITAN_VIDEO_720P_PATH_REFERENCE,
          TITAN_RESULT_OK, 0, TITAN_RESULT_OK,
          TITAN_VIDEO_720P_PATH_REFERENCE, 1, 1);
    check(TITAN_VIDEO_720P_PATH_REFERENCE, TITAN_VIDEO_720P_PATH_OPTIMIZED,
          TITAN_ERROR_VIDEO_INITIALIZATION, 1, TITAN_RESULT_OK,
          TITAN_VIDEO_720P_PATH_REFERENCE, 1, 1);
    check(TITAN_VIDEO_720P_PATH_OPTIMIZED, TITAN_VIDEO_720P_PATH_REFERENCE,
          TITAN_ERROR_VIDEO_INITIALIZATION, 1, TITAN_RESULT_OK,
          TITAN_VIDEO_720P_PATH_OPTIMIZED, 1, 1);
    check(TITAN_VIDEO_720P_PATH_REFERENCE, TITAN_VIDEO_720P_PATH_OPTIMIZED,
          TITAN_ERROR_VIDEO_INITIALIZATION, 1, TITAN_ERROR_VIDEO_INITIALIZATION,
          TITAN_VIDEO_720P_PATH_REFERENCE, 0, 0);
    assert(TitanVideo720pPath_Sanitize((TitanVideo720pPath)99) ==
           TITAN_VIDEO_720P_PATH_OPTIMIZED);
    puts("titan-720p-rollback-fault-matrix: PASS");
    return 0;
}

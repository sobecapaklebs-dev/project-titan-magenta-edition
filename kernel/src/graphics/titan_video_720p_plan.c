#include "titan_video_720p_plan.h"

#include <limits.h>
#include <string.h>

#define TITAN_GS_PAGE_BYTES 8192u
#define TITAN_GS_PSM16_PAGE_WIDTH 64u
#define TITAN_GS_PSM16_PAGE_HEIGHT 64u

static unsigned int TitanVideo720pPlan_CeilDiv(unsigned int value, unsigned int divisor)
{
    return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
}

unsigned int TitanVideo720pPlan_Psm16Bytes(
    unsigned int width,
    unsigned int visible_height,
    unsigned int *storage_height
)
{
    unsigned int pages_x;
    unsigned int pages_y;
    unsigned long long bytes;

    if (width == 0u || visible_height == 0u || (width % 64u) != 0u)
    {
        return 0u;
    }

    pages_x = TitanVideo720pPlan_CeilDiv(width, TITAN_GS_PSM16_PAGE_WIDTH);
    pages_y = TitanVideo720pPlan_CeilDiv(visible_height, TITAN_GS_PSM16_PAGE_HEIGHT);
    if (storage_height != 0)
    {
        *storage_height = pages_y * TITAN_GS_PSM16_PAGE_HEIGHT;
    }

    bytes = (unsigned long long)pages_x * (unsigned long long)pages_y *
        (unsigned long long)TITAN_GS_PAGE_BYTES;
    return bytes <= UINT_MAX ? (unsigned int)bytes : 0u;
}

int TitanVideo720pPlan_Build(TitanVideo720pPath path, TitanVideo720pPlan *plan)
{
    unsigned int internal_bytes;
    unsigned int output_bytes;

    if (plan == 0 ||
        (path != TITAN_VIDEO_720P_PATH_REFERENCE &&
         path != TITAN_VIDEO_720P_PATH_OPTIMIZED))
    {
        return 0;
    }

    memset(plan, 0, sizeof(*plan));
    plan->path = path;
    plan->output_width = 1280u;
    plan->output_height = 720u;
    plan->output_storage_width = 1280u;

    output_bytes = TitanVideo720pPlan_Psm16Bytes(
        plan->output_width,
        plan->output_height,
        &plan->output_storage_height
    );
    if (output_bytes == 0u)
    {
        return 0;
    }

    if (path == TITAN_VIDEO_720P_PATH_REFERENCE)
    {
        plan->internal_width = 1280u;
        plan->internal_height = 720u;
        plan->internal_storage_width = 1280u;
        plan->internal_storage_height = plan->output_storage_height;
        plan->internal_buffer_count = 0u;
        plan->output_buffer_count = 2u;
        plan->internal_framebuffer_bytes = 0u;
        plan->output_framebuffer_bytes = output_bytes * 2u;
        plan->framebuffer_bytes = plan->output_framebuffer_bytes;
        return 1;
    }

    plan->internal_width = 960u;
    plan->internal_height = 540u;
    plan->internal_storage_width = 960u;
    internal_bytes = TitanVideo720pPlan_Psm16Bytes(
        plan->internal_width,
        plan->internal_height,
        &plan->internal_storage_height
    );
    if (internal_bytes == 0u)
    {
        return 0;
    }

    plan->internal_buffer_count = 1u;
    plan->output_buffer_count = 1u;
    plan->internal_framebuffer_bytes = internal_bytes;
    plan->output_framebuffer_bytes = output_bytes;
    plan->framebuffer_bytes = internal_bytes + output_bytes;
    return 1;
}

#include <assert.h>
#include <stdio.h>
#include "titan_video_720p_plan.h"

int main(void)
{
    TitanVideo720pPlan reference;
    TitanVideo720pPlan optimized;
    unsigned int storage = 0u;

    assert(TitanVideo720pPlan_Build(TITAN_VIDEO_720P_PATH_REFERENCE, &reference));
    assert(reference.internal_width == 1280u);
    assert(reference.internal_height == 720u);
    assert(reference.output_width == 1280u);
    assert(reference.output_height == 720u);
    assert(reference.output_storage_height == 768u);
    assert(reference.output_buffer_count == 2u);
    assert(reference.framebuffer_bytes == 3932160u);

    assert(TitanVideo720pPlan_Build(TITAN_VIDEO_720P_PATH_OPTIMIZED, &optimized));
    assert(optimized.internal_width == 960u);
    assert(optimized.internal_height == 540u);
    assert(optimized.internal_storage_width == 960u);
    assert(optimized.internal_storage_height == 576u);
    assert(optimized.output_width == 1280u);
    assert(optimized.output_height == 720u);
    assert(optimized.output_storage_height == 768u);
    assert(optimized.internal_buffer_count == 1u);
    assert(optimized.output_buffer_count == 1u);
    assert(optimized.internal_framebuffer_bytes == 1105920u);
    assert(optimized.output_framebuffer_bytes == 1966080u);
    assert(optimized.framebuffer_bytes == 3072000u);
    assert(reference.framebuffer_bytes - optimized.framebuffer_bytes == 860160u);

    assert(TitanVideo720pPlan_Psm16Bytes(960u, 540u, &storage) == 1105920u);
    assert(storage == 576u);
    assert(TitanVideo720pPlan_Psm16Bytes(1280u, 720u, &storage) == 1966080u);
    assert(storage == 768u);
    assert(TitanVideo720pPlan_Psm16Bytes(961u, 540u, &storage) == 0u);

    puts("pikavr-960x540-internal-contract: OK separate 960x540 composition and 1280x720 output plans");
    return 0;
}

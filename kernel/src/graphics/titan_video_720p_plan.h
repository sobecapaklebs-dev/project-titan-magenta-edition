#ifndef PROJECT_TITAN_VIDEO_720P_PLAN_H
#define PROJECT_TITAN_VIDEO_720P_PLAN_H

typedef enum TitanVideo720pPath
{
    TITAN_VIDEO_720P_PATH_REFERENCE = 0,
    TITAN_VIDEO_720P_PATH_OPTIMIZED = 1
} TitanVideo720pPath;

typedef struct TitanVideo720pPlan
{
    TitanVideo720pPath path;
    unsigned int internal_width;
    unsigned int internal_height;
    unsigned int internal_storage_width;
    unsigned int internal_storage_height;
    unsigned int internal_buffer_count;
    unsigned int output_width;
    unsigned int output_height;
    unsigned int output_storage_width;
    unsigned int output_storage_height;
    unsigned int output_buffer_count;
    unsigned int internal_framebuffer_bytes;
    unsigned int output_framebuffer_bytes;
    unsigned int framebuffer_bytes;
} TitanVideo720pPlan;

int TitanVideo720pPlan_Build(TitanVideo720pPath path, TitanVideo720pPlan *plan);
unsigned int TitanVideo720pPlan_Psm16Bytes(
    unsigned int width,
    unsigned int visible_height,
    unsigned int *storage_height
);

#endif

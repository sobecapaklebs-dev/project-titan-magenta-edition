#ifndef TITAN_HOST_STUB_DRAW_SAMPLING_H
#define TITAN_HOST_STUB_DRAW_SAMPLING_H
#include <packet2.h>
#include <draw_buffers.h>
#define LOD_FORMULAIC 0
#define LOD_USE_K 0
#define LOD_MAG_LINEAR 1
#define LOD_MAG_NEAREST 0
#define LOD_MIN_LINEAR 1
#define LOD_MIPMAP_REGISTER 0
#define WRAP_REPEAT 0
#define WRAP_CLAMP 1
#define CLUT_STORAGE_MODE1 0
#define CLUT_NO_LOAD 0
#define CLUT_LOAD 1
#define TEXTURE_COMPONENTS_RGBA 0
#define TEXTURE_FUNCTION_MODULATE 0
#define TEXTURE_FUNCTION_DECAL 1
#define LOD_MIN_NEAREST 0
typedef struct lod_t {
    unsigned int calculation;
    unsigned int max_level;
    unsigned int mag_filter;
    unsigned int min_filter;
    unsigned int mipmap_select;
    unsigned int l;
    float k;
} lod_t;
qword_t *draw_texture_sampling(qword_t *, int, lod_t *);
qword_t *draw_texturebuffer(qword_t *, int, texbuffer_t *, clutbuffer_t *);
#endif

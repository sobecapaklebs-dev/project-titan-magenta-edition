#ifndef TITAN_HOST_STUB_DRAW_BUFFERS_H
#define TITAN_HOST_STUB_DRAW_BUFFERS_H
#include <tamtypes.h>
typedef struct framebuffer_t {
    unsigned int address;
    unsigned int width;
    unsigned int height;
    int psm;
    unsigned int mask;
} framebuffer_t;
typedef struct zbuffer_t {
    unsigned int enable;
    unsigned int method;
    unsigned int address;
    int zsm;
    unsigned int mask;
} zbuffer_t;
typedef struct texture_info_t {
    unsigned int width;
    unsigned int height;
    unsigned int components;
    unsigned int function;
} texture_info_t;
typedef struct texbuffer_t {
    unsigned int address;
    unsigned int width;
    int psm;
    texture_info_t info;
} texbuffer_t;
typedef struct clutbuffer_t {
    unsigned int address;
    int psm;
    unsigned int storage_mode;
    unsigned int start;
    unsigned int load_method;
} clutbuffer_t;
#endif

#ifndef TITAN_HOST_STUB_DRAW2D_H
#define TITAN_HOST_STUB_DRAW2D_H
#include <tamtypes.h>
typedef struct vertex_t { float x, y; unsigned int z; } vertex_t;
typedef struct texcoord_t { float u, v; } texcoord_t;
typedef struct color_t { unsigned int r, g, b, a; float q; } color_t;
typedef struct point_t { vertex_t v0; color_t color; } point_t;
typedef struct rect_t { vertex_t v0, v1; color_t color; } rect_t;
typedef struct texrect_t { vertex_t v0, v1; texcoord_t t0, t1; color_t color; } texrect_t;
typedef struct triangle_t { vertex_t v0, v1, v2; color_t color; } triangle_t;
#endif

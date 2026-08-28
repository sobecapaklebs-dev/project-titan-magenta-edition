#ifndef TITAN_HOST_STUB_DRAW_H
#define TITAN_HOST_STUB_DRAW_H
#include <packet2.h>
#include <draw_buffers.h>
#include <draw2d.h>
qword_t *draw_setup_environment(qword_t *, int, framebuffer_t *, zbuffer_t *);
qword_t *draw_clear(qword_t *, int, float, float, float, float, unsigned char, unsigned char, unsigned char);
qword_t *draw_finish(qword_t *);
void draw_wait_finish(void);
void draw_enable_blending(void);
unsigned int draw_log2(unsigned int);
qword_t *draw_point(qword_t *, int, point_t *);
qword_t *draw_rect_filled(qword_t *, int, rect_t *);
qword_t *draw_rect_textured(qword_t *, int, texrect_t *);
qword_t *draw_triangle_filled(qword_t *, int, triangle_t *);
qword_t *draw_texture_transfer(qword_t *, void *, int, int, int, int, int);
qword_t *draw_texture_flush(qword_t *);
#endif
